/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>
#include <iostream>


#include "vcm.h"
#include "CSFLog.h"
#include "CSFLogStream.h"
#include "ccapi_call_info.h"
#include "CC_SIPCCCallInfo.h"
#include "ccapi_device_info.h"
#include "CC_SIPCCDeviceInfo.h"

#include "nspr.h"
#include "nss.h"
#include "pk11pub.h"

#include "nsThreadUtils.h"
#include "nsProxyRelease.h"

#include "runnable_utils.h"
#include "PeerConnectionCtx.h"
#include "PeerConnectionImpl.h"

#include "nsPIDOMWindow.h"
#include "nsDOMDataChannel.h"

#ifndef USE_FAKE_MEDIA_STREAMS
#include "MediaSegment.h"
#endif

using namespace mozilla;
using namespace mozilla::dom;

namespace mozilla {
  class DataChannel;
}

class nsIDOMDataChannel;

static const char* logTag = "PeerConnectionImpl";

namespace sipcc {

typedef enum {
  PC_OBSERVER_CALLBACK,
  PC_OBSERVER_CONNECTION,
  PC_OBSERVER_CLOSEDCONNECTION,
  PC_OBSERVER_DATACHANNEL,
  PC_OBSERVER_ICE
} PeerConnectionObserverType;

// TODO: Refactor this.
class PeerConnectionObserverDispatch : public nsRunnable {

public:
  PeerConnectionObserverDispatch(CSF::CC_CallInfoPtr info,
                                 nsRefPtr<PeerConnectionImpl> pc,
                                 IPeerConnectionObserver* observer) :
    mType(PC_OBSERVER_CALLBACK), mInfo(info), mChannel(nsnull), mPC(pc), mObserver(observer) {}

  PeerConnectionObserverDispatch(PeerConnectionObserverType type,
                                 nsRefPtr<nsIDOMDataChannel> channel,
                                 nsRefPtr<PeerConnectionImpl> pc,
                                 IPeerConnectionObserver* observer) :
    mType(type), mInfo(nsnull), mChannel(channel), mPC(pc), mObserver(observer) {}

  PeerConnectionObserverDispatch(PeerConnectionObserverType type,
                                 nsRefPtr<PeerConnectionImpl> pc,
                                 IPeerConnectionObserver* observer) :
    mType(type), mInfo(nsnull), mPC(pc), mObserver(observer) {}

  ~PeerConnectionObserverDispatch(){}

