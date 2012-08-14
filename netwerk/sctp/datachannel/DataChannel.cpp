/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <iostream>
#if !defined(__Userspace_os_Windows)
#include <arpa/inet.h>
#endif

// Hack fix for define issue in sctp lib
  //#define __USER_CODE 1
#define SCTP_DEBUG 1
#include "usrsctp.h"

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

// NS_ENSURE_TRUE for void functions
#define DC_ENSURE_TRUE(x)                                     \
  PR_BEGIN_MACRO                                              \
    if (NS_UNLIKELY(!(x))) {                                  \
       NS_WARNING("NS_ENSURE_TRUE(" #x ") failed");           \
       return;                                                \
    }                                                         \
  PR_END_MACRO

static bool sctp_initialized;

namespace mozilla {

static int
receive_cb(struct socket* sock, union sctp_sockstore addr, 
           void *data, size_t datalen, 
           struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{
  DataChannelConnection *connection = static_cast<DataChannelConnection*>(ulp_info);	
  return connection->ReceiveCallback(sock, data, datalen, rcv, flags);
}


DataChannelConnection::DataChannelConnection(DataConnectionListener *listener) :
   mLock("netwerk::sctp::DataChannel")
{
  mState = CLOSED;
  mSocket = NULL;
  mMasterSocket = NULL;
  mListener = listener;
  mNumChannels = 0;

  LOG(("Constructor DataChannelConnection=%p, listener=%p", this, mListener));

  mStreamsOut.AppendElements(mStreamsOut.Capacity());
  mStreamsIn.AppendElements(mStreamsOut.Capacity()); // make sure both are the same length
  for (PRUint32 i = 0; i < mStreamsOut.Capacity(); i++) {
    mStreamsOut[i] = nsnull;
    mStreamsIn[i]  = nsnull;
  }

  LOG(("DataChannelConnection created"));
}

DataChannelConnection::~DataChannelConnection()
{
  CloseAll();
}

bool
DataChannelConnection::Init(unsigned short port/* XXX DTLSConnection &tunnel*/)
{
  struct sctp_udpencaps encaps;
  struct sctp_assoc_value av;
  struct sctp_event event;
  
  PRUint16 event_types[] = {SCTP_ASSOC_CHANGE,
                            SCTP_PEER_ADDR_CHANGE,
                            SCTP_REMOTE_ERROR,
                            SCTP_SHUTDOWN_EVENT,
                            SCTP_ADAPTATION_INDICATION,
                            SCTP_SEND_FAILED_EVENT,
                            SCTP_STREAM_RESET_EVENT,
                            SCTP_STREAM_CHANGE_EVENT};
  {
    MutexAutoLock lock(mLock);
    if (!sctp_initialized) {
      LOG(("sctp_init(%d)",port+1));

#if 0
      // This needs to be tied to some form object that is guaranteed to be
      // around (singleton likely) unless we want to shutdown sctp whenever
      // we're not using it (and in which case we'd keep a refcnt'd object
      // ref'd by each DataChannelConnection to release the SCTP usrlib via
      // sctp_finish)
      mObserverService = mozilla::services::GetObserverService();
      NS_ENSURE_TRUE(mObserverService,false);
      mObserverService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, true);
#endif
      usrsctp_init(port,NULL); // XXX fix

      usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
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
    usrsctp_close(mMasterSocket);
    return false;
  }
  LOG(("SCTP encapsulation remote port %d, local port %d",port^1,port));

  av.assoc_id = SCTP_ALL_ASSOC;
  av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
  if (usrsctp_setsockopt(mMasterSocket, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &av,
                         (socklen_t)sizeof(struct sctp_assoc_value)) < 0) {
    LOG(("*** failed enable stream reset"));
    usrsctp_close(mMasterSocket);
    return false;
  }
  
  /* Enable the events of interest. */
  memset(&event, 0, sizeof(event));
  event.se_assoc_id = SCTP_ALL_ASSOC;
  event.se_on = 1;
  for (PRUint32 i = 0; i < sizeof(event_types)/sizeof(event_types[0]); i++) {
    event.se_type = event_types[i];
    if (usrsctp_setsockopt(mMasterSocket, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event)) < 0) {
      LOG(("*** failed setsockopt SCTP_EVENT"));
      usrsctp_close(mMasterSocket);
    }
  }

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
  LOG(("Waiting for connections on port %d",ntohs(addr.sin_port)));
  mState = CONNECTING;
  if (usrsctp_bind(mMasterSocket, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
    LOG(("***Failed userspace_bind"));
    return false;
  }
  if (usrsctp_listen(mMasterSocket, 1) < 0) {
    LOG(("***Failed userspace_listen"));
    return false;
  }

  LOG(("Accepting connection"));
  addr_len = 0;
  if ((mSocket = usrsctp_accept(mMasterSocket, NULL, &addr_len)) == NULL) {
    LOG(("***Failed accept"));
    return false;
  }
  mState = OPEN;

  LOG(("Accepting incoming connection.  Entering connected mode. mSocket=%p, masterSocket=%p", mSocket, mMasterSocket));

  // Notify Connection open
  // XXX We need to make sure connection sticks around until the message is delivered
  LOG(("%s: sending ON_CONNECTION for %p",__FUNCTION__,this));
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
  LOG(("Connecting to %s, port %u",addr, port));
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
    LOG(("*** Illegal destination address."));
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
      LOG(("*** Illegal destination address."));
    }
  }
#endif

  mSocket = mMasterSocket;  // XXX Be careful!  

  LOG(("connect() succeeded!  Entering connected mode"));
  mState = OPEN;
  
  // Notify Connection open
  // XXX We need to make sure connection sticks around until the message is delivered
  LOG(("%s: sending ON_CONNECTION for %p",__FUNCTION__,this));
  NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                            DataChannelOnMessageAvailable::ON_CONNECTION,
                            this, NULL));
  mNumChannels = 0;
  return true;
}

