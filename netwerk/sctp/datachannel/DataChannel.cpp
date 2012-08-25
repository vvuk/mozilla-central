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
#define SCTP_STDINT_INCLUDE "mozilla/StandardInteger.h"
#include "usrsctp.h"

#if defined(__Userspace_os_Darwin)
// sctp undefines __APPLE__, so reenable them.
#define __APPLE__ 1
#endif
#include "DataChannelLog.h"
#if defined(__Userspace_os_Darwin)
#undef __APPLE__
#endif

#include "nsServiceManagerUtils.h"
#include "nsIObserverService.h"
#include "nsIObserver.h"
#include "mozilla/Services.h"
#include "nsThreadUtils.h"
#include "nsAutoPtr.h"
#include "nsNetUtil.h"
#include "mtransport/runnable_utils.h"
#include "DataChannel.h"
#include "DataChannelProtocol.h"

#ifdef PR_LOGGING
PRLogModuleInfo* dataChannelLog = PR_NewLogModule("DataChannel");
#endif

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

class DataChannelShutdown;
DataChannelShutdown *gDataChannelShutdown;

class DataChannelShutdown : public nsIObserver
{
public:
  // This needs to be tied to some form object that is guaranteed to be
  // around (singleton likely) unless we want to shutdown sctp whenever
  // we're not using it (and in which case we'd keep a refcnt'd object
  // ref'd by each DataChannelConnection to release the SCTP usrlib via
  // sctp_finish)

  NS_DECL_ISUPPORTS

  DataChannelShutdown() 
    { 
      nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
      if (!observerService)
        return;

      // XXX Verify this is true
      ++mRefCnt;    // Our refcnt must be > 0 when we call this, or we'll get deleted!
      nsresult rv = observerService->AddObserver(this,
                                                 "profile-change-net-teardown",
                                                 false);
      --mRefCnt;
      DC_ENSURE_TRUE(rv == NS_OK);
    }
  virtual ~DataChannelShutdown() 
    {
      nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
      if (observerService)
        observerService->RemoveObserver(this, "profile-change-net-teardown");

      // We know this is a simple pointer
      gDataChannelShutdown = nullptr;
    }

  NS_IMETHODIMP Observe(nsISupports* aSubject, const char* aTopic,
                        const PRUnichar* aData) {
    if (strcmp(aTopic, "profile-change-net-teardown") == 0) {
      LOG(("Shutting down SCTP"));
      if (sctp_initialized) {
        usrsctp_finish();
        sctp_initialized = false;
      }
    }
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS1(DataChannelShutdown, nsIObserver);


BufferedMsg::BufferedMsg(struct sctp_sendv_spa &spa,const char *data,
                         PRUint32 length) : mLength(length)
{
  mSpa = new sctp_sendv_spa;
  *mSpa = spa;
  char *tmp = new char[length]; // infallible malloc!
  memcpy(tmp,data,length);
  mData = tmp;
}

BufferedMsg::~BufferedMsg()
{
  delete mSpa;
  delete mData;
}

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
  mLocalPort = 0;
  mRemotePort = 0;
  mDeferTimeout = 10;
  mTimerRunning = false;
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

NS_IMPL_THREADSAFE_QUERY_INTERFACE1(DataChannelConnection,
                                    nsITimerCallback)
NS_IMPL_THREADSAFE_ADDREF(DataChannelConnection)
NS_IMPL_THREADSAFE_RELEASE(DataChannelConnection)

bool
DataChannelConnection::Init(unsigned short aPort, bool aUsingDtls)
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
      if (aUsingDtls) {
        LOG(("sctp_init(DTLS)"));
        usrsctp_init(0,DataChannelConnection::SctpDtlsOutput);
      } else {
        LOG(("sctp_init(%d)",aPort));
        usrsctp_init(aPort,NULL);
      }

      usrsctp_sysctl_set_sctp_debug_on(0 /*SCTP_DEBUG_ALL*/);
      usrsctp_sysctl_set_sctp_blackhole(2);
      sctp_initialized = true;

      gDataChannelShutdown = new DataChannelShutdown();
    }
  }

  // Open sctp association across tunnel
  // XXX This code will need to change to support SCTP-over-DTLS
  if ((mMasterSocket = usrsctp_socket(
         aUsingDtls ? AF_CONN : AF_INET,
         SOCK_STREAM, IPPROTO_SCTP, receive_cb, NULL, 0, this)) == NULL) {
    return false;
  }

  if (!aUsingDtls) {
    memset(&encaps, 0, sizeof(encaps));
    encaps.sue_address.ss_family = AF_INET;
    encaps.sue_port = htons(aPort); // XXX also causes problems with loopback
    if (usrsctp_setsockopt(mMasterSocket, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT,
                           (const void*)&encaps, 
                           (socklen_t)sizeof(struct sctp_udpencaps)) < 0) {
      LOG(("*** failed encaps"));
      usrsctp_close(mMasterSocket);
      mMasterSocket = NULL;
      return false;
    }
    LOG(("SCTP encapsulation local port %d",aPort));
  }

  av.assoc_id = SCTP_ALL_ASSOC;
  av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
  if (usrsctp_setsockopt(mMasterSocket, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &av,
                         (socklen_t)sizeof(struct sctp_assoc_value)) < 0) {
    LOG(("*** failed enable stream reset"));
    usrsctp_close(mMasterSocket);
    mMasterSocket = NULL;
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
      mMasterSocket = NULL;
    }
  }

  mSocket = NULL;
  return true;
}

