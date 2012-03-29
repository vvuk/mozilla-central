/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#if !defined(__Userspace_os_Windows)
#include <arpa/inet.h>
#endif

#if defined(__Userspace_os_Darwin)
// sctp undefines __APPLE__, so reenable them.
#define __APPLE__ 1
#endif
#include "DataChannelLog.h"
#if defined(__Userspace_os_Darwin)
#undef __APPLE__
#endif

#include "nsThreadUtils.h"
#include "nsAutoPtr.h"
#include "DataChannel.h"
#include "DataChannelProtocol.h"

PRLogModuleInfo* dataChannelLog;

extern "C" {
// Hack fix for define issue in sctp lib
#define __USER_CODE 1
#include "usrsctp.h"
}

// XXX Notes
// Use static casts
// check/fix ownerships
// add assertions
// clean up logging

#if 1
#define ARRAY_LEN(x) (x).Length()
#else
#define ARRAY_LEN(x) (sizeof((x))/sizeof((x)[0]))
#endif

#undef LOG
#define LOG(x)   do { printf x; putc('\n',stdout); fflush(stdout);} while (0)

static bool sctp_initialized;

namespace mozilla {

static int
receive_cb(struct socket* sock, union sctp_sockstore addr, 
           void *data, size_t datalen, 
           struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{
  DataChannelConnection *connection = static_cast<DataChannelConnection*>(ulp_info);	
  return connection->ReceiveCallback(sock, data, datalen, rcv);
}


DataChannelConnection::DataChannelConnection(DataConnectionListener *listener) :
   mLock("netwerk::sctp::DataChannel")
{
  mState = CLOSED;
  mSocket = NULL;
  mMasterSocket = NULL;
  mListener = listener;
  mNumChannels = 0;
#if 1
  printf("mChannelsOut.Capacity = %d\n",mChannelsOut.Capacity());

  mChannelsOut.AppendElements(mChannelsOut.Capacity());
  mChannelsIn.AppendElements(mChannelsOut.Capacity());
  for (PRUint32 i = 0; i < mChannelsOut.Capacity(); i++) {
    mChannelsOut[i].reverse = INVALID_STREAM;
    mChannelsOut[i].pending = false;
    mChannelsIn[i].outgoing = INVALID_STREAM;
  }
#else
  memset(mChannelsOut,'\0',sizeof(mChannelsOut));
  memset(mChannelsIn,'\0',sizeof(mChannelsIn));
  for (PRUint32 i = 0; i < ARRAY_LEN(mChannelsOut); i++) {
    mChannelsOut[i].reverse = INVALID_STREAM;
    mChannelsOut[i].pending = false;
  }
  for (PRUint32 i = 0; i < ARRAY_LEN(mChannelsIn); i++) {
    mChannelsIn[i].outgoing = INVALID_STREAM;
  }
#endif
  LOG(("DataChannelConnection created"));
}

bool
DataChannelConnection::Init(unsigned short port/* XXX DTLSConnection &tunnel*/)
{
  struct sctp_udpencaps encaps;

  {
    MutexAutoLock lock(mLock);
    if (!sctp_initialized)
    {
      LOG(("sctp_init(%d)",port+1));
      usrsctp_init(port,NULL); // XXX fix

      //SCTP_BASE_SYSCTL(sctp_debug_on) = SCTP_DEBUG_ALL;
      usrsctp_sysctl_set_sctp_debug_on(0x0);
      usrsctp_sysctl_set_sctp_blackhole(2);
      sctp_initialized = true;
    }
  }

  // Open sctp association across tunnel
  // XXX This code will need to change to support SCTP-over-DTLS
  if ((mMasterSocket = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, receive_cb, NULL, 0, this)) == NULL) {
    return false;
  }

  // XXX this gets replaced when we have a DTLS connection to use
  memset(&encaps, 0, sizeof(encaps));
  encaps.sue_address.ss_family = AF_INET;
  encaps.sue_port = htons(port^1); // XXX also causes problems with loopback
  if (usrsctp_setsockopt(mMasterSocket, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT,
                         (const void*)&encaps, 
                         (socklen_t)sizeof(struct sctp_udpencaps)) < 0) {
    LOG(("*** failed encaps"));
    return false;
  }
  LOG(("SCTP encapsulation remote port %d, local port %d",port^1,port));

  mSocket = NULL;
  return true;
}

// listen for incoming associations
bool
DataChannelConnection::Listen(unsigned short port)
{
  struct sockaddr_in addr;
  socklen_t addr_len;
  // XXX EVIL and blocks  -- replace
  if (port == 0)
    port = 13;

  // XXX This code will need to change to support SCTP-over-DTLS

  /* Acting as the 'server' */
  memset((void *)&addr, 0, sizeof(addr));
#ifdef HAVE_SIN_LEN
  addr.sin_len = sizeof(struct sockaddr_in);
#endif
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);  // XXX daytime... 
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  LOG(("Waiting for connections on port %d\n",ntohs(addr.sin_port)));
  mState = CONNECTING;
  if (usrsctp_bind(mMasterSocket, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
    LOG(("***Failed userspace_bind"));
    return false;
  }
  if (usrsctp_listen(mMasterSocket, 1) < 0) {
    LOG(("***Failed userspace_listen"));
    return false;
  }

  addr_len = 0;
  if ((mSocket = usrsctp_accept(mMasterSocket, NULL, &addr_len)) == NULL) {
    LOG(("***Failed accept"));
    return false;
  }
  mState = OPEN;

  LOG(("Accepting incoming connection.  Entering connected mode\n"));

  // Notify Connection open
  // XXX We need to make sure connection sticks around until the message is delivered
  NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                            DataChannelOnMessageAvailable::ON_CONNECTION,
                            this, NULL));
  mNumChannels = 0;
  return true;
}