  nsresult Run()
  {
    switch (mType) {
      case PC_OBSERVER_CALLBACK:
        {
          StatusCode code;
          std::string s_sdpstr;
          MediaStreamTable *streams = NULL;

          cc_call_state_t state = mInfo->getCallState();
          std::string statestr = mInfo->callStateToString(state);

          nsDOMMediaStream* stream;
          PRUint32 hint;

          switch (state) {
            case CREATEOFFER:
              s_sdpstr = mInfo->getSDP();
              mObserver->OnCreateOfferSuccess(s_sdpstr.c_str());
              break;

            case CREATEANSWER:
              s_sdpstr = mInfo->getSDP();
              mObserver->OnCreateAnswerSuccess(s_sdpstr.c_str());
              break;

            case CREATEOFFERERROR:
              code = (StatusCode)mInfo->getStatusCode();
              mObserver->OnCreateOfferError(code);
              break;

            case CREATEANSWERERROR:
              code = (StatusCode)mInfo->getStatusCode();
              mObserver->OnCreateAnswerError(code);
              break;

            case SETLOCALDESC:
              code = (StatusCode)mInfo->getStatusCode();
              mObserver->OnSetLocalDescriptionSuccess(code);
              break;

            case SETREMOTEDESC:
              code = (StatusCode)mInfo->getStatusCode();
              mObserver->OnSetRemoteDescriptionSuccess(code);
              break;

            case SETLOCALDESCERROR:
              code = (StatusCode)mInfo->getStatusCode();
              mObserver->OnSetLocalDescriptionError(code);
              break;

            case SETREMOTEDESCERROR:
              code = (StatusCode)mInfo->getStatusCode();
              mObserver->OnSetRemoteDescriptionError(code);
              break;

            case REMOTESTREAMADD:
              streams = mInfo->getMediaStreams();
              stream = mPC->GetRemoteStream(streams->media_stream_id)->GetMediaStream();

              hint = stream->GetHintContents();
              if (hint == nsDOMMediaStream::HINT_CONTENTS_AUDIO) {
                mObserver->OnAddStream(stream, "audio");
              } else if (hint == nsDOMMediaStream::HINT_CONTENTS_VIDEO) {
                mObserver->OnAddStream(stream, "video");
              }

              break;

            default:
              CSFLogDebugS(logTag, ": **** CALL STATE IS: " << statestr);
              break;
          }
          break;
        }
      case PC_OBSERVER_CONNECTION:
        std::cerr << "Delivering PeerConnection onconnection" << std::endl;
        mObserver->NotifyConnection();
        break;
      case PC_OBSERVER_CLOSEDCONNECTION:
        std::cerr << "Delivering PeerConnection onclosedconnection" << std::endl;
        mObserver->NotifyClosedConnection();
        break;
      case PC_OBSERVER_DATACHANNEL:
        std::cerr << "Delivering PeerConnection ondatachannel" << std::endl;
        mObserver->NotifyDataChannel(mChannel);
        break;
      case PC_OBSERVER_ICE:
        std::cerr << "Delivering PeerConnection ICE callback " << std::endl;
        mObserver->OnStateChange(IPeerConnectionObserver::kIceState);
    }
    return NS_OK;
  }

private:
  PeerConnectionObserverType mType;
  CSF::CC_CallInfoPtr mInfo;
  nsRefPtr<nsIDOMDataChannel> mChannel;
  nsRefPtr<PeerConnectionImpl> mPC;
  nsCOMPtr<IPeerConnectionObserver> mObserver;
};

/* We get this callback in order to find out which tracks are audio and which
 * are video. We should get this callback right away for existing streams after
 * we add this class as a listener.
 */
void
LocalSourceStreamInfo::NotifyQueuedTrackChanges(
  mozilla::MediaStreamGraph* aGraph,
  mozilla::TrackID aID,
  mozilla::TrackRate aTrackRate,
  mozilla::TrackTicks aTrackOffset,
  PRUint32 aTrackEvents,
  const mozilla::MediaSegment& aQueuedMedia)
{
  /* Add the track ID to the list for audio/video so they can be counted when
   * createOffer/createAnswer is called. This tells us whether we have the
   * camera, mic, or both for example.
   */
  mozilla::MediaSegment::Type trackType = aQueuedMedia.GetType();

  if (trackType == mozilla::MediaSegment::AUDIO) {
    // Should have very few tracks so not a hashtable
    bool found = false;
    for (unsigned u = 0; u < mAudioTracks.Length(); u++) {
      if (aID == mAudioTracks.ElementAt(u)) {
        found = true;
        break;
      }
    }

    if (!found) {
      mAudioTracks.AppendElement(aID);
    }
  } else if (trackType == mozilla::MediaSegment::VIDEO) {
    // Should have very few tracks so not a hashtable
    bool found = false;
    for (unsigned u = 0; u < mVideoTracks.Length(); u++) {
      if (aID == mVideoTracks.ElementAt(u)) {
        found = true;
        break;
      }
    }

    if (!found) {
      mVideoTracks.AppendElement(aID);
    }
  } else {
    CSFLogError(logTag, "NotifyQueuedTrackChanges - unknown media type");
  }
}

nsDOMMediaStream*
LocalSourceStreamInfo::GetMediaStream()
{
  return mMediaStream.get();
}


nsDOMMediaStream*
RemoteSourceStreamInfo::GetMediaStream()
{
  return mMediaStream.get();
}

/* If the ExpectAudio hint is on we will add a track at the default first
 * audio track ID (0)
 * FIX - Do we need to iterate over the tracks instead of taking these hints?
 */
void
LocalSourceStreamInfo::ExpectAudio()
{
  mAudioTracks.AppendElement(0);
}

// If the ExpectVideo hint is on we will add a track at the default first
// video track ID (1).
void
LocalSourceStreamInfo::ExpectVideo()
{
  mVideoTracks.AppendElement(1);
}

unsigned
LocalSourceStreamInfo::AudioTrackCount()
{
  return mAudioTracks.Length();
}

unsigned
LocalSourceStreamInfo::VideoTrackCount()
{
  return mVideoTracks.Length();
}

PeerConnectionImpl* PeerConnectionImpl::CreatePeerConnection()
{
  PeerConnectionImpl *pc = new PeerConnectionImpl();

  CSFLogDebug(logTag, "Created PeerConnection=%p", pc);

  return pc;
}

std::map<const std::string, PeerConnectionImpl *>
  PeerConnectionImpl::peerconnections;

NS_IMPL_THREADSAFE_ISUPPORTS1(PeerConnectionImpl, IPeerConnection)

PeerConnectionImpl::PeerConnectionImpl()
: mRole(kRoleUnknown)
  , mCall(NULL)
  , mReadyState(kNew)
  , mPCObserver(NULL)
  , mWindow(NULL)
  , mFingerprint("TempFingerprint")
  , mLocalSourceStreamsLock(PR_NewLock())
  , mIceCtx(NULL)
  , mIceStreams(NULL)
  , mIceState(kIceGathering)
  , mIdentity(NULL)
 {}

PeerConnectionImpl::~PeerConnectionImpl()
{
  peerconnections.erase(mHandle);
  Close();
  PR_DestroyLock(mLocalSourceStreamsLock);

  /* We should release mPCObserver on the main thread, but also prevent a double free.
  nsCOMPtr<nsIThread> mainThread;
  NS_GetMainThread(getter_AddRefs(mainThread));
  NS_ProxyRelease(mainThread, mPCObserver);
  */
}

// One level of indirection so we can use WrapRunnable in CreateMediaStream.
void
PeerConnectionImpl::MakeMediaStream(PRUint32 hint, nsIDOMMediaStream** retval)
{
  nsRefPtr<nsDOMMediaStream> stream = nsDOMMediaStream::CreateInputStream(hint);
  NS_ADDREF(*retval = stream);
  CSFLogDebug(logTag, "PeerConnection %p: Created media stream %p inner=%p",
              this, stream.get(), stream->GetStream());

  return;
}

void
PeerConnectionImpl::MakeRemoteSource(nsDOMMediaStream* stream, RemoteSourceStreamInfo** info)
{
  // TODO(ekr@rtfm.com): Add the track info with the first segment
  nsRefPtr<RemoteSourceStreamInfo> remote = new RemoteSourceStreamInfo(stream);
  NS_ADDREF(*info = remote);
  return;
}

void
PeerConnectionImpl::CreateRemoteSourceStreamInfo(PRUint32 hint, RemoteSourceStreamInfo** info)
{
  nsIDOMMediaStream* stream;

  if (!mThread || NS_IsMainThread()) {
    MakeMediaStream(hint, &stream);
  } else {
    mThread->Dispatch(WrapRunnable(
      this, &PeerConnectionImpl::MakeMediaStream, hint, &stream
    ), NS_DISPATCH_SYNC);
  }

  nsDOMMediaStream* comstream = static_cast<nsDOMMediaStream*>(stream);
  static_cast<mozilla::SourceMediaStream*>(comstream->GetStream())->SetPullEnabled(true);

  nsRefPtr<RemoteSourceStreamInfo> remote;
  if (!mThread || NS_IsMainThread()) {
    remote = new RemoteSourceStreamInfo(comstream);
    NS_ADDREF(*info = remote);
    return;
  }

  mThread->Dispatch(WrapRunnable(
    this, &PeerConnectionImpl::MakeRemoteSource, comstream, info
  ), NS_DISPATCH_SYNC);
  return;
}

NS_IMETHODIMP
PeerConnectionImpl::Initialize(IPeerConnectionObserver* observer, 
                               nsIDOMWindow* aWindow, 
                               nsIThread* thread) {
  if (!observer) {
    return NS_ERROR_FAILURE;
  }

  mThread = thread;
  mPCObserver = observer;

  mWindow = do_QueryInterface(aWindow);
  NS_ENSURE_STATE(mWindow);

  PeerConnectionCtx *pcctx = PeerConnectionCtx::GetInstance();

  if (!pcctx) {
    return NS_ERROR_FAILURE;
  }

  mCall = pcctx->createCall();
  if (!mCall.get()) {
    return NS_ERROR_FAILURE;
  }

  // Generate a random handle
  unsigned char handle_bin[4];
  PK11_GenerateRandom(handle_bin, sizeof(handle_bin));

  for (size_t i = 0; i<sizeof(handle_bin); i++) {
    char hex[3];
    snprintf(hex, 3, "%.2x", handle_bin[i]);
    mHandle += hex;
  }

  // TODO(ekr@rtfm.com): need some way to set not offerer later
  // Looks like a bug in the NrIceCtx API.
  mIceCtx = NrIceCtx::Create("PC:" + mHandle, true);
  mIceCtx->SignalGatheringCompleted.connect(this, &PeerConnectionImpl::IceGatheringCompleted);
  mIceCtx->SignalCompleted.connect(this, &PeerConnectionImpl::IceCompleted);

  // Create two streams to start with, assume one for audio and
  // one for video
  mIceStreams.push_back(mIceCtx->CreateStream("stream1", 2));
  mIceStreams.push_back(mIceCtx->CreateStream("stream2", 2));

  for (std::size_t i=0; i<mIceStreams.size(); i++) {
    mIceStreams[i]->SignalReady.connect(this, &PeerConnectionImpl::IceStreamReady);
  }

  // Start gathering
  nsresult res;
  mIceCtx->thread()->Dispatch(WrapRunnableRet(
    mIceCtx, &NrIceCtx::StartGathering, &res), NS_DISPATCH_SYNC
  );
  PR_ASSERT(NS_SUCCEEDED(res));

  // Store under mHandle
  mCall->setPeerConnection(mHandle);
  peerconnections[mHandle] = this;

  // Create the DTLS Identity
  mIdentity = DtlsIdentity::Generate("self");
  
  // Busy-wait until we are ready
  // TODO(ekr@rtfm.com): This needs to be fixed with deferred operation
  // in PeerConnection.js
  while(PeerConnectionCtx::GetInstance()->sipcc_state() != kStarted) {
    PR_Sleep(100);
  }

  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::CreateFakeMediaStream(PRUint32 hint, nsIDOMMediaStream** retval)
{
  bool mute = false;

  // Hack to allow you to mute the stream
  if (hint & 0x80) {
    mute = true;
    hint &= ~0x80;
  }

  if (!mThread || NS_IsMainThread()) {
    MakeMediaStream(hint, retval);
  } else {
    mThread->Dispatch(WrapRunnable(
      this, &PeerConnectionImpl::MakeMediaStream, hint, retval
    ), NS_DISPATCH_SYNC);
  }

  if (!mute) {
    if (hint & nsDOMMediaStream::HINT_CONTENTS_AUDIO) {
      new Fake_AudioGenerator(static_cast<nsDOMMediaStream*>(*retval));
    } else {
  #ifdef MOZILLA_INTERNAL_API
      new Fake_VideoGenerator(static_cast<nsDOMMediaStream*>(*retval));
#endif
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::CreateDataChannel(nsIDOMDataChannel** aRetval)
{
#ifdef MOZILLA_INTERNAL_API
  mozilla::DataChannel *aDataChannel;

  if (!mDataConnection) {
    mDataConnection = new mozilla::DataChannelConnection(this);
    NS_ENSURE_TRUE(mDataConnection,NS_ERROR_FAILURE);
    mDataConnection->Init(0); // XXX FIX port value
  }
  aDataChannel = mDataConnection->Open(/* "",  */
                                       mozilla::DataChannelConnection::RELIABLE,
                                       true, 0, NULL, NULL);
  if (!aDataChannel)
    return NS_ERROR_FAILURE;

  std::cerr << "PeerConnectionImpl::making DOMDataChannel" << std::endl;
  return NS_NewDOMDataChannel(aDataChannel,
                              mWindow, /* GetOwner(), */
                              aRetval);
#else
  return NS_OK;
#endif
}


NS_IMETHODIMP
PeerConnectionImpl::Listen(unsigned short port)
{
  std::cerr << "PeerConnectionImpl::Listen()" << std::endl;
#ifdef MOZILLA_INTERNAL_API
  if (!mDataConnection) {
    mDataConnection = new mozilla::DataChannelConnection(this);
    mDataConnection->Init(port);
  }
  
  listenPort = port;
  PR_CreateThread(
    PR_SYSTEM_THREAD,
    PeerConnectionImpl::ListenThread, this,
    PR_PRIORITY_NORMAL,
    PR_GLOBAL_THREAD,
    PR_JOINABLE_THREAD, 0
  );

  std::cerr << "PeerConnectionImpl::Listen() returned" << std::endl;
#endif
  return NS_OK;
}

// XXX Temporary - remove
void
PeerConnectionImpl::ListenThread(void *data)
{
  sipcc::PeerConnectionImpl *ctx = static_cast<sipcc::PeerConnectionImpl*>(data);

#ifdef MOZILLA_INTERNAL_API
  ctx->mDataConnection->Listen(ctx->listenPort);
#endif
  std::cerr << "PeerConnectionImpl::ListenThread() finished" << std::endl;
}

// XXX Temporary - remove
NS_IMETHODIMP
PeerConnectionImpl::Connect(const nsAString &addr, unsigned short port)
{
  std::cerr << "PeerConnectionImpl::Connect()" << std::endl;
#ifdef MOZILLA_INTERNAL_API
  char *s = ToNewCString(addr);
  if (!mDataConnection) {
    mDataConnection = new mozilla::DataChannelConnection(this);
    mDataConnection->Init(port^1);
  }

  connectStr = s;
  connectPort = port;
  PR_CreateThread(
    PR_SYSTEM_THREAD,
    PeerConnectionImpl::ConnectThread, this,
    PR_PRIORITY_NORMAL,
    PR_GLOBAL_THREAD,
    PR_JOINABLE_THREAD, 0
  );

  std::cerr << "PeerConnectionImpl::Connect() returned" << std::endl;
#endif
  return NS_OK;
}

// XXX Temporary - remove
void
PeerConnectionImpl::ConnectThread(void *data)
{
  sipcc::PeerConnectionImpl *ctx = static_cast<sipcc::PeerConnectionImpl*>(data);

#ifdef MOZILLA_INTERNAL_API
  ctx->mDataConnection->Connect(ctx->connectStr,ctx->connectPort);
#endif
  std::cerr << "PeerConnectionImpl::ConnectThread() finished" << std::endl;
}

void
PeerConnectionImpl::NotifyConnection()
{
  MOZ_ASSERT(NS_IsMainThread());

  std::cerr << "PeerConnectionImpl:: got NotifyConnection" << std::endl;

#ifdef MOZILLA_INTERNAL_API
  if (mPCObserver) {
    PeerConnectionObserverDispatch* runnable =
      new PeerConnectionObserverDispatch(PC_OBSERVER_CONNECTION, nsnull,
                                         this, mPCObserver);
    if (mThread) {
      mThread->Dispatch(runnable, NS_DISPATCH_NORMAL);
      return;
    }
    runnable->Run();
  }
#endif
}

void
PeerConnectionImpl::NotifyClosedConnection()
{
  MOZ_ASSERT(NS_IsMainThread());

  std::cerr << "PeerConnectionImpl:: got NotifyClosedConnection" << std::endl;

#ifdef MOZILLA_INTERNAL_API
  if (mPCObserver) {
    PeerConnectionObserverDispatch* runnable =
      new PeerConnectionObserverDispatch(PC_OBSERVER_CLOSEDCONNECTION, nsnull,
                                         this, mPCObserver);
    if (mThread) {
      mThread->Dispatch(runnable, NS_DISPATCH_NORMAL);
      return;
    }
    runnable->Run();
  }
#endif
}

void
PeerConnectionImpl::NotifyDataChannel(mozilla::DataChannel *channel)
{
  std::cerr << "PeerConnectionImpl:: got NotifyDataChannel" << std::endl;

#ifdef MOZILLA_INTERNAL_API
  nsCOMPtr<nsIDOMDataChannel> domchannel;
  nsresult rv = NS_NewDOMDataChannel(channel, mWindow /*GetOwner()*/,
                                     getter_AddRefs(domchannel));
  NS_ENSURE_SUCCESS(rv, /**/);

  if (mPCObserver) {
    PeerConnectionObserverDispatch* runnable =
      new PeerConnectionObserverDispatch(PC_OBSERVER_DATACHANNEL, domchannel.get(),
                                         this, mPCObserver);
    if (mThread) {
      mThread->Dispatch(runnable, NS_DISPATCH_NORMAL);
      return;
    }
    runnable->Run();
  }
#endif
}

/*
 * CC_SDP_DIRECTION_SENDRECV will not be used when Constraints are implemented
 */
NS_IMETHODIMP
PeerConnectionImpl::CreateOffer(const char* hints) {
  CheckApiState();
  mRole = kRoleOfferer;  // TODO(ekr@rtfm.com): Interrogate SIPCC here?
  mCall->createOffer(hints);
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::CreateAnswer(const char* hints, const char* offer) {
  CheckApiState();
  mRole = kRoleAnswerer;  // TODO(ekr@rtfm.com): Interrogate SIPCC here?
  mCall->createAnswer(hints, offer);
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::SetLocalDescription(PRInt32 action, const char* sdp) {
  CheckApiState();
  mLocalRequestedSDP = sdp;
  mCall->setLocalDescription((cc_jsep_action_t)action, mLocalRequestedSDP);
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::SetRemoteDescription(PRInt32 action, const char* sdp) {
  CheckApiState();
  mRemoteRequestedSDP = sdp;
  mCall->setRemoteDescription((cc_jsep_action_t)action, mRemoteRequestedSDP);
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::AddStream(nsIDOMMediaStream* aMediaStream)
{
  nsDOMMediaStream* stream = static_cast<nsDOMMediaStream*>(aMediaStream);

  CSFLogDebug(logTag, "AddStream");

  // TODO(ekr@rtfm.com): Remove these asserts?
  // Adding tracks here based on nsDOMMediaStream expectation settings
  PRUint32 hints = stream->GetHintContents();

  if (!(hints & (nsDOMMediaStream::HINT_CONTENTS_AUDIO |
        nsDOMMediaStream::HINT_CONTENTS_VIDEO))) {
    CSFLogError(logTag, "Stream must contain either audio or video");
    return NS_ERROR_FAILURE;
  }

  // Now see if we already have a stream of this type, since we only
  // allow one of each.
  // TODO(ekr@rtfm.com): remove this when multiple of each stream
  // is allowed
  PR_Lock(mLocalSourceStreamsLock);
  for (unsigned u = 0; u < mLocalSourceStreams.Length(); u++) {
    nsRefPtr<LocalSourceStreamInfo> localSourceStream = mLocalSourceStreams[u];

    if (localSourceStream->GetMediaStream()->GetHintContents() & hints) {
      CSFLogError(logTag, "Only one stream of any given type allowed");
      PR_Unlock(mLocalSourceStreamsLock);
      PR_ASSERT(PR_FALSE);
      return NS_ERROR_FAILURE;
    }
  }

  // OK, we're good to add
  nsRefPtr<LocalSourceStreamInfo> localSourceStream =
    new LocalSourceStreamInfo(stream);
  cc_media_track_id_t media_stream_id = mLocalSourceStreams.Length();

  // TODO(ekr@rtfm.com): these integers should be the track IDs
  if (hints & nsDOMMediaStream::HINT_CONTENTS_AUDIO) {
    localSourceStream->ExpectAudio();
    mCall->addStream(media_stream_id, 0, AUDIO);
  }

  if (hints & nsDOMMediaStream::HINT_CONTENTS_VIDEO) {
    localSourceStream->ExpectVideo();
    mCall->addStream(media_stream_id, 1, VIDEO);
  }

  // Make it the listener for info from the MediaStream and add it to the list
  mozilla::MediaStream *plainMediaStream = stream->GetStream();

  if (plainMediaStream) {
    plainMediaStream->AddListener(localSourceStream);
  }

  mLocalSourceStreams.AppendElement(localSourceStream);

  PR_Unlock(mLocalSourceStreamsLock);
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::RemoveStream(nsIDOMMediaStream* aMediaStream)
{
  nsDOMMediaStream* stream = static_cast<nsDOMMediaStream*>(aMediaStream);
  CSFLogDebug(logTag, "RemoveStream");

  PR_Lock(mLocalSourceStreamsLock);
  for (unsigned u = 0; u < mLocalSourceStreams.Length(); u++) {
    nsRefPtr<LocalSourceStreamInfo> localSourceStream = mLocalSourceStreams[u];
    if (localSourceStream->GetMediaStream() == stream) {
      PRUint32 hints = stream->GetHintContents();
      if (hints & nsDOMMediaStream::HINT_CONTENTS_AUDIO) {
        // <emannion>  This API will change when we implement multiple streams
        //             It will only need the ID
        mCall->removeStream(u, 0, AUDIO);
      }
      if (hints & nsDOMMediaStream::HINT_CONTENTS_VIDEO) {
        mCall->removeStream(u, 1, VIDEO);
      }
      break;
    }
  }

  PR_Unlock(mLocalSourceStreamsLock);
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::CloseStreams() {
  if (mReadyState != PeerConnectionImpl::kClosed)  {
    ChangeReadyState(PeerConnectionImpl::kClosing);
  }

  mCall->endCall();
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::AddIceCandidate(const char* strCandidate)
{
  mCall->addIceCandidate(strCandidate);
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::GetFingerprint(char** fingerprint)
{
  if (!mIdentity) {
    return NS_ERROR_FAILURE;
  }

  char* tmp = new char[mFingerprint.size() + 1];
  std::copy(mFingerprint.begin(), mFingerprint.end(), tmp);
  tmp[mFingerprint.size()] = '\0';

  *fingerprint = tmp;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::GetLocalDescription(char** sdp)
{
  char* tmp = new char[mLocalSDP.size() + 1];
  std::copy(mLocalSDP.begin(), mLocalSDP.end(), tmp);
  tmp[mLocalSDP.size()] = '\0';

  *sdp = tmp;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::GetRemoteDescription(char** sdp)
{
  char* tmp = new char[mRemoteSDP.size() + 1];
  std::copy(mRemoteSDP.begin(), mRemoteSDP.end(), tmp);
  tmp[mRemoteSDP.size()] = '\0';

  *sdp = tmp;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::GetReadyState(PRUint32* state)
{
  *state = mReadyState;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::GetSipccState(PRUint32* state)
{
  PeerConnectionCtx* pcctx = PeerConnectionCtx::GetInstance();
  *state = pcctx ? pcctx->sipcc_state() : kIdle;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::GetIceState(PRUint32* state)
{
  *state = mIceState;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::Close()
{
  mCall->endCall();
#ifdef MOZILLA_INTERNAL_API
  mDataConnection->CloseAll();
#endif
  // DataConnection will need to stay alive until all threads/runnables exit
  return NS_OK;
}

void
PeerConnectionImpl::Shutdown()
{
  PeerConnectionCtx::Destroy();
}

void
PeerConnectionImpl::onCallEvent(ccapi_call_event_e callEvent,
  CSF::CC_CallPtr call, CSF::CC_CallInfoPtr info)
{
  cc_call_state_t state = info->getCallState();
  std::string statestr = info->callStateToString(state);

  if (CCAPI_CALL_EV_CREATED != callEvent && CCAPI_CALL_EV_STATE != callEvent) {
    CSFLogDebugS(logTag, ": **** CALL HANDLE IS: " << mHandle <<
      ": **** CALL STATE IS: " << statestr);
    return;
  }

  switch (state) {
    case SETLOCALDESC:
      mLocalSDP = mLocalRequestedSDP;
      break;
    case SETREMOTEDESC:
      mRemoteSDP = mRemoteRequestedSDP;
      break;
    default:
      break;
  }

  if (mPCObserver) {
    PeerConnectionObserverDispatch* runnable =
        new PeerConnectionObserverDispatch(info, this, mPCObserver);

    if (mThread) {
      mThread->Dispatch(runnable, NS_DISPATCH_NORMAL);
      return;
    }
    runnable->Run();
  }
}

void
PeerConnectionImpl::ChangeReadyState(PeerConnectionImpl::ReadyState ready_state)
{
  mReadyState = ready_state;
  // FIXME: Dispatch on main thread.
  if (mPCObserver) {
    mPCObserver->OnStateChange(IPeerConnectionObserver::kReadyState);
  }
}

PeerConnectionWrapper *PeerConnectionImpl::AcquireInstance(const std::string& handle)
{
  if (peerconnections.find(handle) == peerconnections.end()) {
    return NULL;
  }

  PeerConnectionImpl *impl = peerconnections[handle];
  impl->AddRef();

  return new PeerConnectionWrapper(impl);
}

void
PeerConnectionImpl::ReleaseInstance()
{
  Release();
}

const std::string&
PeerConnectionImpl::GetHandle()
{
  return mHandle;
}

void
PeerConnectionImpl::IceGatheringCompleted(NrIceCtx *ctx)
{
  CSFLogDebug(logTag, "ICE gathering complete");
  mIceState = kIceWaiting;

#ifdef MOZILLA_INTERNAL_API
  if (mPCObserver) {
    PeerConnectionObserverDispatch* runnable =
      new PeerConnectionObserverDispatch(PC_OBSERVER_ICE, this, mPCObserver);
    if (mThread) {
      mThread->Dispatch(runnable, NS_DISPATCH_NORMAL);
      return;
    }
    runnable->Run();
  }
#endif
}

void
PeerConnectionImpl::IceCompleted(NrIceCtx *ctx)
{
  CSFLogDebug(logTag, "ICE completed");
  mIceState = kIceConnected;
  
#ifdef MOZILLA_INTERNAL_API
  if (mPCObserver) {
    PeerConnectionObserverDispatch* runnable =
      new PeerConnectionObserverDispatch(PC_OBSERVER_ICE, this, mPCObserver);
    if (mThread) {
      mThread->Dispatch(runnable, NS_DISPATCH_NORMAL);
      return;
    }
    runnable->Run();
  }
#endif
}

void
PeerConnectionImpl::IceStreamReady(NrIceMediaStream *stream)
{
  CSFLogDebug(logTag, "ICE stream ready : %s", stream->name().c_str());
}

nsRefPtr<LocalSourceStreamInfo>
PeerConnectionImpl::GetLocalStream(int index)
{
  if (index >= (int) mLocalSourceStreams.Length())
    return NULL;

  PR_ASSERT(mLocalSourceStreams[index]);
  return mLocalSourceStreams[index];
}

nsRefPtr<RemoteSourceStreamInfo>
PeerConnectionImpl::GetRemoteStream(int index)
{
  if (index >= (int) mRemoteSourceStreams.Length())
    return NULL;

  PR_ASSERT(mRemoteSourceStreams[index]);
  return mRemoteSourceStreams[index];
}

nsresult
PeerConnectionImpl::AddRemoteStream(nsRefPtr<RemoteSourceStreamInfo> info,
  int *index)
{
  *index = mRemoteSourceStreams.Length();

  mRemoteSourceStreams.AppendElement(info);

  return NS_OK;
}

void
LocalSourceStreamInfo::StorePipeline(int track,
  mozilla::RefPtr<mozilla::MediaPipeline> pipeline)
{
  PR_ASSERT(mPipelines.find(track) == mPipelines.end());
  if (mPipelines.find(track) != mPipelines.end()) {
    CSFLogDebug(logTag, "Storing duplicate track");
    return;
  }
  mPipelines[track] = pipeline;
}

void 
RemoteSourceStreamInfo::StorePipeline(int track,
  mozilla::RefPtr<mozilla::MediaPipeline> pipeline)
{
  PR_ASSERT(mPipelines.find(track) == mPipelines.end());
  if (mPipelines.find(track) != mPipelines.end()) {
    CSFLogDebug(logTag, "Storing duplicate track");
    return;
  }

  mPipelines[track] = pipeline;
}

}  // end sipcc namespace