nsresult
DataChannelConnection::StartDefer()
{
  nsresult rv;
  // XXX Is this lock needed?  Timers can only be inited from the main thread...
  //MutexAutoLock lock(mLock);
  if (!mDeferredTimer) {
    mDeferredTimer = do_CreateInstance("@mozilla.org/timer;1", &rv);
    if (NS_FAILED(rv)) {
      LOG(("%s: cannot create deferred send timer",__FUNCTION__));
      // XXX and do....?
      return rv;
    }
  }

  if (!mTimerRunning) {
    rv = mDeferredTimer->InitWithCallback(this, mDeferTimeout,
                                          nsITimer::TYPE_ONE_SHOT);
    if (NS_FAILED(rv)) {
      LOG(("%s: cannot initialize open timer",__FUNCTION__));
      // XXX and do....?
      return rv;
    }
    mTimerRunning = true;
  }
  return NS_OK;
}

// nsITimerCallback

NS_IMETHODIMP
DataChannelConnection::Notify(nsITimer *timer)
{
  LOG(("%s: %p [%p] (%dms), sending deferred messages", __FUNCTION__, this, timer, mDeferTimeout));

  if (timer == mDeferredTimer) {
    if (SendDeferredMessages() != 0) {
      // XXX I don't think i need the lock, since this must be main thread...
      nsresult rv = mDeferredTimer->InitWithCallback(this, mDeferTimeout,
                                                     nsITimer::TYPE_ONE_SHOT);
      if (NS_FAILED(rv)) {
        LOG(("%s: cannot initialize open timer",__FUNCTION__));
        // XXX and do....?
        return rv;
      }
      mTimerRunning = true;
    } else {
      LOG(("Turned off deferred send timer"));
      mTimerRunning = false;
    }
  }
  return NS_OK;
}

bool 
DataChannelConnection::ConnectDTLS(TransportFlow *aFlow, PRUint16 localport, PRUint16 remoteport)
{
  LOG(("Connect DTLS local %d, remote %d",localport,remoteport));

  NS_PRECONDITION(mMasterSocket,"SCTP wasn't initialized before ConnectDTLS!");
  NS_ENSURE_TRUE(aFlow,false);

  mTransportFlow = aFlow;
  mTransportFlow->SignalPacketReceived.connect(this, &DataChannelConnection::PacketReceived);
  mLocalPort = localport;
  mRemotePort = remoteport;

  PR_CreateThread(
    PR_SYSTEM_THREAD,
    DataChannelConnection::DTLSConnectThread, this,
    PR_PRIORITY_NORMAL,
    PR_GLOBAL_THREAD,
    PR_JOINABLE_THREAD, 0
  );

  return true; // not finished yet
}