bool
DataChannelConnection::Connect(const char *addr, unsigned short port)
{
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;
  // XXX EVIL and blocks  -- replace

  // XXX This code will need to change to support SCTP-over-DTLS

  /* Acting as the connector */
  LOG(("Connecting to %s, port %u\n",addr, port));
  memset((void *)&addr4, 0, sizeof(struct sockaddr_in));
  memset((void *)&addr6, 0, sizeof(struct sockaddr_in6));
#if !defined(__Userspace_os_Linux) && !defined(__Userspace_os_Windows)
  addr4.sin_len = sizeof(struct sockaddr_in);
  addr6.sin6_len = sizeof(struct sockaddr_in6);
#endif
  addr4.sin_family = AF_INET;
  addr6.sin6_family = AF_INET6;
  addr4.sin_port = htons(port); 
  addr6.sin6_port = htons(port);
  mState = CONNECTING;

#if !defined(__Userspace_os_Windows)
  if (inet_pton(AF_INET6, addr, &addr6.sin6_addr) == 1) {
    if (usrsctp_connect(mMasterSocket, (struct sockaddr *)&addr6, sizeof(struct sockaddr_in6)) < 0) {
      LOG(("*** Failed userspace_connect"));
      return false;
    }
  } else if (inet_pton(AF_INET, addr, &addr4.sin_addr) == 1) {
    if (usrsctp_connect(mMasterSocket, (struct sockaddr *)&addr4, sizeof(struct sockaddr_in)) < 0) {
      LOG(("*** Failed userspace_connect"));
      return false;
    }
  } else {
    LOG(("*** Illegal destination address.\n"));
  }
#else
  {
    struct sockaddr_storage ss;
    int sslen = sizeof(ss);

    if (!WSAStringToAddressA(const_cast<char *>(addr), AF_INET6, NULL, (struct sockaddr*)&ss, &sslen)) {
      addr6.sin6_addr = ((struct sockaddr_in6 *)&ss)->sin6_addr;
      if (usrsctp_connect(mMasterSocket, (struct sockaddr *)&addr6, sizeof(struct sockaddr_in6)) < 0) {
        LOG(("*** Failed userspace_connect"));
        return false;
      }
    } else if (!WSAStringToAddressA(const_cast<char *>(addr), AF_INET, NULL, (struct sockaddr*)&ss, &sslen)) {
      addr4.sin_addr = ((struct sockaddr_in *)&ss)->sin_addr;
      if (usrsctp_connect(mMasterSocket, (struct sockaddr *)&addr4, sizeof(struct sockaddr_in)) < 0) {
        LOG(("*** Failed userspace_connect"));
        return false;
      }
    } else {
      LOG(("*** Illegal destination address.\n"));
    }
  }
#endif

  mSocket = mMasterSocket;  // XXX Be careful!  

  LOG(("connect() succeeded!  Entering connected mode\n"));
  mState = OPEN;
  
  // Notify Connection open
  // XXX We need to make sure connection sticks around until the message is delivered
  NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                            DataChannelOnMessageAvailable::ON_CONNECTION,
                            this, NULL));
  mNumChannels = 0;
  return true;
}