DataChannel *
DataChannelConnection::FindChannelByStreamIn(PRUint16 streamIn)
{
  return mStreamsIn.SafeElementAt(streamIn);
}

DataChannel *
DataChannelConnection::FindChannelByStreamOut(PRUint16 streamOut)
{
  return mStreamsOut.SafeElementAt(streamOut);
}

PRUint16
DataChannelConnection::FindFreeStreamOut()
{
  PRUint32 i, limit;

  limit = mStreamsOut.Length();
  if (limit > MAX_NUM_STREAMS)
    limit = MAX_NUM_STREAMS;
  for (i = 0; i < limit; i++) {
    if (mStreamsOut[i] == NULL) {
      break;
    }
  }
  if (i == limit) {
    return INVALID_STREAM;
  }
  return i;
}

bool
DataChannelConnection::RequestMoreStreamsOut()
{
  struct sctp_status status;
  struct sctp_add_streams sas;
  PRUint32 outStreamsNeeded;
  socklen_t len;

  len = (socklen_t)sizeof(struct sctp_status);
  if (usrsctp_getsockopt(mMasterSocket, IPPROTO_SCTP, SCTP_STATUS, &status, &len) < 0) {
    LOG(("***failed: getsockopt SCTP_STATUS"));
    return false;
  }
  outStreamsNeeded = status.sstat_outstrms + 16;
  if (outStreamsNeeded > 0x10000) {
    LOG(("***failed: too many streams: %x",outStreamsNeeded));
    return false;
  }

  memset(&sas, 0, sizeof(struct sctp_add_streams));
  sas.sas_instrms = 0;
  sas.sas_outstrms = (uint16_t)outStreamsNeeded; /* XXX error handling */
  // XXX any chance this blocks?
  if (usrsctp_setsockopt(mMasterSocket, IPPROTO_SCTP, SCTP_ADD_STREAMS, &sas,
                         (socklen_t) sizeof(struct sctp_add_streams)) < 0) {
    LOG(("***failed: setsockopt"));
    return false;
  }
  return true;
}

PRInt32
DataChannelConnection::SendControlMessage(void *msg, PRUint32 len, PRUint16 streamOut)
{
  struct sctp_sndinfo sndinfo;

  memset(&sndinfo, 0, sizeof(struct sctp_sndinfo));
  sndinfo.snd_sid = streamOut;
  sndinfo.snd_ppid = htonl(DATA_CHANNEL_PPID_CONTROL);
  if (usrsctp_sendv(mSocket, msg, len, NULL, 0,
                    &sndinfo, (socklen_t)sizeof(struct sctp_sndinfo),
                    SCTP_SENDV_SNDINFO, 0) < 0) {
    //LOG(("***failed: sctp_sendv")); don't log because errno is a return!
    return (0);
  } else {
    return (1);
  }
  return (1);
}

PRInt32
DataChannelConnection::SendOpenResponseMessage(PRUint16 streamOut, PRUint16 streamIn)
{
  /* XXX: This should be encoded in a better way */
  struct rtcweb_datachannel_open_response rsp;

  memset(&rsp, 0, sizeof(struct rtcweb_datachannel_open_response));
  rsp.msg_type = DATA_CHANNEL_OPEN_RESPONSE;
  rsp.reverse_stream = htons(streamIn);

  return SendControlMessage(&rsp, sizeof(rsp), streamOut);
}


PRInt32
DataChannelConnection::SendOpenAckMessage(PRUint16 streamOut)
{
  /* XXX: This should be encoded in a better way */
  struct rtcweb_datachannel_ack ack;

  memset(&ack, 0, sizeof(struct rtcweb_datachannel_ack));
  ack.msg_type = DATA_CHANNEL_ACK;

  return SendControlMessage(&ack, sizeof(ack), streamOut);
}

#if 0
PRInt32
DataChannelConnection::SendErrorResponse(struct sctp_rcvinfo rcv,
                                         uint8_t error)
{
  struct rtcweb_datachannel_open_response response;

  response.msg_type = DATA_CHANNEL_OPEN_RESPONSE;
  response.reverse_stream = rcv.rcv_sid;
  response.error = error;

  /*
    send the error back on the incoming stream id - we can report errors on any stream
    as reverse_stream tells the other side what request got an error.
  */
  return SendControlMessage(&response, sizeof(response), rcv.rcv_sid);
}
#endif

