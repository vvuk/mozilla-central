/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AUDIO_SESSION_H_
#define AUDIO_SESSION_H_

#include "MediaConduitInterface.h"
#include "MediaEngineWrapper.h"

// Audio Engine Includes
#include "common_types.h"
#include "voice_engine/main/interface/voe_base.h"
#include "voice_engine/main/interface/voe_volume_control.h"
#include "voice_engine/main/interface/voe_codec.h"
#include "voice_engine/main/interface/voe_file.h"
#include "voice_engine/main/interface/voe_network.h"
#include "voice_engine/main/interface/voe_external_media.h"

//Some WebRTC types for short notations
 using webrtc::VoEBase;
 using webrtc::VoENetwork;
 using webrtc::VoECodec;
 using webrtc::VoEExternalMedia;

/** This file hosts several structures identifying different aspects
 * of a RTP Session.
 */

namespace mozilla {

/**
 * Concrete class for Audio session. Hooks up  
 *  - media-source and target to external transport 
 */
class WebrtcAudioConduit  : public AudioSessionConduit			
	      		              , public webrtc::Transport
{

public:
  
  //AudioSessionConduit Implementation
 virtual MediaConduitErrorCode AttachRenderer(
                               mozilla::RefPtr<AudioRenderer> aAudioRenderer);

 virtual MediaConduitErrorCode ReceivedRTPPacket(const void *data, int len);
 virtual MediaConduitErrorCode ReceivedRTCPPacket(const void *data, int len);

 virtual MediaConduitErrorCode ConfigureSendMediaCodec(
                               const AudioCodecConfig* codecInfo);
 virtual MediaConduitErrorCode ConfigureRecvMediaCodec(
                               const AudioCodecConfig* codecInfo);

 virtual MediaConduitErrorCode AttachTransport(
                               mozilla::RefPtr<TransportInterface> aTransport);

 virtual MediaConduitErrorCode SendAudioFrame(const int16_t speechData[],
                                              unsigned int lengthSamples,
                                              uint32_t samplingFreqHz,
                                              uint64_t capture_time);

  // Pull based API to get audio sample from the jitter buffe
  virtual MediaConduitErrorCode GetAudioFrame(int16_t speechData[],
                                              uint32_t samplingFreqHz,
                                              uint64_t capture_delay,
                                              unsigned int& lengthSamples);

  
  // Webrtc transport implementation
  virtual int SendPacket(int channel, const void *data, int len) ;
  virtual int SendRTCPPacket(int channel, const void *data, int len) ;

	
	
  WebrtcAudioConduit():
                      mVoiceEngine(WebRTCEngineWrapper::Instance()->GetVoiceEngine()),
                      mTransport(NULL),
                      mRenderer(NULL),
                      mPtrVoENetwork(VoENetwork::GetInterface(mVoiceEngine)),
                      mPtrVoEBase(VoEBase::GetInterface(mVoiceEngine)),
                      mPtrVoECodec(VoECodec::GetInterface(mVoiceEngine)),
                      mPtrVoEXmedia(VoEExternalMedia::GetInterface(mVoiceEngine)),
                      mEnginePlaying(false),
                      mChannel(-1),
                      sessionId(-1)
  {
  }

  virtual ~WebrtcAudioConduit();
  
  MediaConduitErrorCode Init();
private:
  WebrtcAudioConduit(WebrtcAudioConduit const&) {}
  void operator=(WebrtcAudioConduit const&) { }


  webrtc::VoiceEngine* mVoiceEngine; 

  mozilla::RefPtr<TransportInterface> mTransport;
  mozilla::RefPtr<AudioRenderer> mRenderer; 

  //TODO: Crypt: Move to scoped_m_ptr version
  ScopedCustomReleasePtr<webrtc::VoENetwork>  mPtrVoENetwork;
  ScopedCustomReleasePtr<webrtc::VoEBase>     mPtrVoEBase;
  ScopedCustomReleasePtr<webrtc::VoECodec>    mPtrVoECodec;
  ScopedCustomReleasePtr<webrtc::VoEExternalMedia> mPtrVoEXmedia;
  bool mEnginePlaying;
  int mChannel;
  uint32_t sessionId; // this session - for book-keeping
};

} // end namespace

#define AUDIO_SESSION "WebrtcAudioConduit "

#endif
