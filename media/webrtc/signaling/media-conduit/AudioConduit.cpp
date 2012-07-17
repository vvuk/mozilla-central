/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioConduit.h"
#include "CSFLog.h"
#include "voice_engine/main/interface/voe_errors.h"


namespace mozilla {

static const char* logTag ="WebrtcAudioSessionConduit";


mozilla::RefPtr<AudioSessionConduit> AudioSessionConduit::Create()
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  WebrtcAudioConduit* obj = new WebrtcAudioConduit();
  if(!obj)
  {
    return nsnull;
  } 
  
  if(obj->Init() == kMediaConduitNoError)
  {
    return obj;
  }else
  {
    return NULL;
  } 

}


/**
 * Destruction defines for our super-classes 
 */

MediaSessionConduit::~MediaSessionConduit()
{}

AudioSessionConduit::~AudioSessionConduit()
{}

WebrtcAudioConduit::~WebrtcAudioConduit()
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  if(mPtrVoEXmedia)
  {
    mPtrVoEXmedia->SetExternalRecordingStatus(false);
    mPtrVoEXmedia->SetExternalPlayoutStatus(false);
  }

  //Deal with the transport  
  if(mPtrVoENetwork)
  {
    mPtrVoENetwork->DeRegisterExternalTransport(mChannel);
  }

  if(mPtrVoEBase) 
  {
    mPtrVoEBase->StopPlayout(mChannel); 
    mPtrVoEBase->StopSend(mChannel);
    mPtrVoEBase->StopReceive(mChannel);
    mPtrVoEBase->DeleteChannel(mChannel);
    mPtrVoEBase->Terminate();
  }
}
/* 
 * WebRTCAudioConduit Implementation 
 */

MediaConduitErrorCode WebrtcAudioConduit::Init()
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  if(!mVoiceEngine)
  {
    CSFLogError(logTag, "Unable to create voice engine");
    return kAudioConduitSessionNotInited;
  }

  
  // init the engine with our audio device layer
  if(!mPtrVoEBase || mPtrVoEBase->Init() == -1)
  {
    CSFLogError(logTag, "VoiceEngine Base Not Initialized");
    return kAudioConduitSessionNotInited;
  }


  if( (mChannel = mPtrVoEBase->CreateChannel()) == -1)
  {
    CSFLogError(logTag, "VoiceEngine Channel creation failed");
    return kAudioConduitChannelError;
  } 


  if(!mPtrVoENetwork || 
    mPtrVoENetwork->RegisterExternalTransport(mChannel, *this) == -1)
  {
    CSFLogError(logTag, " VoiceEngine, External Transport Failed");
    return kAudioConduitTransportRegistrationFail;
  }

  // Set external recording status as true
  if(!mPtrVoEXmedia ||
      mPtrVoEXmedia->SetExternalRecordingStatus(true) == -1)
  {
    CSFLogError(logTag,  "SetExternalRecordingStatus Failed %d", 
                                      mPtrVoEBase->LastError());    
    return kAudioConduitExternalPlayoutError;
  }

  if(mPtrVoEXmedia->SetExternalPlayoutStatus(true) == -1) 
  {
    CSFLogError(logTag,  "SetExternalPlayoutStatus Failed %d ", 
                                     mPtrVoEBase->LastError());    
    return kAudioConduitExternalRecordingError;
  }

  CSFLogDebug(logTag ,  "AudioSessionConduit Initialization Done");  

  return kMediaConduitNoError;
}


// AudioSessionConduit Impelmentation

MediaConduitErrorCode 
  WebrtcAudioConduit::AttachRenderer(mozilla::RefPtr<AudioRenderer> aAudioRenderer)
{
  
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  if(aAudioRenderer)
  {
    mRenderer = aAudioRenderer.get();
    return kMediaConduitNoError;
  }
  return kAudioConduitInvalidRenderer; 
}

