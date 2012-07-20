/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VideoConduit.h"
#include "video_engine/include/vie_errors.h"
#include "CSFLog.h"

namespace mozilla {


static const char* logTag ="WebrtcVideoSessionConduit";

//Factory Implementation
mozilla::RefPtr<VideoSessionConduit> VideoSessionConduit::Create()
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__); 

  WebrtcVideoConduit* obj = new WebrtcVideoConduit();

  if(!obj)
  {
    return nsnull;
  }

  if(obj->Init() == kMediaConduitNoError)
  {
      return obj;
  } else
  {
    return NULL;
  }

}

WebrtcVideoConduit::~WebrtcVideoConduit()
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  //Deal with External Capturer   
  if(mPtrViECapture)
  {
    mPtrViECapture->DisconnectCaptureDevice(mCapId);   
    mPtrViECapture->ReleaseCaptureDevice(mCapId);   
    mPtrExtCapture = NULL;    
  }

  //Deal with External Renderer   
  if(mPtrViERender)
  {
    mPtrViERender->StopRender(mChannel);   
    mPtrViERender->RemoveRenderer(mChannel);    
  }

  //Deal with the transport   
  if(mPtrViENetwork)
  {
    mPtrViENetwork->DeregisterSendTransport(mChannel);   
  }

  if(mPtrViEBase)
  {
    mPtrViEBase->StopSend(mChannel);   
    mPtrViEBase->StopReceive(mChannel);   
    mPtrViEBase->DeleteChannel(mChannel);   
  }
}

MediaConduitErrorCode WebrtcVideoConduit::Init()
{

  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  
  if(!mVideoEngine) 
  {
     CSFLogError(logTag, " Unable to create video engine ");
     return kVideoConduitSessionNotInited;
  }

  
  CSFLogDebug(logTag, " Engine Created: Init'ng the interfaces "); 

  if(!mPtrViEBase || (mPtrViEBase->Init() == -1 ) )
  {
    CSFLogError(logTag, "Video Engine Init Failed %d ", 
                            mPtrViEBase->LastError());
    return kVideoConduitSessionNotInited;    
  }


  if(-1 == mPtrViEBase->CreateChannel(mChannel))
  {
    return kVideoConduitChannelError;
  }

  if(!mPtrViENetwork  ||
      (mPtrViENetwork->RegisterSendTransport(mChannel, *this) == -1 ) )
  {
    CSFLogError(logTag,  "ViENetwork Failed %d ", 
                          mPtrViEBase->LastError());
    return kVideoConduitTransportRegistrationFail;
  }


  mPtrExtCapture = 0;
  if(!mPtrViECapture ||
      (mPtrViECapture->AllocateExternalCaptureDevice(mCapId,
                                                     mPtrExtCapture) == -1 ) ) 
  {
    CSFLogError(logTag, "Unable to Allocate capture module: %d ",
                                     mPtrViEBase->LastError()); 
    return kVideoConduitCaptureError;
  }

  if(mPtrViECapture->ConnectCaptureDevice(mCapId,mChannel) == -1)
  {
    CSFLogError(logTag, "Unable to Connect capture module: %d ",
                                      mPtrViEBase->LastError()); 
    return kVideoConduitCaptureError;
  }

   if(!mPtrViERender  ||
       (mPtrViERender->AddRenderer(mChannel,  
                                   webrtc::kVideoI420, 
                                   (webrtc::ExternalRenderer*) this) == -1) )
  {
    CSFLogError(logTag, "Failed to added external renderer ");
    return kVideoConduitInvalidRenderer;
  }


  CSFLogError(logTag, "Initialization Done");  
  
  return kMediaConduitNoError;
}


MediaConduitErrorCode 
  WebrtcVideoConduit::AttachRenderer(mozilla::RefPtr<VideoRenderer> aVideoRenderer)
{
  
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  
  if(aVideoRenderer)
  {
    mRenderer = aVideoRenderer;
    if(!mEnginePlaying)
    {
      if(mPtrViERender->StartRender(mChannel) == -1)
      {
          CSFLogError(logTag, "Starting the Renderer Fail");
          return kVideoConduitRendererFail;
      }
      mEnginePlaying = true;
    } else
    {
       CSFLogError(logTag, "NULL Renderer");
       return kVideoConduitInvalidRenderer;
    }
  }
  return kMediaConduitNoError;
}

