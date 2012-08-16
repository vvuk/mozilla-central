/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _PEER_CONNECTION_IMPL_H_
#define _PEER_CONNECTION_IMPL_H_

#include <string>
#include <vector>
#include <map>
#include <cmath>

#include "prlock.h"
#include "mozilla/RefPtr.h"
#include "IPeerConnection.h"
#include "nsComponentManagerUtils.h"
#include "nsPIDOMWindow.h"

#ifdef USE_FAKE_MEDIA_STREAMS
#include "FakeMediaStreams.h"
#else
#include "nsDOMMediaStream.h"
#endif

#include "dtlsidentity.h"
#include "nricectx.h"
#include "nricemediastream.h"

#include "peer_connection_types.h"
#include "CallControlManager.h"
#include "CC_Device.h"
#include "CC_Call.h"
#include "CC_Observer.h"
#include "MediaPipeline.h"


#ifdef MOZILLA_INTERNAL_API
#include "mozilla/net/DataChannel.h"
#include "Layers.h"
#include "VideoUtils.h"
#include "ImageLayers.h"
#include "VideoSegment.h"
#else
namespace mozilla {
  class DataChannel;
}
#endif

namespace sipcc {

/* Temporary for providing audio data */
class Fake_AudioGenerator {
 public:
Fake_AudioGenerator(nsDOMMediaStream* aStream) : mStream(aStream), mCount(0) {
    mTimer = do_CreateInstance("@mozilla.org/timer;1");
    PR_ASSERT(mTimer);

    // Make a track
    mozilla::AudioSegment *segment = new mozilla::AudioSegment();
    segment->Init(1); // 1 Channel
    mStream->GetStream()->AsSourceStream()->AddTrack(1, 16000, 0, segment);

    // Set the timer
    mTimer->InitWithFuncCallback(Callback, this, 100, nsITimer::TYPE_REPEATING_PRECISE);
  }

  static void Callback(nsITimer* timer, void *arg) {
    Fake_AudioGenerator* gen = static_cast<Fake_AudioGenerator*>(arg);

    nsRefPtr<mozilla::SharedBuffer> samples = mozilla::SharedBuffer::Create(4000);
    for (int i=0; i<1600; i++) {
      reinterpret_cast<int16_t *>(samples->Data())[i] = ((gen->mCount % 8) * 4000) - 16000;
      ++gen->mCount;
    }

    mozilla::AudioSegment segment;
    segment.Init(1);
    segment.AppendFrames(samples.forget(), 1600,
      0, 1600, nsAudioStream::FORMAT_S16_LE);

    gen->mStream->GetStream()->AsSourceStream()->AppendToTrack(1, &segment);
  }

 private:
  nsCOMPtr<nsITimer> mTimer;
  nsRefPtr<nsDOMMediaStream> mStream;
  int mCount;
};

/* Temporary for providing video data */
#ifdef MOZILLA_INTERNAL_API
class Fake_VideoGenerator {
 public:
  Fake_VideoGenerator(nsDOMMediaStream* aStream) {
    mStream = aStream;
    mCount = 0;
    mTimer = do_CreateInstance("@mozilla.org/timer;1");
    PR_ASSERT(mTimer);

    // Make a track
    mozilla::VideoSegment *segment = new mozilla::VideoSegment();
    mStream->GetStream()->AsSourceStream()->AddTrack(1, USECS_PER_S, 0, segment);
    mStream->GetStream()->AsSourceStream()->AdvanceKnownTracksTime(mozilla::STREAM_TIME_MAX);

    // Set the timer. Set to 10 fps.
    mTimer->InitWithFuncCallback(Callback, this, 100, nsITimer::TYPE_REPEATING_SLACK);
  }

  static void Callback(nsITimer* timer, void *arg) {
    Fake_VideoGenerator* gen = static_cast<Fake_VideoGenerator*>(arg);

    const PRUint32 WIDTH = 640;
    const PRUint32 HEIGHT = 480;

    // Allocate a single blank Image
    mozilla::layers::Image::Format format = mozilla::layers::Image::PLANAR_YCBCR;
    nsRefPtr<mozilla::layers::ImageContainer> container =
      mozilla::layers::LayerManager::CreateImageContainer();

    nsRefPtr<mozilla::layers::Image> image = container->CreateImage(&format, 1);

    int len = ((WIDTH * HEIGHT) * 3 / 2);
    mozilla::layers::PlanarYCbCrImage* planar =
      static_cast<mozilla::layers::PlanarYCbCrImage*>(image.get());
    PRUint8* frame = (PRUint8*) PR_Malloc(len);
    ++gen->mCount;
    memset(frame, (gen->mCount / 8) & 0xff, len); // Rotating colors

    const PRUint8 lumaBpp = 8;
    const PRUint8 chromaBpp = 4;

    mozilla::layers::PlanarYCbCrImage::Data data;
    data.mYChannel = frame;
    data.mYSize = gfxIntSize(WIDTH, HEIGHT);
    data.mYStride = WIDTH * lumaBpp / 8.0;
    data.mCbCrStride = WIDTH * chromaBpp / 8.0;
    data.mCbChannel = frame + HEIGHT * data.mYStride;
    data.mCrChannel = data.mCbChannel + HEIGHT * data.mCbCrStride / 2;
    data.mCbCrSize = gfxIntSize(WIDTH / 2, HEIGHT / 2);
    data.mPicX = 0;
    data.mPicY = 0;
    data.mPicSize = gfxIntSize(WIDTH, HEIGHT);
    data.mStereoMode = mozilla::layers::STEREO_MODE_MONO;

    // SetData copies data, so we can free the frame
    planar->SetData(data);
    PR_Free(frame);

    // AddTrack takes ownership of segment
    mozilla::VideoSegment *segment = new mozilla::VideoSegment();
    // 10 fps.
    segment->AppendFrame(image.forget(), USECS_PER_S / 10, gfxIntSize(WIDTH, HEIGHT));

    gen->mStream->GetStream()->AsSourceStream()->AppendToTrack(1, segment);
  }

