/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
  * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MEDIA_SESSION_ERRORS_H_
#define MEDIA_SESSION_ERRORS_H_



namespace mozilla 
{
enum MediaConduitErrorCode
{
kMediaConduitNoError = 0,
//Video Session Related Errors
kVideoConduitSessionNotInited = 10100, // Video Engine not yet initialized
kVideConduitoMalformedArguement, 
kVideoConduitCaptureError,
kVideoConduitInvalidSendCodec,         // Wrong Send codec
kVideoConduitInvalidReceiveCodec,      // Wrong Recv Codec
kVideoConduitCodecInUse,               // SetSendCodec --> Codec in use
kVideoConduitInvalidRenderer,          // NULL or Wrong Renderer object
kVideoConduitRendererFail,             // Add Render called multiple times
kVideoConduitSendingAlready,
kVideoConduitReceivingAlready,
kVideoConduitTransportRegistrationFail,         // NULL or wrong transport interface 
kVideoConduitInvalidTransport,         // NULL or wrong transport interface 
kVideoConduitChannelError,             // Configuration Error
kVideoConduitRTPRTCPModuleError,       // Couldn't start RTP/RTCP processing
kVideoConduitRTPProcessingFailed,      // Processing incoming RTP frame failed
kVideoConduitUnknownError,             // not sure ..check logs

//Audio SessionRelated Errors
kAudioConduitSessionNotInited = 10200,
kAudioConduitMalformedArguement, 
kAudioConduitInvalidSendCodec,
kAudioConduitInvalidReceiveCodec,
kAudioConduitCodecInUse,
kAudioConduitChannelError,
kAudioConduitInvalidRenderer,
kAudioConduitRedererInUse,
kAudioConduitSendingAlready,
kAudioConduitSocketError,
kAudioConduitTransportRegistrationFail,
kAudioConduitInvalidTransport,
kAudioConduitRTPChannelError,
kAudioConduitRTCPChannelError,
kAudioConduitRTPRTCPModuleError,
kAudioConduitRTPProcessingFailed,
kAudioConduitExternalRecordingError,
kAudioConduitRecordingError,
kAudioConduitExternalPlayoutError,
kAudioConduitPlayoutError,
kAudioConduitUnknownError,


};

}

#endif

