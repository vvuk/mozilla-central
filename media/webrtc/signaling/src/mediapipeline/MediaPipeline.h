/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef mediapipeline_h__
#define mediapipeline_h__

#include <talk/base/sigslot.h>

#ifdef USE_FAKE_MEDIA_STREAMS
#include "FakeMediaStreams.h"
#else
#include "nsDOMMediaStream.h"
#endif
#include "MediaConduitInterface.h"
#include "AudioSegment.h"
#include "VideoSegment.h"
#include "transportflow.h"

namespace mozilla {

// A class that represents the pipeline of audio and video
// The dataflow looks like:
//
// TRANSMIT
// CaptureDevice -> stream -> [us] -> conduit -> [us] -> transport -> network
//
// RECEIVE
// network -> transport -> [us] -> conduit -> [us] -> stream -> Playout 
//
// The boxes labeled [us] are just bridge logic implemented in this class
class MediaPipeline {
 public:
  enum Direction { TRANSMIT, RECEIVE };

  MediaPipeline(Direction direction,
                nsRefPtr<nsDOMMediaStream> stream,
                RefPtr<MediaSessionConduit> conduit,
                mozilla::RefPtr<TransportFlow> rtp_transport,
                mozilla::RefPtr<TransportFlow> rtcp_transport) :
      direction_(direction),
      stream_(stream),
      conduit_(conduit),
      rtp_transport_(rtp_transport),
      rtcp_transport_(rtcp_transport) {
  }

  virtual ~MediaPipeline() {
  }
  
  virtual Direction direction() const { return direction_; }

  // Thread counting
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaPipeline);

 protected:
  Direction direction_;
  nsRefPtr<nsDOMMediaStream> stream_;
  RefPtr<MediaSessionConduit> conduit_;
  RefPtr<TransportFlow> rtp_transport_;
  RefPtr<TransportFlow> rtcp_transport_;
};


// A specialization of pipeline for reading from an input device
// and transmitting to the network.
class MediaPipelineTransmit : public MediaPipeline {
 public: 
  MediaPipelineTransmit(nsRefPtr<nsDOMMediaStream> stream, 
                        RefPtr<MediaSessionConduit> conduit,
                        mozilla::RefPtr<TransportFlow> rtp_transport,
                        mozilla::RefPtr<TransportFlow> rtcp_transport) :
      MediaPipeline(TRANSMIT, stream, conduit, rtp_transport, rtcp_transport),
      transport_(new PipelineTransport(this)),
      listener_(new PipelineListener(this)) {
    Init();  // TODO(ekr@rtfm.com): ignoring error
  }

  // Initialize (stuff here may fail)
  nsresult Init();

  virtual ~MediaPipelineTransmit() {
    stream_->GetStream()->RemoveListener(listener_);

    // These shouldn't be necessary, but just to make sure
    // that if we have messed up ownership somehow the
    // interfaces just abort.
    listener_->Detach();
    transport_->Detach();
  }

  // Separate class to allow ref counting
  class PipelineTransport : public TransportInterface {
   public:
    // Implement the TransportInterface functions
    PipelineTransport(MediaPipelineTransmit *pipeline) : 
        pipeline_(pipeline) {}
    void Detach() { pipeline_ = NULL; }

    virtual nsresult SendRtpPacket(const void* data, int len);
    virtual nsresult SendRtcpPacket(const void* data, int len);

   private:
    MediaPipelineTransmit *pipeline_;  // Raw pointer to avoid cycles
  };
  friend class PipelineTransport;

  // Separate class to allow ref counting
  class PipelineListener : public MediaStreamListener {
   public:
    PipelineListener(MediaPipelineTransmit *pipeline) :
        pipeline_(pipeline) {}
    void Detach() { pipeline_ = NULL; }

      
    // Implement MediaStreamListener
    virtual void NotifyQueuedTrackChanges(MediaStreamGraph* graph, TrackID tid,
                                          TrackRate rate,
                                          TrackTicks offset,
                                          PRUint32 events,
                                          const MediaSegment& queued_media);
    virtual void NotifyPull(MediaStreamGraph* aGraph, StreamTime aDesiredTime) {}