 private:
  nsCOMPtr<nsITimer> mTimer;
  nsRefPtr<nsDOMMediaStream> mStream;
  int mCount;
};
#endif

class LocalSourceStreamInfo : public mozilla::MediaStreamListener {
public:
  LocalSourceStreamInfo(nsDOMMediaStream* aMediaStream)
    : mMediaStream(aMediaStream) {}
  ~LocalSourceStreamInfo() {}

  /**
   * Notify that changes to one of the stream tracks have been queued.
   * aTrackEvents can be any combination of TRACK_EVENT_CREATED and
   * TRACK_EVENT_ENDED. aQueuedMedia is the data being added to the track
   * at aTrackOffset (relative to the start of the stream).
   */
  virtual void NotifyQueuedTrackChanges(
    mozilla::MediaStreamGraph* aGraph,
    mozilla::TrackID aID,
    mozilla::TrackRate aTrackRate,
    mozilla::TrackTicks aTrackOffset,
    PRUint32 aTrackEvents,
    const mozilla::MediaSegment& aQueuedMedia
  );

  virtual void NotifyPull(mozilla::MediaStreamGraph* aGraph,
    mozilla::StreamTime aDesiredTime) {}

  nsDOMMediaStream* GetMediaStream();
  void StorePipeline(int track, mozilla::RefPtr<mozilla::MediaPipeline> pipeline);

  void ExpectAudio();
  void ExpectVideo();
  unsigned AudioTrackCount();
  unsigned VideoTrackCount();

private:
  std::map<int, mozilla::RefPtr<mozilla::MediaPipeline> > mPipelines;
  nsRefPtr<nsDOMMediaStream> mMediaStream;
  nsTArray<mozilla::TrackID> mAudioTracks;
  nsTArray<mozilla::TrackID> mVideoTracks;
};

class RemoteSourceStreamInfo {
 public:
  RemoteSourceStreamInfo(nsDOMMediaStream* aMediaStream) :
      mMediaStream(aMediaStream),
      mPipelines() {}

  nsDOMMediaStream* GetMediaStream();
  void StorePipeline(int track, mozilla::RefPtr<mozilla::MediaPipeline> pipeline);

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteSourceStreamInfo);
 private:
  nsRefPtr<nsDOMMediaStream> mMediaStream;
  std::map<int, mozilla::RefPtr<mozilla::MediaPipeline> > mPipelines;
};

class PeerConnectionWrapper;

class PeerConnectionImpl MOZ_FINAL : public IPeerConnection,
#ifdef MOZILLA_INTERNAL_API
                                     public mozilla::DataChannelConnection::DataConnectionListener,
