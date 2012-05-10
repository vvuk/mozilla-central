/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "nspr.h"
#include "prerror.h"
#include "prio.h"

#include "nsCOMPtr.h"

#include "nsASocketHandler.h"
#include "nsISocketTransportService.h"
#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "nsXPCOM.h"

#include "logging.h"
#include "transportflow.h"
#include "transportlayerprsock.h"

MLOG_INIT("mtransport");

std::string TransportLayerPrsock::ID("mt_prsock");

void TransportLayerPrsock::Import(PRFileDesc *fd, nsresult *result) {
  fd_ = fd;

  handler_ = new SocketHandler(this, fd);

  // Get the transport service as a transport service
  nsresult rv;
  stservice_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  if (!NS_SUCCEEDED(rv)) {
    *result = rv;
    return;
  }

  rv = stservice_->AttachSocket(fd_, handler_);
  if (!NS_SUCCEEDED(rv)) {
    *result = rv;
    return;
  }
  
  SetState(OPEN);

  *result = NS_OK;
}

int TransportLayerPrsock::SendPacket(const unsigned char *data, size_t len) {
  MLOG(PR_LOG_DEBUG, LAYER_INFO << "SendPacket(" << len << ")");
  if (state_ != OPEN) {
    MLOG(PR_LOG_DEBUG, LAYER_INFO << "Can't send packet on closed interface");
    return TE_INTERNAL;
  }

  PRInt32 status;
  status = PR_Write(fd_, data, len);
  if (status >= 0) {
    return status;
  }

  PRErrorCode err = PR_GetError();
  if (err == PR_WOULD_BLOCK_ERROR) {
    return TE_WOULDBLOCK;
  }


  MLOG(PR_LOG_DEBUG, LAYER_INFO << "Write error; channel closed");
  SetState(ERROR);
  return TE_ERROR;
}

void TransportLayerPrsock::OnSocketReady(PRFileDesc *fd, PRInt16 outflags) {
  if (!(outflags & PR_POLL_READ)) {
    return;
  }

  MLOG(PR_LOG_DEBUG, LAYER_INFO << "OnSocketReady(flags=" << outflags << ")");
  
  unsigned char buf[1600];
  PRInt32 rv = PR_Read(fd, buf, sizeof(buf));

  if (rv > 0) {
    // Successful read
    MLOG(PR_LOG_DEBUG, LAYER_INFO << "Read " << rv << " bytes");
    SignalPacketReceived(this, buf, rv);
  } else if (rv == 0) {
    MLOG(PR_LOG_DEBUG, LAYER_INFO << "Read 0 bytes; channel closed");
    SetState(CLOSED);
  } else {
    PRErrorCode err = PR_GetError();
    
    if (err != PR_WOULD_BLOCK_ERROR) {
      MLOG(PR_LOG_DEBUG, LAYER_INFO << "Read error; channel closed");
      SetState(ERROR);
    }
  }
}


NS_IMPL_THREADSAFE_ISUPPORTS0(TransportLayerPrsock::SocketHandler);