void
DataChannelConnection::SendErrorResponse(struct socket* sock,
                                         struct rtcweb_datachannel_open *msg,
                                         struct sctp_rcvinfo rcv,
                                         uint8_t error)
{
  struct rtcweb_datachannel_open_response response;
  struct sctp_sndinfo sndinfo;

  /* ok, send a response */
  response.msg_type = DATA_CHANNEL_OPEN_RESPONSE;
  response.reverse_stream = rcv.rcv_sid;
  response.error = error;

  /*
    send the error back on the incoming stream id - we can report errors on any stream
    as reverse_stream tells the other side what request got an error.
  */
  memset(&sndinfo, 0, sizeof(struct sctp_sndinfo));
  sndinfo.snd_sid = rcv.rcv_sid;
  sndinfo.snd_ppid = htonl(DATA_CHANNEL_PPID_CONTROL);

  if (usrsctp_sendv(sock, &response, sizeof(response),
                    NULL, 0,
                    &sndinfo, (socklen_t)sizeof(struct sctp_sndinfo),
                    SCTP_SENDV_SNDINFO, 0) < 0) {
    LOG(("error %d sending response\n",errno));
    /* hard to send an error here... */
  }
}

// NOTE: not called on main thread
int
DataChannelConnection::ReceiveCallback(struct socket* sock, void *data, size_t datalen, struct sctp_rcvinfo rcv)
{
  struct sctp_sndinfo sndinfo;
  if (data == NULL) {
    usrsctp_close(sock);
  } else {
    DataChannel *channel;
    struct rtcweb_datachannel_open *msg_generic;
    uint16_t forward,reverse;
    PRUint32 i;

    // XXX SCTP library API will be changing to hide mbuf interface (control->data)
    switch (rcv.rcv_ppid) {
      case DATA_CHANNEL_PPID_CONTROL:
        msg_generic = (struct rtcweb_datachannel_open *)data;

        LOG(("Got control msg: %s\n",
             msg_generic->msg_type == DATA_CHANNEL_OPEN ? "open" :
             (msg_generic->msg_type == DATA_CHANNEL_OPEN_RESPONSE ? "open_response" :
              (msg_generic->msg_type == DATA_CHANNEL_ACK ? "ACK" : "unknown!"))));

        switch (msg_generic->msg_type) {
          case DATA_CHANNEL_OPEN:
            {
            // XXX static_cast?
            struct rtcweb_datachannel_open *msg = 
              (struct rtcweb_datachannel_open *) msg_generic;

            LOG(("rtcweb_datachannel_open = \n"
                 "  type\t\t\t%u\n"
                 "  channel_type\t\t%u\n"
                 "  flags\t\t\t0x%04x\n"
                 "  reliability\t\t0x%04x\n"
                 "  label\t\t\t%s\n",
                 msg->msg_type, msg->channel_type, msg->flags,
                 msg->reliability_params,
                 /* XXX */ &(((uint16_t *) (&msg->reliability_params))[1])));

            if (mChannelsIn[rcv.rcv_sid].outgoing != INVALID_STREAM) {
              LOG(("error, channel %u in use\n",rcv.rcv_sid));
              SendErrorResponse(sock,msg,rcv,ERR_DATA_CHANNEL_ALREADY_OPEN);
              break;
            }
            reverse = rcv.rcv_sid;
            {
              MutexAutoLock lock(mLock);

              for (i = 0; i < ARRAY_LEN(mChannelsOut); i++) {
                /* reverse being set tells us the datachannel is open already */
                if (mChannelsOut[i].reverse == INVALID_STREAM &&
                    !mChannelsOut[i].pending) {
                  break;
                }
              }
              if (i >= ARRAY_LEN(mChannelsOut)) {
                MutexAutoUnlock unlock(mLock);
                LOG(("no reverse channel available!\n"));
                /* XXX renegotiate for more streams */
                /* probably should renegotiate if we're near the limit... */
                mChannelsIn[reverse].outgoing = INVALID_STREAM;
                SendErrorResponse(sock,msg, rcv, ERR_DATA_CHANNEL_NONE_AVAILABLE);
                break;
              }
              forward = i;
              mChannelsOut[forward].reverse = reverse;
              mChannelsIn[reverse].outgoing = forward;
              LOG(("selected stream %d\n",forward));
            }

            mChannelsOut[forward].channel = new DataChannel(this,
                                                            forward, DataChannel::CONNECTING,
                                                            NULL, NULL);
            if (!mChannelsOut[forward].channel) {
              LOG(("Couldn't create DataChannel object for stream %d",forward));
              {
                MutexAutoLock lock(mLock);
                mChannelsOut[forward].reverse = INVALID_STREAM;
                mChannelsIn[reverse].outgoing = INVALID_STREAM;
              }
              SendErrorResponse(sock,msg,rcv,ERR_DATA_CHANNEL_NONE_AVAILABLE);
              break;
            }

            mChannelsOut[forward].channel_type       = msg->channel_type;
            mChannelsOut[forward].flags              = msg->flags;
            mChannelsOut[forward].reliability_params = msg->reliability_params;
            mChannelsOut[forward].priority           = msg->priority;

            /* Label is in msg_type_data */
            /* FIX! */

            {
              struct rtcweb_datachannel_open_response response;
              /* ok, send a response */
              response.msg_type = DATA_CHANNEL_OPEN_RESPONSE;
              response.flags = 0; // XXX should this be the source flags? 
              response.reverse_stream = reverse;
              response.error = 0;
              memset(&sndinfo, 0, sizeof(struct sctp_sndinfo));
              sndinfo.snd_ppid = htonl(DATA_CHANNEL_PPID_CONTROL);
              sndinfo.snd_sid = forward;
              if (usrsctp_sendv(sock, &response, sizeof(response),
                                NULL, 0,
                                &sndinfo, (socklen_t)sizeof(struct sctp_sndinfo),
                                SCTP_SENDV_SNDINFO, 0) < 0) {
                LOG(("error %d sending response\n",errno));
                /* I don't think we need to lock here */
                mChannelsOut[forward].reverse = INVALID_STREAM;
                mChannelsIn[reverse].outgoing = INVALID_STREAM;
                delete mChannelsOut[forward].channel;
                mChannelsOut[forward].channel = NULL;
                /* hard to send an error here... */
                break;
              }
              mChannelsOut[forward].waiting_ack = true;
            }
            mNumChannels++;
            LOG(("successful open of in: %u, out: %u, total channels %d\n",
                 reverse, forward, mNumChannels));

            /* Notify ondatachannel */
            // XXX We need to make sure connection sticks around until the message is delivered
            NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                                      DataChannelOnMessageAvailable::ON_CHANNEL_CREATED,
                                      this,mChannelsOut[forward].channel));
            }
            break;

          case DATA_CHANNEL_OPEN_RESPONSE:
            // XXX static_cast?
            {
            struct rtcweb_datachannel_open_response *msg = 
              (struct rtcweb_datachannel_open_response *) msg_generic;
            {
              MutexAutoLock lock(mLock);
              // If it's pending, but channel is NULL, then the channel has been
              // closed, and we should ACK-and-close.
              if (!mChannelsOut[msg->reverse_stream].pending) {
                LOG(("Error: open_response for non-pending channel %u (on %u)\n",
                     msg->reverse_stream, rcv.rcv_sid));
                break;
              }
              if (msg->error) {
                LOG(("Error: open_response for %u returned error %u\n",
                     msg->reverse_stream, msg->error));
                mChannelsOut[msg->reverse_stream].pending   = 0;
                mChannelsOut[msg->reverse_stream].reverse   = INVALID_STREAM;
                mChannelsIn[rcv.rcv_sid].outgoing = INVALID_STREAM;
                if (!mChannelsOut[msg->reverse_stream].channel) {
                  // already closed - reset stream and we're done
                  // XXX reset SCTP stream
                } else {
                  mChannelsOut[msg->reverse_stream].channel->SetReadyState(DataChannel::CLOSED);
                  // XXX Fire onerror!
                }
                break;
              }
              mChannelsOut[msg->reverse_stream].pending   = 0;
              mChannelsOut[msg->reverse_stream].reverse   = rcv.rcv_sid;
              mChannelsIn[rcv.rcv_sid].outgoing = msg->reverse_stream;
            }

            struct rtcweb_datachannel_ack ack;
            /* ok, send an ack */
            ack.msg_type = DATA_CHANNEL_ACK;
            memset(&sndinfo, 0, sizeof(struct sctp_sndinfo));
            sndinfo.snd_ppid = htonl(DATA_CHANNEL_PPID_CONTROL);
            sndinfo.snd_sid = msg->reverse_stream;
              if (usrsctp_sendv(sock, &ack, sizeof(ack),
                                NULL, 0,
                                &sndinfo, (socklen_t)sizeof(struct sctp_sndinfo),
                                SCTP_SENDV_SNDINFO, 0) < 0) {
              LOG(("error %d sending ack\n",errno));
              /* I don't think we need to lock here */
              mChannelsOut[msg->reverse_stream].reverse   = INVALID_STREAM;
              mChannelsIn[rcv.rcv_sid].outgoing = INVALID_STREAM;
              if (mChannelsOut[msg->reverse_stream].channel) // may be closed/null
                mChannelsOut[msg->reverse_stream].channel->SetReadyState(DataChannel::OPEN);
              /* hard to send an error to other side here... */
              // XXX fire onerror!
              break;
            }
            if (!mChannelsOut[msg->reverse_stream].channel) {
              // channel closed before OPEN_RESPONSE.  Send ACK (already done) and then 
              // close/reset the stream
              mChannelsOut[msg->reverse_stream].reverse   = INVALID_STREAM;
              mChannelsIn[rcv.rcv_sid].outgoing = INVALID_STREAM;
              // XXX reset stream
              break;
            }

            mNumChannels++;
            mChannelsOut[msg->reverse_stream].channel->SetReadyState(DataChannel::OPEN);
            LOG(("successful open of in: %u, out: %u, total channels %d\n",
                 rcv.rcv_sid, msg->reverse_stream, mNumChannels));

            /* Notify onopened */
            {
              // XXX We need to make sure this sticks around until the message is delivered
              channel = mChannelsOut[mChannelsIn[rcv.rcv_sid].outgoing].channel;
              if (!channel) {
                LOG(("DataChannel:: no channel for incoming data on stream %d\n",rcv.rcv_sid));
                break;
              }
              NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                                        DataChannelOnMessageAvailable::ON_CHANNEL_OPEN,
                                        channel));
              // XXX need to do this on stream reset/close as well
            }
            }
            break;

          case DATA_CHANNEL_ACK:
            {
              MutexAutoLock lock(mLock);
              forward = mChannelsIn[rcv.rcv_sid].outgoing;
              if (!mChannelsOut[forward].waiting_ack) {
                LOG(("Error: ack on %u for channel %u not waiting\n",
                     rcv.rcv_sid, forward));
                break;
              }
              mChannelsOut[forward].waiting_ack   = 0;
              mChannelsOut[forward].channel->SetReadyState(DataChannel::OPEN);
            }
            LOG(("Got ACK on stream %u for stream %u\n",
                 rcv.rcv_sid, forward));
            // XXX make this a function
            /* Notify onopened */
            {
              // XXX We need to make sure this sticks around until the message is delivered
              channel = mChannelsOut[forward].channel;
              if (!channel) {
                LOG(("DataChannel:: no channel for incoming data on stream %d\n",rcv.rcv_sid));
                break;
              }
              NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                                        DataChannelOnMessageAvailable::ON_CHANNEL_OPEN,
                                        channel));
              // XXX need to do this on stream reset/close as well
            }
            break;

          default:
            LOG(("Error: Unknown message received: %u\n",msg_generic->msg_type));
            break;
        }
        break;

      case DATA_CHANNEL_PPID_DOMSTRING:
        if (mChannelsIn[rcv.rcv_sid].outgoing == INVALID_STREAM) {
          LOG(("message received on channel %d for closed channel",rcv.rcv_sid));
          break;
        }
        LOG(("Received DOMString, channel %d (%p), len %d\n",rcv.rcv_sid,
             mChannelsOut[mChannelsIn[rcv.rcv_sid].outgoing].channel,
             datalen));
        fwrite(data,datalen,1,stdout);
        fwrite("\n",1,1,stdout);
        // FALLTHROUGH
      case DATA_CHANNEL_PPID_BINARY:
        if (ntohl(rcv.rcv_ppid) == DATA_CHANNEL_PPID_BINARY) {
          if (mChannelsIn[rcv.rcv_sid].outgoing == INVALID_STREAM) {
            LOG(("binary message received on channel %d for closed channel",rcv.rcv_sid));
            break;
          }
          LOG(("Received binary, channel %d (%p), len %d\n",rcv.rcv_sid,
               mChannelsOut[mChannelsIn[rcv.rcv_sid].outgoing].channel,
               datalen));
        }

        // XXX We need to make sure this sticks around until the message is delivered
        channel = mChannelsOut[mChannelsIn[rcv.rcv_sid].outgoing].channel;
        if (!channel) {
          LOG(("DataChannel:: no channel for incoming data on stream %d\n",rcv.rcv_sid));
          break;
        }

        // XXX Collect all the data (this stack API will change soon)
        {
          nsCString recvData((const char *)data, datalen);
          PRInt32 len;

         /* for (m = SCTP_BUF_NEXT(control->data); m; m = SCTP_BUF_NEXT(m)) {
            recvData.Append(m->m_data, SCTP_BUF_LEN(m));
          }*/
          if (ntohl(rcv.rcv_ppid) == DATA_CHANNEL_PPID_DOMSTRING) {
            len = -1; // Flag for DOMString

            // paranoia (WebSockets does this)
            if (!IsUTF8(recvData, false)) {
              LOG(("DataChannel:: text frame invalid utf-8\n"));
              // XXX AbortSession(NS_ERROR_ILLEGAL_VALUE);
              return false;
            }
          }

          /* Notify onmessage */
          NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                                    DataChannelOnMessageAvailable::ON_DATA,
                                    channel, recvData, len));
        }
        break;

      default:
        LOG(("Error: Unknown ppid %u\n", ntohl(rcv.rcv_ppid)));
        break;
    } /* switch ppid */
  }
  return 1;
}