   private:
    MediaPipelineTransmit *pipeline_;  // Raw pointer to avoid cycles
  };
  friend class PipelineListener;

 private:
  virtual void ProcessAudioChunk(AudioSessionConduit *conduit, 
                                 TrackRate rate, mozilla::AudioChunk& chunk);
  virtual void ProcessVideoChunk(VideoSessionConduit *conduit, 
                                 TrackRate rate, mozilla::VideoChunk& chunk);
  virtual nsresult SendPacket(TransportFlow *flow, const void* data, int len);

  mozilla::RefPtr<PipelineTransport> transport_;
  mozilla::RefPtr<PipelineListener> listener_;
};


// A specialization of pipeline for reading from the network and
// rendering video.
class MediaPipelineReceive : public MediaPipeline,
                             public sigslot::has_slots<> {
 public: 
  MediaPipelineReceive(nsRefPtr<nsDOMMediaStream> stream, 
                       RefPtr<MediaSessionConduit> conduit,
                       mozilla::RefPtr<TransportFlow> rtp_transport,
                       mozilla::RefPtr<TransportFlow> rtcp_transport) :
      MediaPipeline(RECEIVE, stream, conduit, rtp_transport, rtcp_transport) {
    PR_ASSERT(rtp_transport_);

    if (rtcp_transport_) {
      // If we have un-muxed transport, connect separate methods
      rtp_transport_->SignalPacketReceived.connect(this,
                                                   &MediaPipelineReceive::
                                                   RtpPacketReceived);
      rtcp_transport_->SignalPacketReceived.connect(this,
                                                    &MediaPipelineReceive::
                                                    RtcpPacketReceived);
    } else {
      rtp_transport_->SignalPacketReceived.connect(this,
                                                   &MediaPipelineReceive::
                                                   PacketReceived);
    }
  }
  
 private:
  bool IsRtp(const unsigned char *data, size_t len);
  void RtpPacketReceived(TransportFlow *flow, const unsigned char *data, size_t len);
  void RtcpPacketReceived(TransportFlow *flow, const unsigned char *data, size_t len);
  void PacketReceived(TransportFlow *flow, const unsigned char *data, size_t len);
};


// A specialization of pipeline for reading from the network and
// rendering audio.
class MediaPipelineReceiveAudio : public MediaPipelineReceive {
 public: 
  MediaPipelineReceiveAudio(nsRefPtr<nsDOMMediaStream> stream,
                            RefPtr<AudioSessionConduit> conduit,
                            mozilla::RefPtr<TransportFlow> rtp_transport,
                            mozilla::RefPtr<TransportFlow> rtcp_transport) :
      MediaPipelineReceive(stream, conduit, rtp_transport, rtcp_transport),
      listener_(new PipelineListener(this)) {
    Init();
  }

  ~MediaPipelineReceiveAudio() {
    stream_->GetStream()->RemoveListener(listener_);
    listener_->Detach();
  }

 private:
  // Separate class to allow ref counting
  class PipelineListener : public MediaStreamListener {
   public:
    PipelineListener(MediaPipelineReceiveAudio *pipeline) :
        pipeline_(pipeline) {}
    void Detach() { pipeline_ = NULL; }

      
    // Implement MediaStreamListener
    virtual void NotifyQueuedTrackChanges(MediaStreamGraph* graph, TrackID tid,
                                          TrackRate rate,
                                          TrackTicks offset,
                                          PRUint32 events,
                                          const MediaSegment& queued_media) {}
    virtual void NotifyPull(MediaStreamGraph* aGraph, StreamTime aDesiredTime);

   private:
    MediaPipelineReceiveAudio *pipeline_;  // Raw pointer to avoid cycles
  };
  friend class PipelineListener;

  nsresult Init();

  mozilla::RefPtr<PipelineListener> listener_;
};


}  // end namespace
#endif
