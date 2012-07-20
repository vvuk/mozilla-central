/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "MediaPipeline.h"

#include "nspr.h"
#include <prlog.h>

#include "logging.h"
#include "nsError.h"
#include "AudioSegment.h"
#include "MediaSegment.h"

// Logging context
MLOG_INIT("mediapipeline");


namespace mozilla {

nsresult MediaPipelineTransmit::Init() {
  // TODO(ekr@rtfm.com): Check for errors
  stream_->GetStream()->AddListener(listener_);
  conduit_->AttachTransport(transport_);

  return NS_OK;
}

nsresult MediaPipelineTransmit::PipelineTransport::SendRtpPacket(
    const void *data, int len) {
  if (!pipeline_)
    return NS_OK;  // Detached

  PR_ASSERT(pipeline_->rtp_transport_);
  if (!pipeline_->rtp_transport_) {
    MLOG(PR_LOG_DEBUG, "Couldn't write RTP packet (null flow)");
    return NS_ERROR_NULL_POINTER;
  }
  
  return pipeline_->SendPacket(pipeline_->rtp_transport_, data, len);
}

nsresult MediaPipelineTransmit::PipelineTransport::SendRtcpPacket(
    const void *data, int len) {
  if (!pipeline_)
    return NS_OK;  // Detached

  if (!pipeline_->rtp_transport_) {
    MLOG(PR_LOG_DEBUG, "Couldn't write RTCP packet (null flow)");
    return NS_ERROR_NULL_POINTER;
  }

  return pipeline_->SendPacket(pipeline_->rtcp_transport_, data, len);
}

nsresult MediaPipelineTransmit::SendPacket(TransportFlow *flow, const void *data,
                                           int len) {
  TransportResult res = flow->SendPacket(static_cast<const unsigned char *>(data), len);
  
  if (res != len) {
    // Ignore blocking indications
    if (res == TE_WOULDBLOCK)
      return NS_OK;

    MLOG(PR_LOG_ERROR, "Failed write on stream");
    return NS_BASE_STREAM_CLOSED;
  }

  return NS_OK;
}

void MediaPipelineTransmit::PipelineListener::
NotifyQueuedTrackChanges(MediaStreamGraph* graph, TrackID tid,
                         TrackRate rate,
                         TrackTicks offset,
                         PRUint32 events,
                         const MediaSegment& queued_media) {
  if (!pipeline_)
    return;  // Detached

  MLOG(PR_LOG_DEBUG, "MediaPipeline::NotifyQueuedTrackChanges()");
  // TODO(ekr@rtfm.com): For now assume that we have only one
  // track type and it's destined for us
  if (queued_media.GetType() == MediaSegment::AUDIO) {
    if (pipeline_->conduit_->type() != MediaSessionConduit::AUDIO) {
      // TODO(ekr): How do we handle muxed audio and video streams
      MLOG(PR_LOG_ERROR, "Audio data provided for a video pipeline");
      return;
    }
    AudioSegment* audio = const_cast<AudioSegment *>(
        static_cast<const AudioSegment *>(&queued_media));

    AudioSegment::ChunkIterator iter(*audio);
    while(!iter.IsEnded()) {
      pipeline_->ProcessAudioChunk(static_cast<AudioSessionConduit *>
                                   (pipeline_->conduit_.get()),
                                   rate, *iter);
      iter.Next();
    }
  } else if (queued_media.GetType() == MediaSegment::VIDEO) {
    // TODO(ekr@rtfm.com): Implement VIDEO
  } else {
    // Ignore
  }
}

void MediaPipelineTransmit::ProcessAudioChunk(AudioSessionConduit *conduit,
                                              TrackRate rate,
                                              AudioChunk& chunk) {
  // TODO(ekr@rtfm.com): Do more than one channel
  nsAutoArrayPtr<int16_t> samples(new int16_t[chunk.mDuration]);

  if (chunk.mBuffer) { 
    switch(chunk.mBufferFormat) {
      case nsAudioStream::FORMAT_U8:
      case nsAudioStream::FORMAT_FLOAT32:
        MLOG(PR_LOG_ERROR, "Can't process audio exceptin 16-bit PCM yet");
        PR_ASSERT(PR_FALSE);
        return;
        break;
      case nsAudioStream::FORMAT_S16_LE:
        {
          // Code based on nsAudioStream
          const short* buf = static_cast<const short *>(chunk.mBuffer->Data());
          nsAutoArrayPtr<int16_t> samples(new int16_t[chunk.mDuration]);

          PRInt32 volume = PRInt32((1 << 16) * chunk.mVolume);
          for (PRUint32 i = 0; i < chunk.mDuration; ++i) {
            int16_t s = buf[i];
#if defined(IS_BIG_ENDIAN)
            s = ((s & 0x00ff) << 8) | ((s & 0xff00) >> 8);
#endif
            samples[i] = short((PRInt32(s) * volume) >> 16);
          }
        }
        break; 
      default:
        PR_ASSERT(PR_FALSE);
        return;
        break;
    }
  } else {
    for (PRUint32 i = 0; i < chunk.mDuration; ++i) {
      samples[i] = 0;
    }
  }

  conduit->SendAudioFrame(samples.get(), chunk.mDuration, rate, 0);
}

}  // end namespace