struct large_msg {
  struct rtcweb_datachannel_open msg;
  char label[256]; // XXX make this correct based on max label length
};

// XXX FIX! priority
DataChannel *
DataChannelConnection::Open(/*const std::wstring& label,*/ Type type, bool inOrder, 
                            PRUint32 timeout, DataChannelListener *aListener,
                            nsISupports *aContext)
{
  NS_ABORT_IF_FALSE(NS_IsMainThread(), "not main thread"); // XXX is this needed?
  // We really could allow this from other threads, so long as we deal with
  // asynchronosity issues with channels closing, in particular access to
  // mChannelsOut, and issues with the association closing (access to mSocket).

  PRUint16 stream;
  PRUint16 flags = inOrder ? 0 : DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED;
  struct large_msg large;
  struct rtcweb_datachannel_open *msg = &large.msg;
  size_t len;
  struct sctp_prinfo prinfo;
  struct sctp_sndinfo sndinfo;
  struct sctp_sendv_spa spa;

  {
    MutexAutoLock lock(mLock);

    PRInt32 size = ARRAY_LEN(mChannelsOut);
    for (stream = 0; stream < size; stream++) {
      /* reverse being set tells us the datachannel is open */
      if (mChannelsOut[stream].reverse == INVALID_STREAM &&
          !mChannelsOut[stream].pending) {
        /* found a free outgoing stream */
        break;
      }
    }
    if (stream >= size) {
      MutexAutoUnlock unlock(mLock);
      LOG(("Error: all streams in use!\n"));
      /* XXX renegotiate for more streams */
      /* probably should renegotiate async if we're near the limit... */
#if 0
      DataChannelOut *out = mChannelsOut.AppendElements(new_size-size);
      DataChannelIn  *in  = mChannelsIn.AppendElements(new_size-size);
      if (!out || !in)
        return NULL;
      for (PRUInt32 i = size; i < new_size; i++) {
        mChannelsOut[i].reverse = INVALID_STREAM;
        mChannelsOut[i].pending = false;
        mChannelsIn[i].outgoing = INVALID_STREAM;
      }
      stream = size;
#else
      return NULL;
#endif
    }
    mChannelsOut[stream].pending = true;
  }

  switch(type) {
    case RELIABLE:
      //flags = 0; set above
      prinfo.pr_policy = 0;
      break;
    case UNRELIABLE:
    case PARTIAL_RELIABLE_REXMIT:
      flags = SCTP_UNORDERED;
      prinfo.pr_policy = SCTP_PR_SCTP_RTX;
      break;
    case PARTIAL_RELIABLE_TIMED:
      flags = SCTP_UNORDERED;
      prinfo.pr_policy = SCTP_PR_SCTP_TTL;
      break;
    case RELIABLE_STREAM:
      LOG(("Reliable stream not handled yet\n"));
      //flags = 0; set above
      break;
  }
  prinfo.pr_value = timeout;
  mChannelsOut[stream].channel_type = type;
  mChannelsOut[stream].flags = flags;
  mChannelsOut[stream].reverse = INVALID_STREAM;
  mChannelsOut[stream].reliability_params = timeout;
  mChannelsOut[stream].priority = 0; // XXX implement
  mChannelsOut[stream].channel = new DataChannel(this,
                                                 stream,DataChannel::CONNECTING,
                                                 aListener,
                                                 aContext);

  msg[0].msg_type = DATA_CHANNEL_OPEN;
  msg[0].channel_type = type;
  msg[0].flags = flags;
  msg[0].reliability_params = timeout;
  msg[0].priority = mChannelsOut[stream].priority;
  // XXX FIX! use label
  // XXX Limit length of label and/or reallocate
  sprintf((char *) &((&msg[0].reliability_params)[1]),"chan %d",stream);
  len = sizeof(msg) + strlen((char *) &((&msg[0].reliability_params)[1]));
  sndinfo.snd_sid = stream;
  sndinfo.snd_flags = flags;
  sndinfo.snd_ppid = htonl(DATA_CHANNEL_PPID_CONTROL);
  sndinfo.snd_context = 0;
  sndinfo.snd_assoc_id = 0;

  spa.sendv_sndinfo = sndinfo;
  spa.sendv_prinfo = prinfo;
  spa.sendv_flags = SCTP_SEND_SNDINFO_VALID | SCTP_SEND_PRINFO_VALID;
  LOG(("Sending open for stream %d\n",stream));

  if (usrsctp_sendv(mSocket, &msg[0], len,
                    NULL, 0,
                    (void *)&spa, (socklen_t)sizeof(struct sctp_sendv_spa),
                    SCTP_SENDV_SPA, flags) < 0) {
    LOG(("error %d sending open\n",errno));
    /* no need to lock */
    mChannelsOut[stream].pending = false;
    delete mChannelsOut[stream].channel;
    mChannelsOut[stream].channel = NULL;
    return NULL;
  }

  return mChannelsOut[stream].channel;
}