/* static */
void
DataChannelConnection::DTLSConnectThread(void *data)
{
  DataChannelConnection *_this = static_cast<DataChannelConnection*>(data);
  struct sockaddr_conn addr;

  memset(&addr, 0, sizeof(addr));
  addr.sconn_family = AF_CONN;
#if !defined(__Userspace_os_Linux) && !defined(__Userspace_os_Windows)
  addr.sconn_len = sizeof(addr);
#endif
  addr.sconn_port = htons(_this->mLocalPort);

  int r = usrsctp_bind(_this->mMasterSocket, reinterpret_cast<struct sockaddr *>(&addr),
                       sizeof(addr));
  if (r < 0) {
    LOG(("usrsctp_bind failed: %d",r));
    return;
  }

  // This is the remote addr
  addr.sconn_port = htons(_this->mRemotePort);
  addr.sconn_addr = static_cast<void *>(_this);
  r = usrsctp_connect(_this->mMasterSocket, reinterpret_cast<struct sockaddr *>(&addr),
                      sizeof(addr));
  if (r < 0) {
    LOG(("usrsctp_connect failed: %d",r));
    return;
  }

  // Notify Connection open
  // XXX We need to make sure connection sticks around until the message is delivered
  LOG(("%s: sending ON_CONNECTION for %p",__FUNCTION__,_this));
  // XXX any locking needed?
  _this->mNumChannels = 0;
  _this->mSocket = _this->mMasterSocket;  // XXX Be careful!  
  _this->mState = OPEN;
  LOG(("DTLS connect() succeeded!  Entering connected mode"));

  NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                            DataChannelOnMessageAvailable::ON_CONNECTION,
                            _this, NULL));

  // XXX post return?
  return;
}

void 
DataChannelConnection::PacketReceived(TransportFlow *flow, 
                                      const unsigned char *data, size_t len)
{
  //LOG(("%p: SCTP/DTLS received %ld bytes",this,len));

  // Pass the data to SCTP
  usrsctp_conninput(static_cast<void *>(this), data, len, 0);
}

// XXX Merge with SctpDtlsOutput?
int
DataChannelConnection::SendPacket(const unsigned char *data, size_t len)
{
  //LOG(("%p: SCTP/DTLS sent %ld bytes",this,len));
  return mTransportFlow->SendPacket(data, len) < 0 ? 1 : 0;
}

/* static */
int 
DataChannelConnection::SctpDtlsOutput(void *addr, void *buffer, size_t length,
                                      uint8_t tos, uint8_t set_df)
{
  DataChannelConnection *peer = static_cast<DataChannelConnection *>(addr);

  return peer->SendPacket(static_cast<unsigned char *>(buffer), length);
}

// listen for incoming associations
// Blocks! - Don't call this from main thread!
bool
DataChannelConnection::Listen(unsigned short port)
{
  struct sockaddr_in addr;
  socklen_t addr_len;

  NS_WARN_IF_FALSE(!NS_IsMainThread(), "Blocks, do not call from main thread!!!");

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

  //LOG(("Accepting incoming connection.  Entering connected mode. mSocket=%p, masterSocket=%p", mSocket, mMasterSocket));

  // Notify Connection open
  // XXX We need to make sure connection sticks around until the message is delivered
  LOG(("%s: sending ON_CONNECTION for %p",__FUNCTION__,this));
  NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                            DataChannelOnMessageAvailable::ON_CONNECTION,
                            this, NULL));
  mNumChannels = 0;
  return true;
}

