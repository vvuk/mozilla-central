/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "MediaPipeline.h"

#include <math.h>

#include "nspr.h"
#include <prlog.h>

#include "ImageLayers.h"
#include "logging.h"
#include "nsError.h"
#include "AudioSegment.h"
#include "ImageLayers.h"
#include "MediaSegment.h"

#include "runnable_utils.h"

// Logging context
MLOG_INIT("mediapipeline");


namespace mozilla {

nsresult MediaPipelineTransmit::Init() {
  // TODO(ekr@rtfm.com): Check for errors
  if (main_thread_) {
    main_thread_->Dispatch(WrapRunnable(
      stream_->GetStream(), &mozilla::MediaStream::AddListener, listener_),
      NS_DISPATCH_SYNC);
  }
  else {
    stream_->GetStream()->AddListener(listener_);
  }
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
    if (pipeline_->conduit_->type() != MediaSessionConduit::VIDEO) {
      // TODO(ekr): How do we handle muxed video and video streams
      MLOG(PR_LOG_ERROR, "Video data provided for an audio pipeline");
      return;
    }
    VideoSegment* video = const_cast<VideoSegment *>(
        static_cast<const VideoSegment *>(&queued_media));

    VideoSegment::ChunkIterator iter(*video);
    while(!iter.IsEnded()) {
      pipeline_->ProcessVideoChunk(static_cast<VideoSessionConduit *>
                                   (pipeline_->conduit_.get()),
                                   rate, *iter);
      iter.Next();
    }
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


void MediaPipelineTransmit::ProcessVideoChunk(VideoSessionConduit *conduit,
                                              TrackRate rate,
                                              VideoChunk& chunk) {
#ifdef MOZILLA_INTERNAL_API
  // We now need to send the video frame to the other side
  mozilla::layers::Image *img = chunk.mFrame.GetImage();

  mozilla::layers::Image::Format format = img->GetFormat();

  if (format != mozilla::layers::Image::PLANAR_YCBCR) {
    MLOG(PR_LOG_ERROR, "Can't process non-YCBCR video");
    PR_ASSERT(PR_FALSE);
    return;
  }

  // Cast away constness b/c some of the accessors are non-const
  layers::PlanarYCbCrImage* yuv =
    const_cast<layers::PlanarYCbCrImage *>(
      static_cast<const layers::PlanarYCbCrImage *>(img));

  // TODO(ekr@rtfm.com): Is this really how we get the length?
  // It's the inverse of the code in MediaEngineDefault
  unsigned int length = ((yuv->GetSize().width * yuv->GetSize().height) * 3 / 2);

  // Big-time assumption here that this is all contiguous data coming
  // from Anant's version of gUM. This code here is an attempt to double-check
  // that
  PR_ASSERT(length == yuv->GetDataSize());
  if (length != yuv->GetDataSize())
    return;

  // OK, pass it on to the conduit
  // TODO(ekr@rtfm.com): Check return value
  conduit->SendVideoFrame(yuv->mBuffer.get(), yuv->GetDataSize(),
    yuv->GetSize().width, yuv->GetSize().height, mozilla::kVideoI420, 0);
#endif
}


void MediaPipelineReceive::RtpPacketReceived(TransportFlow *flow,
                                             const unsigned char *data,
                                             size_t len) {
  ++rtp_packets_received_;
  (void)conduit_->ReceivedRTPPacket(data, len);  // Ignore error codes
}

void MediaPipelineReceive::RtcpPacketReceived(TransportFlow *flow,
                                              const unsigned char *data,
                                              size_t len) {
  ++rtcp_packets_received_;
  (void)conduit_->ReceivedRTCPPacket(data, len);  // Ignore error codes
}

bool MediaPipelineReceive::IsRtp(const unsigned char *data, size_t len) {
  if (len < 2)
    return false;

  // TODO(ekr@rtfm.com): this needs updating in light of RFC5761
  if ((data[1] >= 200) && (data[1] <= 204))
    return false;

  return true;

}

void MediaPipelineReceive::PacketReceived(TransportFlow *flow,
                                          const unsigned char *data,
                                          size_t len) {
  if (IsRtp(data, len)) {
    RtpPacketReceived(flow, data, len);
  } else {
    RtcpPacketReceived(flow, data, len);
  }
}

nsresult MediaPipelineReceiveAudio::Init() {
  MLOG(PR_LOG_DEBUG, __FUNCTION__);
  if (main_thread_) {
    main_thread_->Dispatch(WrapRunnable(
      stream_->GetStream(), &mozilla::MediaStream::AddListener, listener_),
      NS_DISPATCH_SYNC);
  }
  else {
    stream_->GetStream()->AddListener(listener_);
  }

  return NS_OK;
}

void MediaPipelineReceiveAudio::PipelineListener::
NotifyPull(MediaStreamGraph* graph, StreamTime desired) {
  mozilla::SourceMediaStream *source =
    pipeline_->stream_->GetStream()->AsSourceStream();

  PR_ASSERT(source);
  if (!source) {
    MLOG(PR_LOG_ERROR, "NotifyPull() called from a non-SourceMediaStream");
    return;
  }

  double time_s = MediaTimeToSeconds(desired);

  // Clip the number of seconds asked for to 1 second
  if (time_s > 1) {
    time_s = 1.0f;
  }

  // Number of 10 ms samples we need
  int num_samples = floor((time_s / .01f) + .5);

  MLOG(PR_LOG_DEBUG, "Asking for " << num_samples << "sample from Audio Conduit");

  while (num_samples--) {
    // TODO(ekr@rtfm.com): Is there a way to avoid mallocating here?
    nsRefPtr<SharedBuffer> samples = SharedBuffer::Create(1000);
    int samples_length;

    mozilla::MediaConduitErrorCode err =
      static_cast<mozilla::AudioSessionConduit*>(pipeline_->conduit_.get())->GetAudioFrame(
        static_cast<int16_t *>(samples->Data()),
        16000,  // Sampling rate fixed at 16 kHz for now
        0,  // TODO(ekr@rtfm.com): better estimate of capture delay
        samples_length);

    if (err != mozilla::kMediaConduitNoError)
      return;

    MLOG(PR_LOG_DEBUG, "Audio conduit returned buffer of length " << samples_length);

    mozilla::AudioSegment segment;
    segment.Init(1);
    segment.AppendFrames(samples.forget(), samples_length,
      0, samples_length, nsAudioStream::FORMAT_S16_LE);

    char buf[32];
    snprintf(buf, 32, "%p", source);
    MLOG(PR_LOG_DEBUG, "Appended segments to stream " << buf);
    source->AppendToTrack(1,  // TODO(ekr@rtfm.com): Track ID
      &segment);
  }
}

nsresult MediaPipelineReceiveVideo::Init() {
  MLOG(PR_LOG_DEBUG, __FUNCTION__);

  static_cast<mozilla::VideoSessionConduit *>(conduit_.get())->
      AttachRenderer(renderer_);

  return NS_OK;
}

MediaPipelineReceiveVideo::PipelineRenderer::PipelineRenderer(
    MediaPipelineReceiveVideo *pipeline) :
    pipeline_(pipeline),
#ifdef MOZILLA_INTERNAL_API
    image_container_(mozilla::layers::LayerManager::CreateImageContainer()),
#endif
    width_(640), height_(480) {}

void MediaPipelineReceiveVideo::PipelineRenderer::RenderVideoFrame(
    const unsigned char* buffer,
    unsigned int buffer_size,
    uint32_t time_stamp,
    int64_t render_time) {
#ifdef MOZILLA_INTERNAL_API
  mozilla::SourceMediaStream *source =
    pipeline_->stream_->GetStream()->AsSourceStream();

  // Create a video frame and append it to the track.
  mozilla::layers::Image::Format format = mozilla::layers::Image::PLANAR_YCBCR;
  nsRefPtr<mozilla::layers::Image> image = image_container_->CreateImage(&format, 1);

  mozilla::layers::PlanarYCbCrImage* videoImage = static_cast<mozilla::layers::PlanarYCbCrImage*>(image.get());
  PRUint8* frame = const_cast<PRUint8*>(static_cast<const PRUint8*> (buffer));
  const PRUint8 lumaBpp = 8;
  const PRUint8 chromaBpp = 4;

  mozilla::layers::PlanarYCbCrImage::Data data;
  data.mYChannel = frame;
  data.mYSize = gfxIntSize(width_, height_);
  data.mYStride = width_ * lumaBpp/ 8;
  data.mCbCrStride = width_ * chromaBpp / 8;
  data.mCbChannel = frame + height_ * data.mYStride;
  data.mCrChannel = data.mCbChannel + height_ * data.mCbCrStride / 2;
  data.mCbCrSize = gfxIntSize(width_/ 2, height_/ 2);
  data.mPicX = 0;
  data.mPicY = 0;
  data.mPicSize = gfxIntSize(width_, height_);
  data.mStereoMode = mozilla::layers::STEREO_MODE_MONO;

  videoImage->SetData(data);

  VideoSegment segment;
  segment.AppendFrame(image.forget(), 1, gfxIntSize(width_, height_));
  source->AppendToTrack(1, &(segment));
#endif
}


}  // end namespace