MediaConduitErrorCode 
  WebrtcAudioConduit::AttachTransport(mozilla::RefPtr<TransportInterface> aTransport)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  
  if(aTransport)
  {
    mTransport = aTransport.get();
    return kMediaConduitNoError;
  }
   return kAudioConduitInvalidTransport;
}


MediaConduitErrorCode 
WebrtcAudioConduit::ConfigureSendMediaCodec(const AudioCodecConfig* codecConfig)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  if(!codecConfig)
  {
    return  kAudioConduitInvalidSendCodec;
  }


  int error = 0;
  webrtc::CodecInst cinst;
  strcpy(cinst.plname, codecConfig->mName.c_str());
  cinst.pltype = codecConfig->mType;
  cinst.rate = codecConfig->mRate; 
  cinst.pacsize = codecConfig->mPacSize; 
  cinst.plfreq =  codecConfig->mFreq;
  cinst.channels = codecConfig->mChannels;

  error = mPtrVoECodec->SetSendCodec(mChannel, cinst);
  if(-1 == error)
  { 
    error = mPtrVoEBase->LastError();
    CSFLogError(logTag, "Setting Send Codec Failed %d ", error);
    
    if(error ==  VE_CANNOT_SET_SEND_CODEC || error == VE_CODEC_ERROR)
    {
      return kAudioConduitInvalidSendCodec;
    }
    return kAudioConduitUnknownError; 
  }

  //Let's Send Transport State-machine on the Engine
  if(mPtrVoEBase->StartSend(mChannel) == -1)
  {
    error = mPtrVoEBase->LastError();
    if(error == VE_ALREADY_SENDING)
      {
        return kAudioConduitSendingAlready;
      }
    return kAudioConduitUnknownError;
  }

  return kMediaConduitNoError;
}

MediaConduitErrorCode 
WebrtcAudioConduit::ConfigureRecvMediaCodec(const AudioCodecConfig* codecConfig)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  if(!codecConfig)
  {
    return kAudioConduitInvalidReceiveCodec;
  }

  int error = 0;
  webrtc::CodecInst cinst;
  strcpy(cinst.plname, codecConfig->mName.c_str());
  cinst.pltype = codecConfig->mType;
  cinst.rate = codecConfig->mRate;
  cinst.pacsize = codecConfig->mPacSize; 
  cinst.plfreq =  codecConfig->mFreq;
  cinst.channels = codecConfig->mChannels;

  if(mPtrVoECodec->SetRecPayloadType(mChannel,cinst) == -1)
  {
    error = mPtrVoEBase->LastError();
    CSFLogError(logTag,  "Setting Receive Codec Failed %d ",error);

    if(error == VE_CODEC_ERROR)
    {
      return kAudioConduitInvalidReceiveCodec;
    }
    return kAudioConduitUnknownError; 

  }

  error = mPtrVoEBase->StartReceive(mChannel);
  if(-1 == error)
  {
    error = mPtrVoEBase->LastError();
    CSFLogError(logTag ,  "StartReceive Failed %d ",error);
    if(error == VE_RECV_SOCKET_ERROR)
    {
      return kAudioConduitSocketError;
    }
    return kAudioConduitUnknownError; 
  }

  return kMediaConduitNoError;
}

MediaConduitErrorCode 
WebrtcAudioConduit::SendAudioFrame(const int16_t audio_data[],
                                    unsigned int numberOfSamples,
                                    uint32_t samplingFreqHz,
                                    uint64_t capture_time) 
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

 
  if(!mEnginePlaying)
  {
    if(-1 == mPtrVoEBase->StartPlayout(mChannel))
      return kAudioConduitSessionNotInited;

    mEnginePlaying = true;

  }

  if(mPtrVoEXmedia->ExternalRecordingInsertData(audio_data,
                                                numberOfSamples,
                                                samplingFreqHz,
                                                capture_time) == -1)
  {
     int error = mPtrVoEBase->LastError();
     if(error == VE_RUNTIME_REC_ERROR)
     {
       return kAudioConduitRecordingError;
     }
    CSFLogError(logTag,  "Send Failediled %d", mPtrVoEBase->LastError());
  }
  // we should be good here
  return kMediaConduitNoError;
}