// Blocks! - Don't call this from main thread!
bool
DataChannelConnection::Connect(const char *addr, unsigned short port)
{
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;

  NS_WARN_IF_FALSE(!NS_IsMainThread(), "Blocks, do not call from main thread!!!");

  /* Acting as the connector */
  LOG(("Connecting to %s, port %u",addr, port));
  memset((void *)&addr4, 0, sizeof(struct sockaddr_in));
  memset((void *)&addr6, 0, sizeof(struct sockaddr_in6));
#ifdef HAVE_SIN_LEN
  addr4.sin_len = sizeof(struct sockaddr_in);
#endif
#ifdef HAVE_SIN6_LEN
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

  // XXX fix!  Main-thread IO
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

PRInt32
DataChannelConnection::SendOpenRequestMessage(const nsACString& label,
                                              PRUint16 streamOut, bool unordered,
                                              PRUint16 prPolicy, PRUint32 prValue)
{
  /* XXX: This should be encoded in a better way */
  char *temp = ToNewCString(label);
  int len = strlen(temp); // not including nul
  struct rtcweb_datachannel_open_request *req = 
    (struct rtcweb_datachannel_open_request*) malloc(sizeof(*req)+len);
   // careful - ok because request includes 1 char label

  memset(req, 0, sizeof(struct rtcweb_datachannel_open_request));
  req->msg_type = DATA_CHANNEL_OPEN_REQUEST;
  switch (prPolicy) {
  case SCTP_PR_SCTP_NONE:
    req->channel_type = DATA_CHANNEL_RELIABLE;
    break;
  case SCTP_PR_SCTP_TTL:
    req->channel_type = DATA_CHANNEL_PARTIAL_RELIABLE_TIMED;
    break;
  case SCTP_PR_SCTP_RTX:
    req->channel_type = DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT;
    break;
  default:
    // FIX! need to set errno!  Or make all these SendXxxx() funcs return 0 or errno!
    NS_Free(temp);
    return (0);
  }
  req->flags = htons(0);
  if (unordered) {
    req->flags |= htons(DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED);
  }
  req->reliability_params = htons((uint16_t)prValue); /* XXX Why 16-bit */
  req->priority = htons(0); /* XXX: add support */
  strcpy(&req->label[0],temp);
  
  NS_Free(temp);
  return SendControlMessage(req, sizeof(*req)+len, streamOut);
}

// XXX This should use a separate thread (outbound queue) which should
// select() to know when to *try* to send data to the socket again.
// Alternatively, it can use a timeout, but that's guaranteed to be wrong
// (just not sure in what direction).  We could re-implement NSPR's
// PR_POLL_WRITE/etc handling... with a lot of work.
bool
DataChannelConnection::SendDeferredMessages()
{
  PRUint32 i;
  DataChannel *channel;
  bool still_blocked = false;
  bool sent = false;

  // XXX For total fairness, on a still_blocked we'd start next time at the
  // same index.  Sorry, not going to bother for now.
  for (i = 0; i < mStreamsOut.Length() && !still_blocked; i++) {
    channel = mStreamsOut[i];
    if (!channel)
      continue;

    // Only one of these should be set....
    if (channel->mFlags & DATA_CHANNEL_FLAGS_SEND_REQ) {
      if (SendOpenRequestMessage(channel->mLabel, channel->mStreamOut, 
                                 channel->mFlags & DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED,
                                 channel->mPrPolicy, channel->mPrValue)) {
        channel->mFlags &= ~DATA_CHANNEL_FLAGS_SEND_REQ;
        sent = true;
      } else {
        if (errno == EAGAIN) {
          still_blocked = true;
        } else {
          // Close the channel, inform the user
          mStreamsOut[channel->mStreamOut] = NULL;
          channel->mState = CLOSED;
          // Don't need to reset; we didn't open it
          NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                                    DataChannelOnMessageAvailable::ON_CHANNEL_CLOSED, this,
                                    channel));
        }
      }
    }
    if (!still_blocked &&
        channel->mFlags & DATA_CHANNEL_FLAGS_SEND_RSP) {
      if (SendOpenResponseMessage(channel->mStreamOut, channel->mStreamIn)) {
        channel->mFlags &= ~DATA_CHANNEL_FLAGS_SEND_RSP;
        sent = true;
      } else {
        if (errno == EAGAIN) {
          still_blocked = true;
        } else {
          // Close the channel
          // Don't need to reset; we didn't open it
          // The other side may be left with a hanging Open.  Our inability to
          // send the open response means we can't easily tell them about it
          // We haven't informed the user/DOM of the creation yet, so just
          // delete the channel.
          mStreamsIn[channel->mStreamIn]   = NULL;
          mStreamsOut[channel->mStreamOut] = NULL;
          delete channel;
        }
      }
    }
    if (!still_blocked &&
        channel->mFlags & DATA_CHANNEL_FLAGS_SEND_ACK) {
      if (SendOpenAckMessage(channel->mStreamOut)) {
        channel->mFlags &= ~DATA_CHANNEL_FLAGS_SEND_ACK;
        sent = true;
      } else {
        if (errno == EAGAIN) {
          still_blocked = true;
        } else {
          // Close the channel, inform the user
          Close(channel->mStreamOut);
        }
      }
    }
    if (!still_blocked &&
        channel->mFlags & DATA_CHANNEL_FLAGS_SEND_DATA) {
      bool failed_send = false;
      PRInt32 result;

      if (channel->mState == CLOSED || channel->mState == CLOSING) {
        channel->mBufferedData.Clear();
      }
      while (!channel->mBufferedData.IsEmpty() &&
             !failed_send) {
        struct sctp_sendv_spa *spa = channel->mBufferedData[0]->mSpa;
        const char *data           = channel->mBufferedData[0]->mData;
        PRUint32 len               = channel->mBufferedData[0]->mLength;

        // SCTP will return EMSGSIZE if the message is bigger than the buffer
        // size (or EAGAIN if there isn't space)
        if ((result = usrsctp_sendv(mSocket, data, len,
                                    NULL, 0,
                                    (void *)spa, (socklen_t)sizeof(struct sctp_sendv_spa),
                                    SCTP_SENDV_SPA,
                                    spa->sendv_sndinfo.snd_flags) < 0)) {
          if (errno == EAGAIN) {
            // leave queued for resend
            failed_send = true;
            LOG(("queue full again when resending %d bytes (%d)",len,result));
          } else {
            LOG(("error %d re-sending string",errno));
            failed_send = true;
          }
        } else {
          //LOG(("Resent buffer of %d bytes (%d)",len,result));
          sent = true;
          channel->mBufferedData.RemoveElementAt(0);
        }
      }
      if (channel->mBufferedData.IsEmpty())
        channel->mFlags &= ~DATA_CHANNEL_FLAGS_SEND_DATA;
      else
        still_blocked = true;
    }
  }

  if (!still_blocked) {
    // adjust time for next wait
    return false;
  }
  // adjust time?  More time for next wait if we didn't send anything, less if did
  // Pretty crude, but better than nothing; just to keep CPU use down
  if (!sent && mDeferTimeout < 50)
    mDeferTimeout++;
  else if (sent && mDeferTimeout > 10)
    mDeferTimeout--;

  return true;
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
  nsCString label(nsDependentCString(req->label));

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
                            label,
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
      LOG(("successful incoming open of '%s' in: %u, out: %u, total channels %d\n",
           label.get(),streamIn, streamOut, mNumChannels));

      /* Notify ondatachannel */
      // XXX We need to make sure connection sticks around until the message is delivered
      LOG(("%s: sending ON_CHANNEL_CREATED for %p",__FUNCTION__,channel));
      NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                                DataChannelOnMessageAvailable::ON_CHANNEL_CREATED,
                                this, channel));
    } else {
      if (errno == EAGAIN) {
        channel->mFlags |= DATA_CHANNEL_FLAGS_SEND_RSP;
        StartDefer();
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

  if (rsp->error) {
    LOG(("%s: error in response to open of channel %d (%s)",
         __FUNCTION__, streamOut, channel->mLabel.get()));
    
  } else {
    DC_ENSURE_TRUE(!FindChannelByStreamIn(streamIn));

    channel->mStreamIn = streamIn;
    channel->mState = OPEN;
    mStreamsIn[streamIn] = channel;
    if (SendOpenAckMessage(streamOut)) {
      channel->mFlags = 0;
    } else {
      // XXX Only on EAGAIN!?  And if not, then close the channel??
      channel->mFlags |= DATA_CHANNEL_FLAGS_SEND_ACK;
      StartDefer();
    }
    LOG(("%s: sending ON_CHANNEL_OPEN for %p",__FUNCTION__,channel));
    NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                              DataChannelOnMessageAvailable::ON_CHANNEL_OPEN, this,
                              channel));
  }
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
  LOG(("unknown DataChannel message received: %u, len %ld on stream %lu",ppid,length,streamIn));
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
    nsCAutoString recvData(buffer, length);

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
        NS_WARN_IF_FALSE(channel->mBinaryBuffer.IsEmpty(),"Binary message aborted by text message!");
        if (!channel->mBinaryBuffer.IsEmpty())
          channel->mBinaryBuffer.Truncate(0);
        break;

      case DATA_CHANNEL_PPID_BINARY:
        channel->mBinaryBuffer += recvData;
        LOG(("DataChannel: Received binary message of length %lu (total %u) on channel id %d",
             length, channel->mBinaryBuffer.Length(),channel->mStreamOut));
        return; // Not ready to notify application

      case DATA_CHANNEL_PPID_BINARY_LAST:
        LOG(("DataChannel: Received binary message of length %lu on channel id %d",
             length, channel->mStreamOut));
        if (!channel->mBinaryBuffer.IsEmpty()) {
          channel->mBinaryBuffer += recvData;
          LOG(("%s: sending ON_DATA (binary fragmented) for %p",__FUNCTION__,channel));
          NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                              DataChannelOnMessageAvailable::ON_DATA, this,
                              channel, channel->mBinaryBuffer, 
                              channel->mBinaryBuffer.Length()));
          channel->mBinaryBuffer.Truncate(0);
          return;
        }
        // else send using recvData normally
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
          LOG(("length %u, sizeof(*req) = %u",length,sizeof(*req)));
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
    case DATA_CHANNEL_PPID_BINARY_LAST:
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
#if defined(__Userspace_os_Windows)
  DWORD addr_len = INET6_ADDRSTRLEN;
