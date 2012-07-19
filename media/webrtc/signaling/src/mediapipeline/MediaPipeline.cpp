/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "MediaPipeline.h"

#include <prlog.h>

#include "logging.h"
#include "nsError.h"
#include "AudioSegment.h"
#include "MediaSegment.h"

// Logging context
MLOG_INIT("mediapipeline");


namespace mozilla {

nsresult MediaPipelineTransmit::SendRtpPacket(const void *data, int len) {
  PR_ASSERT(rtp_transport_);
  if (!rtp_transport_) {
    MLOG(PR_LOG_DEBUG, "Couldn't write RTP packet (null flow)");
    return NS_ERROR_NULL_POINTER;
  }

  return SendPacket(rtp_transport_, data, len);
}

nsresult MediaPipelineTransmit::SendRtcpPacket(const void *data, int len) {
  PR_ASSERT(rtcp_transport_);
  if (!rtp_transport_) {
    MLOG(PR_LOG_DEBUG, "Couldn't write RTCP packet (null flow)");
    return NS_ERROR_NULL_POINTER;
  }

  return SendPacket(rtcp_transport_, data, len);
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

void MediaPipelineTransmit::
NotifyQueuedTrackChanges(MediaStreamGraph* graph, TrackID tid,
                         TrackRate rate,
                         TrackTicks offset,
                         PRUint32 events,
                         const MediaSegment& queued_media) {
  // TODO(ekr@rtfm.com): For now assume that we have only one
  // track type and it's destined for us
  if (queued_media.GetType() == MediaSegment::AUDIO) {
    AudioSegment* audio = const_cast<AudioSegment *>(static_cast<const AudioSegment *>(&queued_media));
    AudioSegment::ChunkIterator iter(*audio);
    
    while(iter.IsEnded()) {
      //      AudioChunk& chunk = *iter;

    }
  } else if (queued_media.GetType() == MediaSegment::VIDEO) {
    // TODO(ekr@rtfm.com): Implement VIDEO
  } else {
    // Ignore
  }
}

}  // end namespace