MediaConduitErrorCode
WebrtcAudioConduit::GetAudioFrame(int16_t speechData[],
                                   uint32_t samplingFreqHz,
                                   uint64_t capture_delay,
                                   unsigned int& lengthSamples)
{
  
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  lengthSamples = 0;

  if(mEnginePlaying)
  {
    if(mPtrVoEXmedia->ExternalPlayoutGetData((WebRtc_Word16*) speechData, 
                                              (int)samplingFreqHz,
                                              capture_delay, 
                                              (int&)lengthSamples) == -1)
    {
      int error = mPtrVoEBase->LastError();

      if(error == VE_RUNTIME_PLAY_ERROR)
      {
         return kAudioConduitPlayoutError;
      }
      return kAudioConduitUnknownError;

    }
  } //end if

  CSFLogDebug(logTag,  "GetAudioFrame:Got samples: length %d ", lengthSamples);
  return kMediaConduitNoError;
}

// Transport Layer Callbacks 
MediaConduitErrorCode 
WebrtcAudioConduit::ReceivedRTPPacket(const void *data, int len)
{
  CSFLogDebug(logTag,  "%s : channel %d", __FUNCTION__, mChannel);

  if(mEnginePlaying) 
  {
    if(mPtrVoENetwork->ReceivedRTPPacket(mChannel,data,len) == -1)
    {
      int error = mPtrVoEBase->LastError();
      if(error == VE_RTP_RTCP_MODULE_ERROR)
      {
        return kAudioConduitRTPRTCPModuleError;
      }

      return kAudioConduitUnknownError;
    }
  }
  return kMediaConduitNoError;
}

MediaConduitErrorCode 
WebrtcAudioConduit::ReceivedRTCPPacket(const void *data, int len)
{
  CSFLogDebug(logTag,  "%s : channel %d ",__FUNCTION__, mChannel);

  if(mEnginePlaying) 
  {
    if(mPtrVoENetwork->ReceivedRTCPPacket(mChannel, data, len) == -1)
    {
      int error = mPtrVoEBase->LastError();
      if(error == VE_RTP_RTCP_MODULE_ERROR)
      {
        return kAudioConduitRTPRTCPModuleError;
      }

      return kAudioConduitUnknownError;

    }
  } else 
  {
    return kAudioConduitSessionNotInited;
  }

  return kMediaConduitNoError;
}

//WebRTC::RTP Callback Implementation

int WebrtcAudioConduit::SendPacket(int channel, const void* data, int len)
{
  CSFLogDebug(logTag,  "%s : channel %d",__FUNCTION__,channel);

   if(mTransport) 
   {
      if(mTransport->SendRtpPacket(data, len) != NS_OK)
      {
        CSFLogDebug(logTag,  "SendRtpPacket:PktTransport Fail");
        return -1;
      }
   } else 
   {
     CSFLogDebug(logTag,  "SendRtpPacket:TransportI/F NULL");
     return -1;
   }
 return 0; 
}

int WebrtcAudioConduit::SendRTCPPacket(int channel, const void* data, int len)
{
  CSFLogDebug(logTag,  "%s : channel %d", __FUNCTION__, channel);
  if(mTransport) 
  {
    if(mTransport->SendRtcpPacket(data, len)  != NS_OK)
    {
       CSFLogDebug(logTag,  "SendRtcpPacket:PktTransport Fail");
        return -1;
    } 
    

  } else
  {
     CSFLogDebug(logTag,  "SendRtpPacket:TransportI/F NULL");
     return -1;
  }

  return 0;
}

}// end namespace