#endif

  switch (spc->spc_aaddr.ss_family) {
  case AF_INET:
    sin = (struct sockaddr_in *)&spc->spc_aaddr;
#if !defined(__Userspace_os_Windows)
    addr = inet_ntop(AF_INET, &sin->sin_addr, addr_buf, INET6_ADDRSTRLEN);
#else
    if (WSAAddressToStringA((LPSOCKADDR)sin, sizeof(sin->sin_addr), NULL, 
                            addr_buf, &addr_len)) {
      return;
    }
#endif
    break;
  case AF_INET6:
    sin6 = (struct sockaddr_in6 *)&spc->spc_aaddr;
#if !defined(__Userspace_os_Windows)
    addr = inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf, INET6_ADDRSTRLEN);
#else
    if (WSAAddressToStringA((LPSOCKADDR)sin6, sizeof(sin6), NULL, 
                            addr_buf, &addr_len)) {
      return;
    }
#endif
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
            NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                                      DataChannelOnMessageAvailable::ON_CHANNEL_CLOSED, this,
                                      channel));
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
            NS_DispatchToMainThread(new DataChannelOnMessageAvailable(
                                      DataChannelOnMessageAvailable::ON_CHANNEL_CLOSED, this,
                                      channel));
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
          StartDefer();
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
DataChannelConnection::Open(const nsACString& label, Type type, bool inOrder, 
                            PRUint32 prValue, DataChannelListener *aListener,
                            nsISupports *aContext)
{
  DataChannel *channel;
  PRUint16 streamOut, prPolicy;
  PRUint32 flags;

  LOG(("DC Open: label %s, type %u, inorder %d, prValue %u, listener %p, context %p",
       PromiseFlatCString(label).get(),type, inOrder, prValue, aListener, aContext));
  MutexAutoLock lock(mLock);
  switch (type) {
    case DATA_CHANNEL_RELIABLE:
      prPolicy = SCTP_PR_SCTP_NONE;
      break;
    case DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT:
      prPolicy = SCTP_PR_SCTP_RTX;
      break;
    case DATA_CHANNEL_PARTIAL_RELIABLE_TIMED:
      prPolicy = SCTP_PR_SCTP_TTL;
      break;
  }
  if ((prPolicy == SCTP_PR_SCTP_NONE) && (prValue != 0)) {
    return NULL;
  }

  flags = !inOrder ? DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED : 0;
  streamOut = FindFreeStreamOut(); // may be INVALID_STREAM!
  channel = new DataChannel(this, streamOut, INVALID_STREAM,
                            DataChannel::CONNECTING,
                            label, type, prValue,
                            flags,
                            aListener, aContext);
  if (!channel) { // XXX remove - infallible malloc
    return (NULL);
  }

  if (streamOut == INVALID_STREAM) {
    RequestMoreStreamsOut();
    return channel;
  }
  mStreamsOut[streamOut] = channel;

  if (!SendOpenRequestMessage(label, streamOut, !inOrder, prPolicy, prValue)) {
    LOG(("SendOpenRequest failed, errno = %d",errno));
    if (errno == EAGAIN) {
      channel->mFlags |= DATA_CHANNEL_FLAGS_SEND_REQ;
      StartDefer();
    } else {
      mStreamsOut[streamOut] = NULL;
      MutexAutoUnlock unlock(mLock);
      delete channel;
      return NULL;
    }
  }
  return channel;
}