MediaConduitErrorCode 
  WebrtcVideoConduit::AttachTransport(mozilla::RefPtr<TransportInterface> aTransport)
{
 
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
 
  if(aTransport)
  {
    mTransport = aTransport;
  } else
  {
    return kVideoConduitInvalidTransport;
  }
  return kMediaConduitNoError;
}

MediaConduitErrorCode 
 WebrtcVideoConduit::ConfigureSendMediaCodec(const VideoCodecConfig* codecConfig)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  if(!codecConfig)
  {
    return kMediaConduitNoError;
  }


  int error = 0;
  webrtc::VideoCodec  video_codec;
  memset(&video_codec, 0, sizeof(webrtc::VideoCodec));
  for(int idx=0; idx < mPtrViECodec->NumberOfCodecs(); idx++)
  {
    std::string plName = video_codec.plName;
    if(0 == mPtrViECodec->GetCodec(idx, video_codec))
    {
    if(codecConfig->mName.compare(plName) == 0)
        {
      video_codec.width = codecConfig->mWidth;
      video_codec.height = codecConfig->mHeight;
      break;
    }
    }
  }

  if(mPtrViECodec->SetSendCodec(mChannel, video_codec) == -1)
  {
     error = mPtrViEBase->LastError();
     if(error == kViECodecInvalidCodec)           
     {
        return kVideoConduitInvalidSendCodec;
     } else if(error == kViECodecInUse)
     {
        return kVideoConduitCodecInUse;
     } 

     return kVideoConduitUnknownError;
     
  }

  if(mPtrViEBase->StartSend(mChannel) == -1)
  {
     error = mPtrViEBase->LastError();
     if(error ==  kViEBaseAlreadySending)
     {
       return kVideoConduitSendingAlready;
     }

     return kVideoConduitUnknownError;
  }

  return kMediaConduitNoError;
}

MediaConduitErrorCode 
WebrtcVideoConduit::ConfigureRecvMediaCodec(const VideoCodecConfig* codecConfig)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  if(!codecConfig)
  {
     return kMediaConduitNoError;
  }
  

  int error = 0;
  webrtc::VideoCodec  video_codec;

  memset(&video_codec, 0, sizeof(webrtc::VideoCodec));

  for(int idx=0; idx < mPtrViECodec->NumberOfCodecs(); idx++)
  {
    std::string plName = video_codec.plName;
    if(mPtrViECodec->GetCodec(idx, video_codec) == 0)
    {
      if(codecConfig->mName.compare(plName) == 0)
      {
        video_codec.width = codecConfig->mWidth;
        video_codec.height = codecConfig->mHeight;
        //Set the codec now
          if(mPtrViECodec->SetReceiveCodec(mChannel,video_codec) == -1)
          {
              error = mPtrViEBase->LastError();
              if(error == kViECodecInvalidCodec || error == kViECodecInUse)
              {
                return kVideoConduitInvalidReceiveCodec;
              }
            return kVideoConduitUnknownError;
          }
          break;
      }
    }
  }

  if(mPtrViEBase->StartReceive(mChannel) == -1)
  {
    error = mPtrViEBase->LastError();
    if(error == kViEBaseAlreadyReceiving)
    {
      return kVideoConduitReceivingAlready;
    }
   return kVideoConduitUnknownError;
  }

  return kMediaConduitNoError;
}

