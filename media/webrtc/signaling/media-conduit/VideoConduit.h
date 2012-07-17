/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef VIDEO_SESSION_H_
#define VIDEO_SESSION_H_

#include "MediaConduitInterface.h"
#include "MediaEngineWrapper.h"



// Video Engine Includes
#include "common_types.h"
#include "video_engine/include/vie_base.h"
#include "video_engine/include/vie_capture.h"
#include "video_engine/include/vie_codec.h"
#include "video_engine/include/vie_render.h"
#include "video_engine/include/vie_network.h"
#include "video_engine/include/vie_file.h"

/** This file hosts several structures identifying different aspects
 * of a RTP Session.
 */

 using  webrtc::ViEBase;
 using  webrtc::ViENetwork;
 using  webrtc::ViECodec;
 using  webrtc::ViECapture;
 using  webrtc::ViERender;
 using  webrtc::ViEExternalCapture;


namespace mozilla {

/**
 * Concrete class for Video session. Hooks up  
 *  - media-source and target to external transport 
 */
class WebrtcVideoConduit  : public VideoSessionConduit      
                            ,public webrtc::Transport
                            ,public webrtc::ExternalRenderer
{

public:

  //VideoSessionConduit Implementation
  MediaConduitErrorCode AttachRenderer(mozilla::RefPtr<VideoRenderer> aVideoRenderer);

  virtual MediaConduitErrorCode ReceivedRTPPacket(const void *data, int len);
  virtual MediaConduitErrorCode ReceivedRTCPPacket(const void *data, int len);
  virtual MediaConduitErrorCode ConfigureSendMediaCodec(const VideoCodecConfig* codecInfo);
  virtual MediaConduitErrorCode ConfigureRecvMediaCodec(const VideoCodecConfig* codecInfo);
  virtual MediaConduitErrorCode AttachTransport(mozilla::RefPtr<TransportInterface> aTransport);
  virtual MediaConduitErrorCode SendVideoFrame(unsigned char* video_frame,
                                                unsigned int video_frame_length,
                                                unsigned short width,
                                                unsigned short height,
                                                VideoType video_type,
                                                uint64_t capture_time);
            
  

  // Webrtc transport implementation
  virtual int SendPacket(int channel, const void *data, int len) ;
  virtual int SendRTCPPacket(int channel, const void *data, int len) ;

  
   // ViEExternalRenderer implementation
  virtual int FrameSizeChange(unsigned int, unsigned int, unsigned int);

  virtual int DeliverFrame(unsigned char*,int, uint32_t , int64_t);


  WebrtcVideoConduit():
                      mVideoEngine(WebRTCEngineWrapper::Instance()->GetVideoEngine()),
                      mTransport(NULL),
                      mRenderer(NULL),
                      mPtrViEBase(ViEBase::GetInterface(mVideoEngine)),
                      mPtrViECapture(ViECapture::GetInterface(mVideoEngine)),
                      mPtrViECodec(ViECodec::GetInterface(mVideoEngine)),
                      mPtrViENetwork(ViENetwork::GetInterface(mVideoEngine)),
                      mPtrViERender(ViERender::GetInterface(mVideoEngine)),
                      mEnginePlaying(false),
                      mChannel(-1),
                      mCapId(-1),
                      sessionId(-1)                            
  {
  }


  virtual ~WebrtcVideoConduit() ;
 


  MediaConduitErrorCode Init();

private:

  //Copy and Assigment
  WebrtcVideoConduit(WebrtcVideoConduit const&) {}
  void operator=(WebrtcVideoConduit const&) { }

  webrtc::VideoEngine* mVideoEngine; 

  mozilla::RefPtr<TransportInterface> mTransport;
  mozilla::RefPtr<VideoRenderer> mRenderer; 

  ScopedCustomReleasePtr<webrtc::ViEBase> mPtrViEBase;
  ScopedCustomReleasePtr<webrtc::ViECapture> mPtrViECapture;
  ScopedCustomReleasePtr<webrtc::ViECodec> mPtrViECodec;
  ScopedCustomReleasePtr<webrtc::ViENetwork> mPtrViENetwork;
  ScopedCustomReleasePtr<webrtc::ViERender> mPtrViERender;
  webrtc::ViEExternalCapture*  mPtrExtCapture;

  bool mEnginePlaying;
  int mChannel;
  int mCapId;
  uint32_t sessionId; // this session - for book-keeping
};



} // end namespace

#define VIDEO_SESSION "WebrtcVideoConduit "

#endif