void
DataChannelConnection::Close(PRUint16 streamOut)
{
  DataChannel *channel;

  MutexAutoLock lock(mLock);
  LOG(("Closing stream %d",streamOut));
  channel = FindChannelByStreamOut(streamOut);
  if (channel) {
    channel->mBufferedData.Clear();
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
DataChannelConnection::SendMsgInternal(DataChannel *channel, const char *data,
                                       PRUint32 length, PRUint32 ppid)
{
  uint16_t flags;
  struct sctp_sendv_spa spa;
  PRInt32 result;

  NS_ENSURE_TRUE(channel->mState == OPEN || channel->mState == CONNECTING,0);
  NS_WARN_IF_FALSE(length > 0,"Length is 0?!");

  flags = (channel->mFlags & DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED) ? SCTP_UNORDERED : 0;

  // To avoid problems where an in-order OPEN_RESPONSE is lost and an
  // out-of-order data message "beats" it, require data to be in-order
  // until we get an ACK.
  if (channel->mState == CONNECTING) {
    flags &= ~SCTP_UNORDERED;
  }
  spa.sendv_sndinfo.snd_ppid = htonl(ppid);
  spa.sendv_sndinfo.snd_sid = channel->mStreamOut;
  spa.sendv_sndinfo.snd_flags = flags;
  spa.sendv_sndinfo.snd_context = 0;
  spa.sendv_sndinfo.snd_assoc_id = 0;

  spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
  spa.sendv_prinfo.pr_value = channel->mPrValue;

  spa.sendv_flags = SCTP_SEND_SNDINFO_VALID | SCTP_SEND_PRINFO_VALID;

  // XXX fix!  Main-thread IO
  // XXX FIX!  to deal with heavy overruns of JS trying to pass data in
  // (more than the buffersize) queue data onto another thread to do the
  // actual sends.  See netwerk/protocol/websocket/WebSocketChannel.cpp

  // SCTP will return EMSGSIZE if the message is bigger than the buffer
  // size (or EAGAIN if there isn't space)
  if (channel->mBufferedData.IsEmpty()) {
    result = usrsctp_sendv(mSocket, data, length,
                           NULL, 0, 
                           (void *)&spa, (socklen_t)sizeof(struct sctp_sendv_spa),
                           SCTP_SENDV_SPA, flags);
    //LOG(("Sent buffer (len=%u), result=%d",length,result));
  } else {
    // Fake EAGAIN if we're already buffering data
    result = -1;
    errno = EAGAIN;
  }
  if (result < 0) {
    if (errno == EAGAIN) {
      // queue data for resend!  And queue any further data for the stream until it is...
      BufferedMsg *buffered = new BufferedMsg(spa,data,length); // infallible malloc
      channel->mBufferedData.AppendElement(buffered); // owned by mBufferedData array
      channel->mFlags |= DATA_CHANNEL_FLAGS_SEND_DATA;
      //LOG(("Queued %u buffers (len=%u)",channel->mBufferedData.Length(),length));
      StartDefer();
      return 0;
    }
    LOG(("error %d sending string",errno));
  }
  return result;
}

// Handles fragmenting binary messages
PRInt32
DataChannelConnection::SendBinary(DataChannel *channel, const char *data,
                                  PRUint32 len)
{
  // Since there's a limit on network buffer size and no limits on message
  // size, and we don't want to use EOR mode (multiple writes for a
  // message, but all other streams are blocked until you finish sending
  // this message), we need to add application-level fragmentatio of large
  // messages.  On a reliable channel, these can be simply rebuilt into a
  // large message.  On an unreliable channel, we can't and don't know how
  // long to wait, and there are no retransmissions, and no easy way to
  // tell the user "this part is missing", so on unreliable channels we
  // need to return an error if sending more bytes than the network buffers
  // can hold, and perhaps a lower number.

  // XXX We *really* don't want to do this from main thread!
  if (len > DATA_CHANNEL_MAX_BINARY_FRAGMENT &&
      channel->mPrPolicy == DATA_CHANNEL_RELIABLE) {
    PRInt32 sent=0;
    PRUint32 origlen = len;
    LOG(("Sending binary message length %u in chunks",len));
    // XXX check flags for out-of-order, or force in-order for large binary messages
    while (len > 0) {
      PRUint32 sendlen = PR_MIN(len,DATA_CHANNEL_MAX_BINARY_FRAGMENT);
      PRUint32 ppid;
      len -= sendlen;
      ppid = len > 0 ? DATA_CHANNEL_PPID_BINARY : DATA_CHANNEL_PPID_BINARY_LAST;
      //LOG(("Send chunk of %d bytes, ppid %d",sendlen,ppid));
      // Note that these might end up being deferred and queued.
      sent += SendMsgInternal(channel, data, sendlen, ppid);
      data += sendlen;
    }
    LOG(("Sent %d buffers for %u bytes, %d sent immediately, % buffers queued",
         (origlen+DATA_CHANNEL_MAX_BINARY_FRAGMENT-1)/DATA_CHANNEL_MAX_BINARY_FRAGMENT,
         origlen,sent,
         channel->mBufferedData.Length()));
    return sent;
  }
  NS_WARN_IF_FALSE(len <= DATA_CHANNEL_MAX_BINARY_FRAGMENT,
                   "Sending too-large data on unreliable channel!");

  // This will fail if the message is too large
  return SendMsgInternal(channel, data, len, DATA_CHANNEL_PPID_BINARY_LAST);
}

PRInt32
DataChannelConnection::SendBlob(PRUint16 stream, nsIInputStream *aBlob)
{
  DataChannel *channel = mStreamsOut[stream];
  NS_ENSURE_TRUE(channel,0);
  // Spawn a thread to send the data

  LOG(("Sending blob to stream %u",stream));

  // XXX to do this safely, we must enqueue these atomically onto the
  // output socket.  We need a sender thread(s?) to enque data into the
  // socket and to avoid main-thread IO that might block.  Even on a
  // background thread, we may not want to block on one stream's data.
  // I.e. run non-blocking and service multiple channels.

  // For now as a hack, block main thread while sending it.
  nsAutoPtr<nsCString> temp(new nsCString());
  PRUint32 len;
  aBlob->Available(&len);
  nsresult rv = NS_ReadInputStreamToString(aBlob, *temp, len);

  NS_ENSURE_SUCCESS(rv, rv);

  aBlob->Close();
  //aBlob->Release(); We didn't AddRef() the way WebSocket does in OutboundMessage (yet)

  // Consider if it makes sense to split the message ourselves for
  // transmission, at least on RELIABLE channels.  Sending large blobs via
  // unreliable channels requires some level of application involvement, OR
  // sending them at big, single messages, which if large will probably not
  // get through.

  // XXX For now, send as one large binary message.  We should also signal
  // (via PPID) that it's a blob.
  const char *data = temp.get()->BeginReading();
  len              = temp.get()->Length();

  return SendBinary(channel, data, len);
}

PRInt32
DataChannelConnection::SendMsgCommon(PRUint16 stream, const nsACString &aMsg,
                                     bool isBinary)
{
  NS_ABORT_IF_FALSE(NS_IsMainThread(), "not main thread");
  // We really could allow this from other threads, so long as we deal with
  // asynchronosity issues with channels closing, in particular access to
  // mStreamsOut, and issues with the association closing (access to mSocket).

  const char *data = aMsg.BeginReading();
  PRUint32 len     = aMsg.Length();
  DataChannel *channel;

  if (isBinary)
    LOG(("Sending to stream %u: %u bytes",stream,len));
  else
    LOG(("Sending to stream %u: %s",stream,data));
  // XXX if we want more efficiency, translate flags once at open time
  channel = mStreamsOut[stream];
  NS_ENSURE_TRUE(channel,0);

  if (isBinary)
    return SendBinary(channel, data, len);
  return SendMsgInternal(channel, data, len, DATA_CHANNEL_PPID_DOMSTRING);
}
}