//TODO: Make this function's Interface and Implementation flexible
// of samples lenght and frequency.
// Currently it works only for 16-bit linear PCM audio
MediaConduitErrorCode 
  WebrtcVideoConduit::SendVideoFrame(unsigned char* video_frame,
                                      unsigned int video_frame_length,
                                      unsigned short width,
                                      unsigned short height,
                                      VideoType video_type,
                                      uint64_t capture_time)
{

  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

 
  if(!mEnginePlaying)
  {
    if(-1 == mPtrViERender->StartRender(mChannel))
    {
      CSFLogError(logTag,  "Start Render Failed ");
      return kVideoConduitRendererFail;
    }
    mEnginePlaying = true;
    mPtrViECodec->SendKeyFrame(mChannel);
  }

  //insert the frame to video engine
  if(mPtrExtCapture->IncomingFrame(video_frame, 
                                   video_frame_length,
                                   width, height,
                                   webrtc::kVideoI420,
                                   (unsigned long long)capture_time) == -1)
  {
    CSFLogError(logTag,  "IncomingFrame Failed %d ",mPtrViEBase->LastError());   
    return kVideoConduitCaptureError;
  }

  CSFLogError(logTag, " Inserted A Frame ");
  return kMediaConduitNoError;
}

// Transport Layer Callbacks 
MediaConduitErrorCode WebrtcVideoConduit::ReceivedRTPPacket(const void *data, int len)
{
  CSFLogError(logTag, "%s: Len %d ", __FUNCTION__, len);

  if(mEnginePlaying)
  {
    if(mPtrViENetwork->ReceivedRTPPacket(mChannel,data,len) == -1)
    {
      int error = mPtrViEBase->LastError();
      if(error >= kViERtpRtcpInvalidChannelId && error <= kViERtpRtcpRtcpDisabled)
      {
        return kVideoConduitRTPProcessingFailed;
      }
       return kVideoConduitRTPRTCPModuleError;
    }
  } else
  {
    CSFLogError(logTag, "ReceivedRTPPacket: Engine Error %d ",  len);
    return kVideoConduitSessionNotInited;
  }
  return kMediaConduitNoError;
}

MediaConduitErrorCode WebrtcVideoConduit::ReceivedRTCPPacket(const void *data, int len)
{
  CSFLogError(logTag, " %s ", __FUNCTION__);

  if(mEnginePlaying)
  {
    if(mPtrViENetwork->ReceivedRTCPPacket(mChannel,data,len) == -1)
    {
      int error = mPtrViEBase->LastError();
      if(error >= kViERtpRtcpInvalidChannelId && error <= kViERtpRtcpRtcpDisabled)
      {
        return kVideoConduitRTPProcessingFailed;
      }
       return kVideoConduitRTPRTCPModuleError;
    }
  } else
  {
    CSFLogError(logTag, "ReceivedRTCPPacket: Engine Error %d ", len);
    return kVideoConduitSessionNotInited;
  }
  return kMediaConduitNoError;
}

//WebRTC::RTP Callback Implementation
int WebrtcVideoConduit::SendPacket(int channel, const void* data, int len)
{
  CSFLogError(logTag, "%s len %d ", __FUNCTION__, len);

   if(mTransport)
   {

    if(mTransport->SendRtpPacket(data, len) != NS_OK)
    {
      return -1;
    }

   }
  // In the case of no-tansport and successfully transmitted RTP pakcet
  // we just return 0 to the engine.
  return 0;
}

int WebrtcVideoConduit::SendRTCPPacket(int channel, const void* data, int len)
{
  CSFLogError(logTag,  "%s : channel %d ", __FUNCTION__, channel);

   if(mTransport)
   {

    if(mTransport->SendRtcpPacket(data, len) != NS_OK)
    {
      return -1;
    }

   } 

  return 0;
}

// WebRTC::ExternalMedia Implementation
int WebrtcVideoConduit::FrameSizeChange(unsigned int width, 
                                        unsigned int height,
                                        unsigned int numStreams)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  if(mRenderer) 
  {
    mRenderer->FrameSizeChange(width, height, numStreams);
  } 
  
  return 0;
}

int WebrtcVideoConduit::DeliverFrame(unsigned char* buffer, 
                                     int buffer_size,
                                     uint32_t time_stamp, 
                                     int64_t render_time)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  if(mRenderer)
  {
    mRenderer->RenderVideoFrame(buffer, buffer_size, time_stamp, render_time);

  } 

  return 0;
}

}// end namespace