PRInt32
DataChannelConnection::SendOpenRequestMessage(PRUint16 streamOut, bool unordered,
                                              PRUint16 prPolicy, PRUint32 prValue)
{
  /* XXX: This should be encoded in a better way */
  struct rtcweb_datachannel_open_request req;

  memset(&req, 0, sizeof(struct rtcweb_datachannel_open_request));
  req.msg_type = DATA_CHANNEL_OPEN_REQUEST;
  switch (prPolicy) {
  case SCTP_PR_SCTP_NONE:
    /* XXX: What about DATA_CHANNEL_RELIABLE_STREAM */
    req.channel_type = DATA_CHANNEL_RELIABLE;
    break;
  case SCTP_PR_SCTP_TTL:
    /* XXX: What about DATA_CHANNEL_UNRELIABLE */
    req.channel_type = DATA_CHANNEL_PARTIAL_RELIABLE_TIMED;
    break;
  case SCTP_PR_SCTP_RTX:
    req.channel_type = DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT;
    break;
  default:
    // FIX! need to set errno!  Or make all these SendXxxx() funcs return 0 or errno!
    return (0);
  }
  req.flags = htons(0);
  if (unordered) {
    req.flags |= htons(DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED);
  }
  req.reliability_params = htons((uint16_t)prValue); /* XXX Why 16-bit */
  req.priority = htons(0); /* XXX: add support */

  return SendControlMessage(&req, sizeof(req), streamOut);
}

void
DataChannelConnection::SendDeferredMessages()
{
  PRUint32 i;
  DataChannel *channel;

  for (i = 0; i < mStreamsOut.Length(); i++) {
    channel = mStreamsOut[i];
    if (!channel)
      continue;

    // Only one of these should be set....
    if (channel->mFlags & DATA_CHANNEL_FLAGS_SEND_REQ) {
      if (SendOpenRequestMessage(channel->mStreamOut, 
                                 channel->mFlags & DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED,
                                 channel->mPrPolicy, channel->mPrValue)) {
        channel->mFlags &= ~DATA_CHANNEL_FLAGS_SEND_REQ;
      } else {
        if (errno != EAGAIN) {
          /* XXX: error handling */
          mStreamsOut[channel->mStreamOut] = NULL;
          channel->mState = CLOSED;
          // XXX FIX  Close the channel, inform the user?
        }
      }
    }
    if (channel->mFlags & DATA_CHANNEL_FLAGS_SEND_RSP) {
      if (SendOpenResponseMessage(channel->mStreamOut, channel->mStreamIn)) {
        channel->mFlags &= ~DATA_CHANNEL_FLAGS_SEND_RSP;
      } else {
        if (errno != EAGAIN) {
          mStreamsIn[channel->mStreamIn]   = NULL;
          mStreamsOut[channel->mStreamOut] = NULL;
          delete channel;
        }
      }
    }
    if (channel->mFlags & DATA_CHANNEL_FLAGS_SEND_ACK) {
      if (SendOpenAckMessage(channel->mStreamOut)) {
        channel->mFlags &= ~DATA_CHANNEL_FLAGS_SEND_ACK;
      } else {
        if (errno != EAGAIN) {
          /* XXX: error handling */
          // Send error?  Close channel?
          mStreamsIn[channel->mStreamIn]   = NULL;
          mStreamsOut[channel->mStreamOut] = NULL;
          channel->mState = CLOSED;
          // XXX FIX  Close the channel, inform the user?
        }
      }
    }
  }
  return;
}

void
DataChannelConnection::HandleOpenRequestMessage(const struct rtcweb_datachannel_open_request *req,
                                                size_t length,
                                                PRUint16 streamIn)
{
  DataChannel *channel;
  PRUint32 prValue;
  PRUint16 prPolicy;
  PRUint16 streamOut;
  PRUint32 flags;

  if ((channel = FindChannelByStreamIn(streamIn))) {
    LOG(("ERROR: HandleOpenRequestMessage: channel for stream %d is in state %d instead of CLOSED.",
         streamIn, channel->mState));
    /* XXX: some error handling */
    return;
  }
  switch (req->channel_type) {
    case DATA_CHANNEL_RELIABLE:
      prPolicy = SCTP_PR_SCTP_NONE;
      break;
#if 0
    case DATA_CHANNEL_RELIABLE_STREAM:
      prPolicy = SCTP_PR_SCTP_NONE;
      break;
#endif
    case DATA_CHANNEL_UNRELIABLE:
      prPolicy = SCTP_PR_SCTP_TTL;
      NS_ASSERTION(ntohs(req->reliability_params),"UNRELIABLE DataChannels must have 0 TTL!");
      break;
    case DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT:
      prPolicy = SCTP_PR_SCTP_RTX;
      break;
    case DATA_CHANNEL_PARTIAL_RELIABLE_TIMED:
      prPolicy = SCTP_PR_SCTP_TTL;
      break;
    default:
      /* XXX error handling */
      break;
  }
  prValue = ntohs(req->reliability_params);
  flags = ntohs(req->flags) & DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED;
  streamOut = FindFreeStreamOut(); // may be INVALID_STREAM!

  channel = new DataChannel(this, streamOut, streamIn,
                            DataChannel::CONNECTING,
                            prPolicy, prValue,
                            flags,
                            NULL, NULL);
  mStreamsIn[streamIn] = channel;
  if (streamOut == INVALID_STREAM) {
    RequestMoreStreamsOut();
  } else {
    mStreamsOut[streamOut] = channel;
    if (SendOpenResponseMessage(streamOut, streamIn)) {
      mNumChannels++;
      LOG(("successful open of in: %u, out: %u, total channels %d\n",
           streamIn, streamOut, mNumChannels));

      /* Notify ondatachannel */
      // XXX We need to make sure connection sticks around until the message is delivered
      LOG(("%s: sending ON_CHANNEL_CREATED for %p",__FUNCTION__,channel));
      NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                                DataChannelOnMessageAvailable::ON_CHANNEL_CREATED,
                                this, channel));
    } else {
      if (errno == EAGAIN) {
        channel->mFlags |= DATA_CHANNEL_FLAGS_SEND_RSP;
      } else {
        /* XXX: Signal error to the other end. */
        mStreamsIn[streamIn] = NULL;
        mStreamsOut[streamOut] = NULL;
        delete channel;
      }
    }
  }
}