#endif
                                     public sigslot::has_slots<> {
public:
  PeerConnectionImpl();
  ~PeerConnectionImpl();

  enum ReadyState {
    kNew,
    kNegotiating,
    kActive,
    kClosing,
    kClosed
  };

  enum SipccState {
    kIdle,
    kStarting,
    kStarted
  };

  // TODO(ekr@rtfm.com): make this conform to the specifications
  enum IceState {
    kIceGathering,
    kIceWaiting,
    kIceChecking,
    kIceConnected,
    kIceFailed
  };

  enum Role {
    kRoleUnknown,
    kRoleOfferer,
    kRoleAnswerer
  };

  NS_DECL_ISUPPORTS
  NS_DECL_IPEERCONNECTION

  static PeerConnectionImpl* CreatePeerConnection();
  static void Shutdown();

  Role GetRole() const { return mRole; }
  void MakeMediaStream(PRUint32 hint, nsIDOMMediaStream** stream);
  void MakeRemoteSource(nsDOMMediaStream* stream, RemoteSourceStreamInfo** info);
  void CreateRemoteSourceStreamInfo(PRUint32 hint, RemoteSourceStreamInfo** info);

  // Implementation of the only observer we need
  virtual void onCallEvent(
    ccapi_call_event_e callEvent,
    CSF::CC_CallPtr call,
    CSF::CC_CallInfoPtr info
  );

  // DataConnection observers
  void NotifyConnection();
  void NotifyClosedConnection();
  void NotifyDataChannel(mozilla::DataChannel *channel);

  // Handle system to allow weak references to be passed through C code
  static PeerConnectionWrapper *AcquireInstance(const std::string& handle);
  virtual void ReleaseInstance();
  virtual const std::string& GetHandle();

  // ICE events
  void IceGatheringCompleted(NrIceCtx *ctx);
  void IceCompleted(NrIceCtx *ctx);
  void IceStreamReady(NrIceMediaStream *stream);

  mozilla::RefPtr<NrIceCtx> ice_ctx() const { return mIceCtx; }
  mozilla::RefPtr<NrIceMediaStream> ice_media_stream(size_t i) const {
    // TODO(ekr@rtfm.com): If someone asks for a value that doesn't exist,
    // make one.
    if (i >= mIceStreams.size()) {
      return NULL;
    }
    return mIceStreams[i];
  }

  // Get a specific local stream
  nsRefPtr<LocalSourceStreamInfo> GetLocalStream(int index);

  // Get a specific remote stream
  nsRefPtr<RemoteSourceStreamInfo> GetRemoteStream(int index);

  // Add a remote stream. Returns the index in index
  nsresult AddRemoteStream(nsRefPtr<RemoteSourceStreamInfo> info, int *index);

  // Get a transport flow
  mozilla::RefPtr<TransportFlow> GetTransportFlow(int index, bool rtcp) {
    int index_inner = index * 2 + (rtcp ? 1 : 0);

    if (mTransportFlows.find(index_inner) == mTransportFlows.end())
      return NULL;

    return mTransportFlows[index_inner];
  }

  // Add a transport flow
  void AddTransportFlow(int index, bool rtcp, mozilla::RefPtr<TransportFlow> flow) {
    int index_inner = index * 2 + (rtcp ? 1 : 0);

    mTransportFlows[index_inner] = flow;
  }

  static void ListenThread(void *data);
  static void ConnectThread(void *data);

  // Get the main thread
  nsCOMPtr<nsIThread> GetMainThread() { return mThread; }

  // Get the DTLS identity
  mozilla::RefPtr<DtlsIdentity> const GetIdentity() { return mIdentity; }

private:
  PeerConnectionImpl(const PeerConnectionImpl&rhs);
  PeerConnectionImpl& operator=(PeerConnectionImpl);

  void ChangeReadyState(ReadyState ready_state);
  void CheckApiState() {
    PR_ASSERT(mIceState != kIceGathering);
  }

  // The role we are adopting
  Role mRole;

  // The call
  CSF::CC_CallPtr mCall;
  ReadyState mReadyState;


  nsCOMPtr<nsIThread> mThread;
  nsCOMPtr<IPeerConnectionObserver> mPCObserver;
  nsCOMPtr<nsPIDOMWindow> mWindow;

  // The SDP sent in from JS - here for debugging.
  std::string mLocalRequestedSDP;
  std::string mRemoteRequestedSDP;
  // The SDP we are using.
  std::string mLocalSDP;
  std::string mRemoteSDP;

  // DTLS fingerprint, fake it for now.
  std::string mFingerprint;

  // A list of streams returned from GetUserMedia
  PRLock *mLocalSourceStreamsLock;
  nsTArray<nsRefPtr<LocalSourceStreamInfo> > mLocalSourceStreams;

  // A list of streams provided by the other side
  PRLock *mRemoteSourceStreamsLock;
  nsTArray<nsRefPtr<RemoteSourceStreamInfo> > mRemoteSourceStreams;

  // A handle to refer to this PC with
  std::string mHandle;

  // ICE objects
  mozilla::RefPtr<NrIceCtx> mIceCtx;
  std::vector<mozilla::RefPtr<NrIceMediaStream> > mIceStreams;
  IceState mIceState;

  // Transport flows: even is RTP, odd is RTCP
  std::map<int, mozilla::RefPtr<TransportFlow> > mTransportFlows;

  // The DTLS identity
  mozilla::RefPtr<DtlsIdentity> mIdentity;

  nsCOMPtr<nsIEventTarget> mDTLSTarget;
#ifdef MOZILLA_INTERNAL_API
  // DataConnection that's used to get all the DataChannels
	nsAutoPtr<mozilla::DataChannelConnection> mDataConnection;
#endif

  // Singleton list of all the PeerConnections
  static std::map<const std::string, PeerConnectionImpl *> peerconnections;

public:

  unsigned short listenPort;
  unsigned short connectPort;
  char *connectStr; // XXX ownership/free
};

// This is what is returned when you acquire on a handle
class PeerConnectionWrapper {
 public:
  PeerConnectionWrapper(PeerConnectionImpl *impl) : impl_(impl) {}

  ~PeerConnectionWrapper() {
    if (impl_)
      impl_->ReleaseInstance();
  }

  PeerConnectionImpl *impl() { return impl_; }

 private:
  PeerConnectionImpl *impl_;
};

}  // end sipcc namespace

#endif  // _PEER_CONNECTION_IMPL_H_
