
/*
Modified version of nr_socket_local, adapted for NSPR
*/

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Original code from nICEr:

Copyright (c) 2007, Adobe Systems, Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of Adobe Systems, Network Resonance nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <csi_platform.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include "nr_api.h"
#include "nr_socket.h"

#include "nspr.h"
#include "prerror.h"
#include "prio.h"
#include "prnetdb.h"

#include "nr_socket_local.h"

typedef struct nr_socket_local_ {
  nr_transport_addr my_addr;
  PRFileDesc *sock;
} nr_socket_local;


static int nr_socket_local_destroy(void **objp);
static int nr_socket_local_sendto(void *obj,const void *msg, size_t len,
  int flags, nr_transport_addr *to);
static int nr_socket_local_recvfrom(void *obj,void * restrict buf,
  size_t maxlen, size_t *len, int flags, nr_transport_addr *from);
static int nr_socket_local_getfd(void *obj, NR_SOCKET *fd);
static int nr_socket_local_getaddr(void *obj, nr_transport_addr *addrp);
static int nr_socket_local_close(void *obj);

static nr_socket_vtbl nr_socket_local_vtbl={
  nr_socket_local_destroy,
  nr_socket_local_sendto,
  nr_socket_local_recvfrom,
  nr_socket_local_getfd,
  nr_socket_local_getaddr,
  nr_socket_local_close
};

static int nr_transport_addr_to_praddr(nr_transport_addr *addr,
  PRNetAddr *naddr)
  {
    int _status;
    
    memset(naddr, 0, sizeof(PRNetAddr));

    switch(addr->protocol){
      case IPPROTO_TCP:
        ABORT(R_INTERNAL); /* Can't happen for now */
        break;
      case IPPROTO_UDP:
        break;
      default:
        ABORT(R_BAD_ARGS);
    }
    
    switch(addr->ip_version){
      case NR_IPV4:
        naddr->inet.family = PR_AF_INET;
        naddr->inet.port = addr->u.addr4.sin_port;
        naddr->inet.ip = addr->u.addr4.sin_addr.s_addr;
        break;
      case NR_IPV6:
        naddr->ipv6.family = PR_AF_INET6;
        naddr->ipv6.port = addr->u.addr6.sin6_port;
        memcpy(naddr->ipv6.ip._S6_un._S6_u8,
               &addr->u.addr6.sin6_addr.__u6_addr.__u6_addr8, 16);
        break;
      default:
        ABORT(R_BAD_ARGS);
    }

    _status = 0;
  abort:
    return(_status);
  }

static int nr_praddr_to_transport_addr(PRNetAddr *praddr,
  nr_transport_addr *addr, int keep)
  {
    int _status;
    int r;

    switch(praddr->raw.family) {
      case PR_AF_INET:
        r = nr_sockaddr_to_transport_addr((sockaddr *)&praddr->raw,
          sizeof(struct sockaddr_in),IPPROTO_UDP,keep,addr);
        break;
      case PR_AF_INET6:
        r = nr_sockaddr_to_transport_addr((sockaddr *)&praddr->raw,
          sizeof(struct sockaddr_in6),IPPROTO_UDP,keep,addr);
        break;
      default:
        ABORT(R_BAD_ARGS);
    }

    _status=0;
 abort:
    return(_status);
  }

int nr_socket_local_create(nr_transport_addr *addr, nr_socket **sockp)
  {
    int r,_status;
    nr_socket_local *lcl=0;
    PRStatus status;
    PRNetAddr naddr;

    if((r=nr_transport_addr_to_praddr(addr, &naddr)))
      ABORT(r);

    if (!(lcl=(nr_socket_local *)RCALLOC(sizeof(nr_socket_local))))
      ABORT(R_NO_MEMORY);
    lcl->sock=0;
    
    if (!(lcl->sock = PR_NewUDPSocket())) {
      r_log(LOG_GENERIC,LOG_CRIT,"Couldn't create socket");
      ABORT(R_INTERNAL);
    }

    status = PR_Bind(lcl->sock, &naddr);
    if (status != PR_SUCCESS) {
      r_log(LOG_GENERIC,LOG_CRIT,"Couldn't bind socket to address %s",
            addr->as_string);
      ABORT(R_INTERNAL);
    }

    r_log(LOG_GENERIC,LOG_DEBUG,"Creating socket %d with addr %s",
          lcl->sock,addr->as_string);
    nr_transport_addr_copy(&lcl->my_addr,addr);

    /* If we have a wildcard port, patch up the addr */
    if(nr_transport_addr_is_wildcard(addr)){
      status = PR_GetSockName(lcl->sock, &naddr);
      if (status != PR_SUCCESS)
        ABORT(R_INTERNAL);
      
      if((r=nr_praddr_to_transport_addr(&naddr,&lcl->my_addr,1)))
         ABORT(r);
    }
    
    if((r=nr_socket_create_int(lcl, &nr_socket_local_vtbl, sockp)))
      ABORT(r);

    _status=0;
  abort:
    if(_status){
      nr_socket_local_destroy((void **)&lcl);
    }
    return(_status);
  }