void
DataChannelConnection::HandleOpenResponseMessage(const struct rtcweb_datachannel_open_response *rsp,
                                                 size_t length, PRUint16 streamIn)
{
  PRUint16 streamOut;
  DataChannel *channel;

  streamOut = ntohs(rsp->reverse_stream);
  channel = FindChannelByStreamOut(streamOut);

  DC_ENSURE_TRUE(channel != NULL);
  DC_ENSURE_TRUE(channel->mState == CONNECTING);
  DC_ENSURE_TRUE(!FindChannelByStreamIn(streamIn));

  channel->mStreamIn = streamIn;
  channel->mState = OPEN;
  mStreamsIn[streamIn] = channel;
  if (SendOpenAckMessage(streamOut)) {
    channel->mFlags = 0;
  } else {
    // XXX Only on EAGAIN!?  And if not, then close the channel??
    channel->mFlags |= DATA_CHANNEL_FLAGS_SEND_ACK;
  }
  LOG(("%s: sending ON_CHANNEL_OPEN for %p",__FUNCTION__,channel));
  NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                          DataChannelOnMessageAvailable::ON_CHANNEL_OPEN, this,
                          channel));
  return;
}

void
DataChannelConnection::HandleOpenAckMessage(const struct rtcweb_datachannel_ack *ack,
                                            size_t length, PRUint16 streamIn)
{
  DataChannel *channel;

  channel = FindChannelByStreamIn(streamIn);

  DC_ENSURE_TRUE(channel != NULL);
  DC_ENSURE_TRUE(channel->mState == CONNECTING);

  channel->mState = OPEN;
  LOG(("%s: sending ON_CHANNEL_OPEN for %p",__FUNCTION__,channel));
  NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                          DataChannelOnMessageAvailable::ON_CHANNEL_OPEN, this,
                          channel));
  return;
}

void
DataChannelConnection::HandleUnknownMessage(PRUint32 ppid, size_t length, PRUint16 streamIn)
{
  /* XXX: Send an error message? */
  LOG(("unknown DataChannel message received: %u, len %d on stream %u",ppid,length,streamIn));
  NS_WARNING("unknown DataChannel message received");
  return;
}

void
DataChannelConnection::HandleDataMessage(PRUint32 ppid, 
                                         const char *buffer, size_t length,
                                         PRUint16 streamIn)
{
  DataChannel *channel;

  channel = FindChannelByStreamIn(streamIn);

  // XXX A closed channel may trip this... check
  DC_ENSURE_TRUE(channel != NULL);
  if (channel->mState == CONNECTING) {
    /* Implicit ACK */
    channel->mState = OPEN;
  }
  // XXX should this be a simple if, no warnings/debugbreaks?
  DC_ENSURE_TRUE(channel->mState != CLOSED);

  {
    nsCString recvData(buffer, length);

    switch (ppid) {
      case DATA_CHANNEL_PPID_DOMSTRING:
        LOG(("DataChannel: String message received of length %lu on channel %d: %.*s",
             length, channel->mStreamOut, (int)PR_MIN(length,80), buffer));
        length = -1; // Flag for DOMString

        // paranoia (WebSockets does this)
        if (!IsUTF8(recvData, false)) {
          NS_ERROR("DataChannel:: text message invalid utf-8");
          return;
        }
        break;
      case DATA_CHANNEL_PPID_BINARY:
        LOG(("DataChannel: Received binary message of length %lu on channel id %d",
             length, channel->mStreamOut));
        break;
      default:
        NS_ERROR("Unknown data PPID");
        return;
    }
    /* Notify onmessage */
    LOG(("%s: sending ON_DATA for %p",__FUNCTION__,channel));
    NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                              DataChannelOnMessageAvailable::ON_DATA, this,
                              channel, recvData, length));
  }
  return;
}

