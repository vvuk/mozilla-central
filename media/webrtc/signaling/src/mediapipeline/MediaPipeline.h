/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef mediapipeline_h__
#define mediapipeline_h__

#ifdef USE_FAKE_MEDIA_STREAMS
#include "FakeMediaStreams.h"
#else
#include "nsDOMMediaStream.h"
#endif
#include "MediaConduitInterface.h"
#include "AudioSegment.h"
#include "TransportFlow.h"

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
                nsRefPtr<nsDOMMediaStream>& stream,
                RefPtr<MediaSessionConduit>& conduit,
                TransportFlow* rtp_transport,
                TransportFlow* rtcp_transport) :
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
  TransportFlow *rtp_transport_;
  TransportFlow *rtcp_transport_;
};


class MediaPipelineTransmit : public MediaPipeline,
                              public MediaStreamListener,
                              public TransportInterface {
 public: 
  MediaPipelineTransmit(nsRefPtr<nsDOMMediaStream>& stream, 
                        RefPtr<MediaSessionConduit>& conduit,
                        TransportFlow* rtp_transport,
                        TransportFlow* rtcp_transport) :
      MediaPipeline(TRANSMIT, stream, conduit, rtp_transport, rtcp_transport),
      TransportInterface() {
    stream_->GetStream()->AddListener(this);

    // TODO(ekr@rtfm.com): check error code; move to an Init function?
    // TODO(ekr@rtfm.com): This creates a reference cycle. Move to
    // an internal class
    conduit->AttachTransport(this);
  }

  virtual ~MediaPipelineTransmit() {
    // TODO(ekr@rtfm.com): Race conditions?
    stream_->GetStream()->RemoveListener(this);
  }

  // Implement the TransportInterface functions
  virtual nsresult SendRtpPacket(const void* data, int len);
  virtual nsresult SendRtcpPacket(const void* data, int len);


  // Implement MediaStreamListener
  virtual void NotifyQueuedTrackChanges(MediaStreamGraph* graph, TrackID tid,
                                        TrackRate rate,
                                        TrackTicks offset,
                                        PRUint32 events,
                                        const MediaSegment& queued_media);

 private:
  virtual void ProcessAudioChunk(AudioSessionConduit *conduit, 
                                 TrackRate rate, mozilla::AudioChunk& chunk);

  virtual nsresult SendPacket(TransportFlow *flow, const void* data, int len);
};

}  // end namespace
#endif