static int nr_socket_local_destroy(void **objp)
  {
    nr_socket_local *lcl;

    if(!objp || !*objp)
      return(0);

    lcl=(nr_socket_local *)*objp;
    *objp=0;

    if(lcl->sock)
      PR_Close(lcl->sock);

    RFREE(lcl);
    
    return(0);
  }

static int nr_socket_local_sendto(void *obj,const void *msg, size_t len,
  int flags, nr_transport_addr *addr)
  {
    int r,_status;
    nr_socket_local *lcl=(nr_socket_local *)obj;
    PRNetAddr naddr;
    PRInt32 status;

    if ((r=nr_transport_addr_to_praddr(addr, &naddr)))
      ABORT(r);

    if(lcl->sock==NULL)
      ABORT(R_EOD);

    // TODO(ekr@rtfm.com): Convert flags?
    status = PR_SendTo(lcl->sock, msg, len, flags, &naddr, PR_INTERVAL_NO_WAIT);
    if (status != len) {
      r_log_e(LOG_GENERIC, LOG_INFO, "Error in sendto %s", addr->as_string);

      ABORT(R_IO_ERROR);
    }

    _status=0;
  abort:
    return(_status);
  }

static int nr_socket_local_recvfrom(void *obj,void * restrict buf,
  size_t maxlen, size_t *len, int flags, nr_transport_addr *addr)
  {
    int r,_status;
    nr_socket_local *lcl=(nr_socket_local *)(obj);
    PRNetAddr from;
    PRInt32 status;
    PRNetAddr addr_in;

    status = PR_RecvFrom(lcl->sock, buf, maxlen, flags, &from, PR_INTERVAL_NO_WAIT);
    if (status <= 0) {
      r_log_e(LOG_GENERIC,LOG_ERR,"Error in recvfrom");
      ABORT(R_IO_ERROR);
    }
    *len=status;

    if((r=nr_praddr_to_transport_addr(&from,addr,0)))
      ABORT(r);
    
    //r_log(LOG_GENERIC,LOG_DEBUG,"Read %d bytes from %s",*len,addr->as_string);

    _status=0;
  abort:
    return(_status);
  }

static int nr_socket_local_getfd(void *obj, NR_SOCKET *fd)
  {
    nr_socket_local *lcl=(nr_socket_local *)(obj);

    if(lcl->sock==NULL)
      return(R_BAD_ARGS);

    // TODO: implement
    abort();
    *fd=0;

    return(0);
  }

static int nr_socket_local_getaddr(void *obj, nr_transport_addr *addrp)
  {
    nr_socket_local *lcl=(nr_socket_local *)obj;
      
    nr_transport_addr_copy(addrp,&lcl->my_addr);

    return(0);
  }

static int nr_socket_local_close(void *obj)
  {
    nr_socket_local *lcl=(nr_socket_local *)obj;

    //r_log(LOG_GENERIC,LOG_DEBUG,"Closing sock (%x:%d)",lcl,lcl->sock);

    if(lcl->sock != NULL){
      // NR_ASYNC_CANCEL(lcl->sock,NR_ASYNC_WAIT_READ);
      // NR_ASYNC_CANCEL(lcl->sock,NR_ASYNC_WAIT_WRITE);
      // TODO(ekr@rtfm.com): Need to close... PR_Close(lcl->sock);
    }
    lcl->sock=NULL;

    return(0);
  }