void
DataChannelConnection::HandleMessage(char *buffer, size_t length, PRUint32 ppid, PRUint16 streamIn)
{
  struct rtcweb_datachannel_open_request *req;
  struct rtcweb_datachannel_open_response *rsp;
  struct rtcweb_datachannel_ack *ack, *msg;

  switch (ppid) {
    case DATA_CHANNEL_PPID_CONTROL:
      DC_ENSURE_TRUE(length >= sizeof(*ack));

      msg = (struct rtcweb_datachannel_ack *)buffer;
      switch (msg->msg_type) {
        case DATA_CHANNEL_OPEN_REQUEST:
          DC_ENSURE_TRUE(length >= sizeof(*req));

          req = (struct rtcweb_datachannel_open_request *)buffer;
          HandleOpenRequestMessage(req, length, streamIn);
          break;
        case DATA_CHANNEL_OPEN_RESPONSE:
          DC_ENSURE_TRUE(length >= sizeof(*rsp));

          rsp = (struct rtcweb_datachannel_open_response *)buffer;
          HandleOpenResponseMessage(rsp, length, streamIn);
          break;
        case DATA_CHANNEL_ACK:
          // DC_ENSURE_TRUE(length >= sizeof(*ack)); checked above

          ack = (struct rtcweb_datachannel_ack *)buffer;
          HandleOpenAckMessage(ack, length, streamIn);
          break;
        default:
          HandleUnknownMessage(ppid, length, streamIn);
          break;
      }
      break;
    case DATA_CHANNEL_PPID_DOMSTRING:
    case DATA_CHANNEL_PPID_BINARY:
      HandleDataMessage(ppid, buffer, length, streamIn);
      break;
    default:
      LOG(("Message of length %lu, PPID %u on stream %u received.",
           length, ppid, streamIn));
      break;
  }
}

void
DataChannelConnection::HandleAssociationChangeEvent(const struct sctp_assoc_change *sac)
{
  PRUint32 i, n;

  switch (sac->sac_state) {
  case SCTP_COMM_UP:
    LOG(("Association change: SCTP_COMM_UP"));
    break;
  case SCTP_COMM_LOST:
    LOG(("Association change: SCTP_COMM_LOST"));
    break;
  case SCTP_RESTART:
    LOG(("Association change: SCTP_RESTART"));
    break;
  case SCTP_SHUTDOWN_COMP:
    LOG(("Association change: SCTP_SHUTDOWN_COMP"));
    break;
  case SCTP_CANT_STR_ASSOC:
    LOG(("Association change: SCTP_CANT_STR_ASSOC"));
    break;
  default:
    LOG(("Association change: UNKNOWN"));
    break;
  }
  LOG(("Association change: streams (in/out) = (%u/%u)",
       sac->sac_inbound_streams, sac->sac_outbound_streams));
  n = sac->sac_length - sizeof(*sac);
  if (((sac->sac_state == SCTP_COMM_UP) ||
        (sac->sac_state == SCTP_RESTART)) && (n > 0)) {
    for (i = 0; i < n; i++) {
      switch (sac->sac_info[i]) {
      case SCTP_ASSOC_SUPPORTS_PR:
        LOG(("Supports: PR"));
        break;
      case SCTP_ASSOC_SUPPORTS_AUTH:
        LOG(("Supports: AUTH"));
        break;
      case SCTP_ASSOC_SUPPORTS_ASCONF:
        LOG(("Supports: ASCONF"));
        break;
      case SCTP_ASSOC_SUPPORTS_MULTIBUF:
        LOG(("Supports: MULTIBUF"));
        break;
      case SCTP_ASSOC_SUPPORTS_RE_CONFIG:
        LOG(("Supports: RE-CONFIG"));
        break;
      default:
        LOG(("Supports: UNKNOWN(0x%02x)", sac->sac_info[i]));
        break;
      }
    }
  } else if (((sac->sac_state == SCTP_COMM_LOST) ||
              (sac->sac_state == SCTP_CANT_STR_ASSOC)) && (n > 0)) {
    LOG(("Association: ABORT ="));
    for (i = 0; i < n; i++) {
      LOG((" 0x%02x", sac->sac_info[i]));
    }
  }
  if ((sac->sac_state == SCTP_CANT_STR_ASSOC) ||
      (sac->sac_state == SCTP_SHUTDOWN_COMP) ||
      (sac->sac_state == SCTP_COMM_LOST)) {
    return;
  }
  return;
}

void
DataChannelConnection::HandlePeerAddressChangeEvent(const struct sctp_paddr_change *spc)
{
  char addr_buf[INET6_ADDRSTRLEN];
  const char *addr;
  struct sockaddr_in *sin;
  struct sockaddr_in6 *sin6;

  switch (spc->spc_aaddr.ss_family) {
  case AF_INET:
    sin = (struct sockaddr_in *)&spc->spc_aaddr;
    addr = inet_ntop(AF_INET, &sin->sin_addr, addr_buf, INET6_ADDRSTRLEN);
    break;
  case AF_INET6:
    sin6 = (struct sockaddr_in6 *)&spc->spc_aaddr;
    addr = inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf, INET6_ADDRSTRLEN);
    break;
  default:
    break;
  }
  LOG(("Peer address %s is now ", addr));
  switch (spc->spc_state) {
  case SCTP_ADDR_AVAILABLE:
    LOG(("SCTP_ADDR_AVAILABLE"));
    break;
  case SCTP_ADDR_UNREACHABLE:
    LOG(("SCTP_ADDR_UNREACHABLE"));
    break;
  case SCTP_ADDR_REMOVED:
    LOG(("SCTP_ADDR_REMOVED"));
    break;
  case SCTP_ADDR_ADDED:
    LOG(("SCTP_ADDR_ADDED"));
    break;
  case SCTP_ADDR_MADE_PRIM:
    LOG(("SCTP_ADDR_MADE_PRIM"));
    break;
  case SCTP_ADDR_CONFIRMED:
    LOG(("SCTP_ADDR_CONFIRMED"));
    break;
  default:
    LOG(("UNKNOWN"));
    break;
  }
  LOG((" (error = 0x%08x).\n", spc->spc_error));
  return;
}

