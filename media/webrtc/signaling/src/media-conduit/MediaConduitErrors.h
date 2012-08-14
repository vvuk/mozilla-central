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
kMediaConduitSessionNotInited = 10100, // Video Engine not yet initialized
kMediaConduitMalformedArgument,        // Malformed input to Conduit API
kMediaConduitCaptureError,             // WebRTC capture APIs failed
kMediaConduitInvalidSendCodec,         // Wrong Send codec
kMediaConduitInvalidReceiveCodec,      // Wrong Recv Codec
kMediaConduitCodecInUse,               // SetSendCodec --> Codec in use
kMediaConduitInvalidRenderer,          // NULL or Wrong Renderer object
kMediaConduitRendererFail,             // Add Render called multiple times
kMediaConduitSendingAlready,           // Engine already trasmitting
kMediaConduitReceivingAlready,         // Engine already receiving
kMediaConduitTransportRegistrationFail,// NULL or wrong transport interface 
kMediaConduitInvalidTransport,         // NULL or wrong transport interface 
kMediaConduitChannelError,             // Configuration Error
kMediaConduitSocketError,              // Media Engine transport socket error
kMediaConduitRTPRTCPModuleError,       // Couldn't start RTP/RTCP processing
kMediaConduitRTPProcessingFailed,      // Processing incoming RTP frame failed
kMediaConduitUnknownError,             // More information can be found in logs
kMediaConduitExternalRecordingError,   // Couldn't start external recording
kMediaConduitRecordingError,           // Runtime recording error 
kMediaConduitExternalPlayoutError,     // Couldn't start externla playout
kMediaConduitPlayoutError              // Runtime playout erorr
};

}

#endif