void
DataChannelConnection::Close(PRUint16 stream)
{
  // Careful to handle Close() of stream in pending state
  if (stream == INVALID_STREAM)
    return; // paranoia

  MutexAutoLock lock(mLock);

  if (mChannelsOut[stream].channel)
    mChannelsOut[stream].channel->SetReadyState(DataChannel::CLOSED);
  mChannelsOut[stream].channel = NULL;

  if (mChannelsOut[stream].reverse != INVALID_STREAM) {
    // XXX tell sctp to reset the stream
    mChannelsOut[stream].reverse = INVALID_STREAM;
    mChannelsOut[stream].waiting_ack = false;
  } else if (mChannelsOut[stream].pending) {
    // mark to ignore OPEN_RESPONSE and then immediately close.
    // channel of NULL means we've closed the channel already.
  }
}

PRInt32
DataChannelConnection::SendMsgCommon(PRUint16 stream, const nsACString &aMsg, bool isBinary)
{
  NS_ABORT_IF_FALSE(NS_IsMainThread(), "not main thread");
  // We really could allow this from other threads, so long as we deal with
  // asynchronosity issues with channels closing, in particular access to
  // mChannelsOut, and issues with the association closing (access to mSocket).

  const char *data = aMsg.BeginReading();
  struct sctp_prinfo prinfo;
  struct sctp_sndinfo sndinfo;
  struct sctp_sendv_spa spa;
  PRUint32 len     = aMsg.Length();
  PRInt32 result;
  PRUint32 ppid = htonl((isBinary ? DATA_CHANNEL_PPID_BINARY : 
                         DATA_CHANNEL_PPID_DOMSTRING));
  uint16_t flags;

  if (isBinary)
    LOG(("Sending to stream %d: %d bytes",stream,len));
  else
    LOG(("Sending to stream %d: %s",stream,data));
  // XXX if we want more efficiency, translate flags once at open time
  flags = (mChannelsOut[stream].flags & DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED) ? SCTP_UNORDERED : 0;

  // To avoid problems where an in-order OPEN_RESPONSE is lost and an
  // out-of-order data message "beats" it, require data to be in-order
  // until we get an ACK.
  if (mChannelsOut[stream].waiting_ack || mChannelsOut[stream].pending)
    flags &= ~SCTP_UNORDERED;
  sndinfo.snd_ppid = ppid;
  sndinfo.snd_sid = stream;
  sndinfo.snd_flags = flags;
  sndinfo.snd_context = 0;
  sndinfo.snd_assoc_id = 0;

  prinfo.pr_policy = SCTP_PR_SCTP_TTL;
  prinfo.pr_value = mChannelsOut[stream].reliability_params;

  spa.sendv_sndinfo = sndinfo;
  spa.sendv_prinfo = prinfo;
  spa.sendv_flags = SCTP_SEND_SNDINFO_VALID | SCTP_SEND_PRINFO_VALID;
  LOG(("Sending open for stream %d\n",stream));

  if ((result = usrsctp_sendv(mSocket, data, len,
                              NULL, 0, 
                              (void *)&spa, (socklen_t)sizeof(struct sctp_sendv_spa),
                              SCTP_SENDV_SPA, flags) < 0)) {
    LOG(("error %d sending string\n",errno));
  }
  return result;
}

}