void
DataChannelConnection::HandleRemoteErrorEvent(const struct sctp_remote_error *sre)
{
  size_t i, n;

  n = sre->sre_length - sizeof(struct sctp_remote_error);
  LOG(("Remote Error (error = 0x%04x): ", sre->sre_error));
  for (i = 0; i < n; i++) {
    LOG((" 0x%02x", sre-> sre_data[i]));
  }
  return;
}

void
DataChannelConnection::HandleShutdownEvent(const struct sctp_shutdown_event *sse)
{
  LOG(("Shutdown event."));
  /* XXX: notify all channels. */
  return;
}

void
DataChannelConnection::HandleAdaptationIndication(const struct sctp_adaptation_event *sai)
{
  LOG(("Adaptation indication: %x.", sai-> sai_adaptation_ind));
  return;
}

void
DataChannelConnection::HandleSendFailedEvent(const struct sctp_send_failed_event *ssfe)
{
  size_t i, n;

  if (ssfe->ssfe_flags & SCTP_DATA_UNSENT) {
    LOG(("Unsent "));
  }
   if (ssfe->ssfe_flags & SCTP_DATA_SENT) {
    LOG(("Sent "));
  }
  if (ssfe->ssfe_flags & ~(SCTP_DATA_SENT | SCTP_DATA_UNSENT)) {
    LOG(("(flags = %x) ", ssfe->ssfe_flags));
  }
  LOG(("message with PPID = %d, SID = %d, flags: 0x%04x due to error = 0x%08x",
       ntohl(ssfe->ssfe_info.snd_ppid), ssfe->ssfe_info.snd_sid,
       ssfe->ssfe_info.snd_flags, ssfe->ssfe_error));
  n = ssfe->ssfe_length - sizeof(struct sctp_send_failed_event);
  for (i = 0; i < n; i++) {
    LOG((" 0x%02x", ssfe->ssfe_data[i]));
  }
  return;
}

void
DataChannelConnection::ResetOutgoingStream(PRUint16 streamOut)
{
  PRUint32 i;

  // Rarely has more than a couple items and only for a short time
  for (i = 0; i < mStreamsResetting.Length(); i++) {
    if (mStreamsResetting[i] == streamOut) {
      return;
    }
  }
  mStreamsResetting.AppendElement(streamOut);
  return;
}

void
DataChannelConnection::SendOutgoingStreamReset()
{
  struct sctp_reset_streams *srs;
  PRUint32 i;
  size_t len;

  if (mStreamsResetting.IsEmpty() == 0) {
    return;
  }
  len = sizeof(sctp_assoc_t) + (2 + mStreamsResetting.Length()) * sizeof(uint16_t);
  srs = (struct sctp_reset_streams *) malloc(len);
  if (!srs) { // XXX remove, infallible malloc
    return;
  }
  memset(srs, 0, len);
  srs->srs_flags = SCTP_STREAM_RESET_OUTGOING;
  srs->srs_number_streams = mStreamsResetting.Length();
  for (i = 0; i < mStreamsResetting.Length(); i++) {
    srs->srs_stream_list[i] = mStreamsResetting[i];
  }
  if (usrsctp_setsockopt(mMasterSocket, IPPROTO_SCTP, SCTP_RESET_STREAMS, srs, (socklen_t)len) < 0) {
    LOG(("***failed: setsockopt"));
  } else {
    mStreamsResetting.Clear();
  }
  free(srs);
  return;
}

void
DataChannelConnection::HandleStreamResetEvent(const struct sctp_stream_reset_event *strrst)
{
  PRUint32 n, i;
  DataChannel *channel;

  if (!(strrst->strreset_flags & SCTP_STREAM_RESET_DENIED) &&
      !(strrst->strreset_flags & SCTP_STREAM_RESET_FAILED)) {
    n = (strrst->strreset_length - sizeof(struct sctp_stream_reset_event)) / sizeof(uint16_t);
    for (i = 0; i < n; i++) {
      if (strrst->strreset_flags & SCTP_STREAM_RESET_INCOMING_SSN) {
        channel = FindChannelByStreamIn(strrst->strreset_stream_list[i]);
        if (channel != NULL) {
          mStreamsIn[channel->mStreamIn] = NULL;
          channel->mStreamIn = INVALID_STREAM;
          if (channel->mStreamOut == INVALID_STREAM) {
            channel->mPrPolicy = SCTP_PR_SCTP_NONE;
            channel->mPrValue = 0;
            channel->mFlags = 0;
            channel->mState = CLOSED;
          } else {
            ResetOutgoingStream(channel->mStreamOut);
            channel->mState = CLOSING;
          }
        }
      }
      if (strrst->strreset_flags & SCTP_STREAM_RESET_OUTGOING_SSN) {
        channel = FindChannelByStreamOut(strrst->strreset_stream_list[i]);
        if (channel != NULL && channel->mStreamOut != INVALID_STREAM) {
          mStreamsOut[channel->mStreamOut] = NULL;
          channel->mStreamOut = INVALID_STREAM;
          if (channel->mStreamIn == INVALID_STREAM) {
            channel->mPrPolicy = SCTP_PR_SCTP_NONE;
            channel->mPrValue = 0;
            channel->mFlags = 0;
            channel->mState = CLOSED;
          }
        }
      }
    }
  }
  return;
}

void
DataChannelConnection::HandleStreamChangeEvent(const struct sctp_stream_change_event *strchg)
{
  PRUint16 streamOut;
  PRUint32 i;
  DataChannel *channel;

  for (i = 0; i < mStreamsOut.Length(); i++) {
    channel = mStreamsOut[i];
    if (!channel)
      continue;

    if ((channel->mState == CONNECTING) &&
        (channel->mStreamOut == INVALID_STREAM)) {
      if ((strchg->strchange_flags & SCTP_STREAM_CHANGE_DENIED) ||
          (strchg->strchange_flags & SCTP_STREAM_CHANGE_FAILED)) {
        /* XXX: Signal to the other end. */
        if (channel->mStreamIn != INVALID_STREAM) {
          mStreamsIn[channel->mStreamIn] = NULL;
        }
        channel->mState = CLOSED;
        // inform user!
        // XXX delete channel;
      } else {
        streamOut = FindFreeStreamOut();
        if (streamOut != INVALID_STREAM) {
          channel->mStreamOut = streamOut;
          mStreamsOut[streamOut] = channel;
          if (channel->mStreamIn == INVALID_STREAM) {
            channel->mFlags |= DATA_CHANNEL_FLAGS_SEND_REQ;
          } else {
            channel->mFlags |= DATA_CHANNEL_FLAGS_SEND_RSP;
          }
        } else {
          /* We will not find more ... */
          break;
        }
      }
    }
  }
  return;
}


void
DataChannelConnection::HandleNotification(const union sctp_notification *notif, size_t n)
{
  if (notif->sn_header.sn_length != (uint32_t)n) {
    return;
  }
  switch (notif->sn_header.sn_type) {
  case SCTP_ASSOC_CHANGE:
    HandleAssociationChangeEvent(&(notif->sn_assoc_change));
    break;
  case SCTP_PEER_ADDR_CHANGE:
    HandlePeerAddressChangeEvent(&(notif->sn_paddr_change));
    break;
  case SCTP_REMOTE_ERROR:
    HandleRemoteErrorEvent(&(notif->sn_remote_error));
    break;
  case SCTP_SHUTDOWN_EVENT:
    HandleShutdownEvent(&(notif->sn_shutdown_event));
    break;
  case SCTP_ADAPTATION_INDICATION:
    HandleAdaptationIndication(&(notif->sn_adaptation_event));
    break;
  case SCTP_PARTIAL_DELIVERY_EVENT:
    break;
  case SCTP_AUTHENTICATION_EVENT:
    break;
  case SCTP_SENDER_DRY_EVENT:
    break;
  case SCTP_NOTIFICATIONS_STOPPED_EVENT:
    break;
  case SCTP_SEND_FAILED_EVENT:
    HandleSendFailedEvent(&(notif->sn_send_failed_event));
    break;
  case SCTP_STREAM_RESET_EVENT:
    HandleStreamResetEvent(&(notif->sn_strreset_event));
    SendDeferredMessages();
    SendOutgoingStreamReset();
    RequestMoreStreamsOut();
    break;
  case SCTP_ASSOC_RESET_EVENT:
    break;
  case SCTP_STREAM_CHANGE_EVENT:
    HandleStreamChangeEvent(&(notif->sn_strchange_event));
    SendDeferredMessages();
    SendOutgoingStreamReset();
    RequestMoreStreamsOut();
    break;
  default:
    break;
   }
 }


// NOTE: not called on main thread
int
DataChannelConnection::ReceiveCallback(struct socket* sock, void *data, size_t datalen,
                                       struct sctp_rcvinfo rcv, PRInt32 flags)
{
  if (data == NULL) {
    usrsctp_close(sock);
  } else {
    MutexAutoLock lock(mLock);
    if (flags & MSG_NOTIFICATION) {
      HandleNotification((union sctp_notification *)data, datalen);
    } else {
      HandleMessage((char *)data, datalen, ntohl(rcv.rcv_ppid), rcv.rcv_sid);
    }
  }
  return 1;
}

DataChannel *
DataChannelConnection::Open(/*const std::wstring& label,*/ Type type, bool inOrder, 
                            PRUint32 prValue, DataChannelListener *aListener,
                            nsISupports *aContext)
{
  DataChannel *channel;
  PRUint16 streamOut, prPolicy;
  PRUint32 flags;

  LOG(("DC Open: type %u, inorder %d, prValue %u, listener %p, context %p",
       type, inOrder, prValue, aListener, aContext));
  MutexAutoLock lock(mLock);
  switch (type) {
    case DATA_CHANNEL_RELIABLE:
      prPolicy = SCTP_PR_SCTP_NONE;
      break;
    case DATA_CHANNEL_RELIABLE_STREAM:
      LOG(("Type RELIABLE_STREAM not supported yet"));
      return NULL;
    case DATA_CHANNEL_UNRELIABLE:
      prPolicy = SCTP_PR_SCTP_TTL;
      prValue  = 0;
      // UNRELIABLE in-order is allowed
      break;
    case DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT:
      prPolicy = SCTP_PR_SCTP_RTX;
      break;
    case DATA_CHANNEL_PARTIAL_RELIABLE_TIMED:
      prPolicy = SCTP_PR_SCTP_TTL;
      break;
  }
  if ((prPolicy == SCTP_PR_SCTP_NONE) && (prValue != 0)) {
    return (NULL);
  }

  flags = !inOrder ? DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED : 0;
  streamOut = FindFreeStreamOut(); // may be INVALID_STREAM!
  channel = new DataChannel(this, streamOut, INVALID_STREAM,
                            DataChannel::CONNECTING,
                            type, prValue,
                            flags,
                            aListener, aContext);
  if (!channel) { // XXX remove - infallible malloc
    return (NULL);
  }

  if (streamOut == INVALID_STREAM) {
    RequestMoreStreamsOut();
  } else {
    mStreamsOut[streamOut] = channel;
    if (!SendOpenRequestMessage(streamOut, !inOrder, prPolicy, prValue)) {
      if (errno == EAGAIN) {
        channel->mFlags |= DATA_CHANNEL_FLAGS_SEND_REQ;
      } else {
        mStreamsOut[streamOut] = NULL;
        delete channel;
        return NULL;
      }
    }
  }
  return (channel);
}

void
DataChannelConnection::Close(PRUint16 streamOut)
{
  DataChannel *channel;

  MutexAutoLock lock(mLock);
  LOG(("Closing stream %d",streamOut));
  channel = FindChannelByStreamOut(streamOut);
  if (channel) {
    ResetOutgoingStream(channel->mStreamOut);
    SendOutgoingStreamReset();
    channel->mState = CLOSING;
   }
  return;
}

void DataChannelConnection::CloseAll()
{
  LOG(("Closing all channels"));
  // Don't need to lock here

  // Make sure no more channels will be opened
  mState = CLOSED;

  // Close current channels 
  // FIX! if there are runnables, they must use weakrefs or hold a strong
  // ref and keep the channel and/or connection alive
  for (PRUint32 i = 0; i < mStreamsOut.Length(); i++) {
    if (mStreamsOut[i]) {
      mStreamsOut[i]->Close();
    }
  }
}

PRInt32
DataChannelConnection::SendMsgCommon(PRUint16 stream, const nsACString &aMsg, bool isBinary)
{
  NS_ABORT_IF_FALSE(NS_IsMainThread(), "not main thread");
  // We really could allow this from other threads, so long as we deal with
  // asynchronosity issues with channels closing, in particular access to
  // mStreamsOut, and issues with the association closing (access to mSocket).

  const char *data = aMsg.BeginReading();
  struct sctp_prinfo prinfo;
  struct sctp_sndinfo sndinfo;
  struct sctp_sendv_spa spa;
  PRUint32 len     = aMsg.Length();
  PRInt32 result;
  PRUint32 ppid = htonl((isBinary ? DATA_CHANNEL_PPID_BINARY : 
                         DATA_CHANNEL_PPID_DOMSTRING));
  uint16_t flags;
  DataChannel *channel;

  if (isBinary)
    LOG(("Sending to stream %d: %d bytes",stream,len));
  else
    LOG(("Sending to stream %d: %s",stream,data));
  // XXX if we want more efficiency, translate flags once at open time
  channel = mStreamsOut[stream];
  if (!channel)
    return 0;

  flags = (channel->mFlags & DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED) ? SCTP_UNORDERED : 0;

  // To avoid problems where an in-order OPEN_RESPONSE is lost and an
  // out-of-order data message "beats" it, require data to be in-order
  // until we get an ACK.
  if (channel->mState == CONNECTING) {
    flags &= ~SCTP_UNORDERED;
  }
  sndinfo.snd_ppid = ppid;
  sndinfo.snd_sid = stream;
  sndinfo.snd_flags = flags;
  sndinfo.snd_context = 0;
  sndinfo.snd_assoc_id = 0;

  prinfo.pr_policy = SCTP_PR_SCTP_TTL;
  prinfo.pr_value = channel->mPrValue;

  spa.sendv_sndinfo = sndinfo;
  spa.sendv_prinfo = prinfo;
  spa.sendv_flags = SCTP_SEND_SNDINFO_VALID | SCTP_SEND_PRINFO_VALID;
  LOG(("Sending open for stream %d",stream));

  if ((result = usrsctp_sendv(mSocket, data, len,
                              NULL, 0, 
                              (void *)&spa, (socklen_t)sizeof(struct sctp_sendv_spa),
                              SCTP_SENDV_SPA, flags) < 0)) {
    LOG(("error %d sending string",errno));
  }
  return result;
}

}
