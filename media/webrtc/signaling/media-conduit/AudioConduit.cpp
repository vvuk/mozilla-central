/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "AudioConduit.h"
#include "CSFLog.h"
#include "voice_engine/main/interface/voe_errors.h"


namespace mozilla {

static const char* logTag ="WebrtcAudioSessionConduit";

// 32 bytes is what WebRTC CodecInst expects
const unsigned int WebrtcAudioConduit::CODEC_PLNAME_SIZE = 32;

/**
 * Factory Method for AudioConduit
 */
mozilla::RefPtr<AudioSessionConduit> AudioSessionConduit::Create()
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  WebrtcAudioConduit* obj = new WebrtcAudioConduit();
  if(!obj)
  {
    return NULL;
  } 
  
  if(obj->Init() == kMediaConduitNoError)
  {
    CSFLogDebug(logTag,  "%s Successfully created AudioConduit ", __FUNCTION__);
    return obj;
  } else {
    return NULL;
  } 

}


/**
 * Destruction defines for our super-classes 
 */
WebrtcAudioConduit::~WebrtcAudioConduit()
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  for(unsigned i=0; i < mRecvCodecList.size(); i++)
  {
    if(mRecvCodecList[i])
    {
      delete mRecvCodecList[i];
    }
  }

 
  if(mCurSendCodecConfig)
  {
    delete mCurSendCodecConfig;
  }

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

  if(mVoiceEngine)
  {
    webrtc::VoiceEngine::Delete(mVoiceEngine);
  }

}
/* 
 * WebRTCAudioConduit Implementation 
 */

MediaConduitErrorCode WebrtcAudioConduit::Init()
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  //Per WebRTC APIs below function calls return NULL on failure
  if( !(mVoiceEngine = webrtc::VoiceEngine::Create()) )
  {
    CSFLogError(logTag, "%s Unable to create voice engine", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }

  if(!(mPtrVoEBase = VoEBase::GetInterface(mVoiceEngine)) )
  {
    CSFLogError(logTag, "%s Unable to initialize VoEBase", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }

  if(!(mPtrVoENetwork = VoENetwork::GetInterface(mVoiceEngine)) )
  {
    CSFLogError(logTag, "%s Unable to initialize VoENetwork", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }

  if(!(mPtrVoECodec = VoECodec::GetInterface(mVoiceEngine)) )
  {
    CSFLogError(logTag, "%s Unable to initialize VoEBCodec", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }

  if(!(mPtrVoEXmedia = VoEExternalMedia::GetInterface(mVoiceEngine)) )
  {
    CSFLogError(logTag, "%s Unable to initialize VoEExternalMedia", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }

  
  // init the engine with our audio device layer
  if(mPtrVoEBase->Init() == -1)
  {
    CSFLogError(logTag, "%s VoiceEngine Base Not Initialized", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }


  if( (mChannel = mPtrVoEBase->CreateChannel()) == -1)
  {
    CSFLogError(logTag, "%s VoiceEngine Channel creation failed",__FUNCTION__);
    return kMediaConduitChannelError;
  } 


  CSFLogDebug(logTag, "%s Channel Created %d ",__FUNCTION__, mChannel);

  if(mPtrVoENetwork->RegisterExternalTransport(mChannel, *this) == -1)
  {
    CSFLogError(logTag, "%s VoiceEngine, External Transport Failed",__FUNCTION__);
    return kMediaConduitTransportRegistrationFail;
  }

  if(mPtrVoEXmedia->SetExternalRecordingStatus(true) == -1)
  {
    CSFLogError(logTag, "%s SetExternalRecordingStatus Failed %d",__FUNCTION__, 
                                                      mPtrVoEBase->LastError());    
    return kMediaConduitExternalPlayoutError;
  }

  if(mPtrVoEXmedia->SetExternalPlayoutStatus(true) == -1) 
  {
    CSFLogError(logTag, "%s SetExternalPlayoutStatus Failed %d ",__FUNCTION__, 
                                                     mPtrVoEBase->LastError());    
    return kMediaConduitExternalRecordingError;
  }

  CSFLogDebug(logTag ,  "%s AudioSessionConduit Initialization Done",__FUNCTION__);  

  return kMediaConduitNoError;
}


// AudioSessionConduit Implementation


MediaConduitErrorCode 
  WebrtcAudioConduit::AttachTransport(mozilla::RefPtr<TransportInterface> aTransport)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  
  if(!aTransport)
  {
    CSFLogError(logTag, "%s NULL Transport", __FUNCTION__);
    return kMediaConduitInvalidTransport;
  }

  // set the transport
  mTransport = aTransport;
  return kMediaConduitNoError;

   
}


MediaConduitErrorCode 
WebrtcAudioConduit::ConfigureSendMediaCodec(const AudioCodecConfig* codecConfig)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);

  int error = 0;
  webrtc::CodecInst cinst;

  //validate basic params
  if(!codecConfig)
  {
    CSFLogError(logTag, "%s NULL CodecConfig Param ", __FUNCTION__);
    return kMediaConduitMalformedArgument; 
  }

  //validate length of the payload
  if( (codecConfig->mName.length() == 0) ||
      (codecConfig->mName.length() >= CODEC_PLNAME_SIZE))
  {
    CSFLogError(logTag, "%s Invalid Payload Name Length ", __FUNCTION__);
    return kMediaConduitMalformedArgument;
  }

  //Only mono or stereo channels supported
  if( (codecConfig->mChannels != 1) && (codecConfig->mChannels != 2))
  {
    CSFLogError(logTag, "%s Channel Unsupported ", __FUNCTION__);
    return kMediaConduitMalformedArgument;
  }

  //Check if we have same codec already applied
  if(CheckCodecsForMatch(mCurSendCodecConfig, codecConfig))
  {
    CSFLogDebug(logTag,  "%s Codec has been applied already ", __FUNCTION__);
    return kMediaConduitNoError;
  }

  //are we transmitting already, stop and apply the send codec
  if(mEngineTransmitting)
  {
    CSFLogDebug(logTag, "%s Engine Already Sending. Attemping to Stop ", __FUNCTION__);
    if(mPtrVoEBase->StopSend(mChannel) == -1)
    {
      error = mPtrVoEBase->LastError();
      if(error == VE_NOT_SENDING)
      {
        CSFLogDebug(logTag, "%s StopSend() Success ", __FUNCTION__);
        mEngineTransmitting = false;
      } else {
        CSFLogError(logTag, "%s StopSend() Failed %d ", __FUNCTION__,
                                            mPtrVoEBase->LastError());
        return kMediaConduitUnknownError;
      }
    } 
  }

  
  mEngineTransmitting = false;

  if(!CodecConfigToWebRTCCodec(codecConfig,cinst))
  {
    CSFLogError(logTag,"%s CodecConfig to WebRTC Codec Failed ",__FUNCTION__);
    return kMediaConduitMalformedArgument;
  }
  
  if(mPtrVoECodec->SetSendCodec(mChannel, cinst) == -1)
  {
    error = mPtrVoEBase->LastError();
    CSFLogError(logTag, "%s SetSendCodec - Invalid Codec %d ",__FUNCTION__,
                                                                    error);  
     
    if(error ==  VE_CANNOT_SET_SEND_CODEC || error == VE_CODEC_ERROR)
    {
      return kMediaConduitInvalidSendCodec;
    }

    return kMediaConduitUnknownError; 
  }

  //Let's Send Transport State-machine on the Engine
  if(mPtrVoEBase->StartSend(mChannel) == -1)
  {
    error = mPtrVoEBase->LastError();
    CSFLogError(logTag, "%s StartSend failed %d", __FUNCTION__, error);
    if(error == VE_ALREADY_SENDING)
    {
      return kMediaConduitSendingAlready;
    }
    return kMediaConduitUnknownError;
  }

  //Copy the applied config for future reference.
  if(mCurSendCodecConfig)
  {
    delete mCurSendCodecConfig;
  }

  mCurSendCodecConfig = new AudioCodecConfig(codecConfig->mType,
                                              codecConfig->mName,
                                              codecConfig->mFreq,
                                              codecConfig->mPacSize,
                                              codecConfig->mChannels,
                                              codecConfig->mRate);
  

  mEngineTransmitting = true;
  return kMediaConduitNoError;
}

MediaConduitErrorCode 
WebrtcAudioConduit::ConfigureRecvMediaCodecs(
                    const std::vector<AudioCodecConfig*>& codecConfigList)
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  int error = 0;
  bool success = false;

  
  if(!codecConfigList.size())
  {
    CSFLogError(logTag, "%s Zero number of codecs to configure", __FUNCTION__);
    return kMediaConduitMalformedArgument;
  }
  
  //Try Applying the codecs in the list 
  for(unsigned int i=0 ; i < codecConfigList.size(); i++)
  { 
    //validate length of the payload
    if( (codecConfigList[i]->mName.length() == 0) ||
        (codecConfigList[i]->mName.length() >= CODEC_PLNAME_SIZE))
    {
      CSFLogError(logTag, "%s Invalid Payload Name Length ", __FUNCTION__);
      continue;
    }

    //Only mono or stereo channels supported
    if( (codecConfigList[i]->mChannels != 1) && (codecConfigList[i]->mChannels != 2))
    {
      CSFLogError(logTag, "%s Channel Unsupported ", __FUNCTION__);
      continue;
    }

    //Check if we have same codec already applied
    if(CheckCodecForMatch(codecConfigList[i]))
    {
      CSFLogDebug(logTag,  "%s Codec has been applied already ", __FUNCTION__);
      continue;
    }
   
    // are we receiving already. If so, stop receiving and playout
    // since we can't apply new recv codec when the engine is playing
    if(mEngineReceiving)
    {
      CSFLogDebug(logTag, "%s Engine Already Receiving. Attemping to Stop ", __FUNCTION__);
      // AudioEngine doesn't fail fatal on stop reception. Ref:voe_errors.h.
      // hence we need-notbe strict in failing here on error
      mPtrVoEBase->StopReceive(mChannel);
      CSFLogDebug(logTag, "%s Attemping to Stop playout ", __FUNCTION__);

      if(mPtrVoEBase->StopPlayout(mChannel) == -1)
      {
        if( mPtrVoEBase->LastError() == VE_CANNOT_STOP_PLAYOUT)
         {
           CSFLogDebug(logTag, "%s Stop-Playout Failed %d", mPtrVoEBase->LastError());
           return kMediaConduitPlayoutError;
         }
      }
      mEngineReceiving = false;
    }

    mEngineReceiving = false;
    
    webrtc::CodecInst cinst;
    if(!CodecConfigToWebRTCCodec(codecConfigList[i],cinst))
    {
      CSFLogError(logTag,"%s CodecConfig to WebRTC Codec Failed ",__FUNCTION__);
      continue;
    }
    
    
    if(mPtrVoECodec->SetRecPayloadType(mChannel,cinst) == -1)
    {
      error = mPtrVoEBase->LastError();
      CSFLogError(logTag,  "%s SetRecvCodec Failed %d ",__FUNCTION__, error);
      continue;
    } else {
      CSFLogDebug(logTag, "%s Successfully Set RecvCodec %s", __FUNCTION__,
                                          codecConfigList[i]->mName.c_str());
      //copy this to out database
      if(CopyCodecToDB(codecConfigList[i]))
      {
        success = true;
      } else {
        CSFLogError(logTag,"%s Unable to updated Codec Database", __FUNCTION__);
        return kMediaConduitUnknownError;
      }

    }

  } //end for

  //Success == false indicates none of the codec was applied
  if(!success)
  {
    CSFLogError(logTag, "%s Setting Receive Codec Failed ", __FUNCTION__);
    return kMediaConduitInvalidReceiveCodec;
  }
  
  //If we are here, atleast one codec should have been set
  if(mPtrVoEBase->StartReceive(mChannel) == -1)
  {
    error = mPtrVoEBase->LastError();
    CSFLogError(logTag ,  "StartReceive Failed %d ",error);
    if(error == VE_RECV_SOCKET_ERROR)
    {
      return kMediaConduitSocketError;
    }
    return kMediaConduitUnknownError; 
  }

  
  if(mPtrVoEBase->StartPlayout(mChannel) == -1)
  {
    CSFLogError(logTag, "Starting playout Failed");
    return kMediaConduitPlayoutError;
  }

  //we should be good here for setting this.
  mEngineReceiving = true;
  DumpCodecDB();
  return kMediaConduitNoError;
}

MediaConduitErrorCode 
WebrtcAudioConduit::SendAudioFrame(const int16_t audio_data[],
                                    int32_t lengthSamples,
                                    int32_t samplingFreqHz,
                                    int32_t capture_delay) 
{
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  
  // Following checks need to be performed
  // 1. Non null audio buffer pointer, 
  // 2. invalid sampling frequency -  less than 0 or unsupported ones
  // 3. Appropriate Sample Length for 10 ms audio-frame. This represents
  //    block size the VoiceEngine feeds into encoder for passed in audio-frame 
  //    Ex: for 16000 sampling rate , valid block-length is 160
  //    Similarly for 32000 sampling rate, valid block length is 320
  //    We do the check by the verify modular operator below to be zero
  
  if(!audio_data || (lengthSamples <= 0) ||
                    (IsSamplingFreqSupported(samplingFreqHz) == false) ||
                    ((lengthSamples % (samplingFreqHz / 100) != 0)) )
  {
    CSFLogError(logTag, "%s Invalid Params ", __FUNCTION__);
    return kMediaConduitMalformedArgument;
  }

  //validate capture time
  if(capture_delay < 0 )
  {
     CSFLogError(logTag,"%s Invalid Capture Delay ", __FUNCTION__);
     return kMediaConduitMalformedArgument; 
  }

  // if transmission is not started .. conduit cannot insert frames
  if(!mEngineTransmitting)
  {
    CSFLogError(logTag, "%s Engine not transmitting ", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }
 
  
  //Insert the samples 
  if(mPtrVoEXmedia->ExternalRecordingInsertData(audio_data,
                                                lengthSamples,
                                                samplingFreqHz,
                                                capture_delay) == -1)
  {
    int error = mPtrVoEBase->LastError();
    CSFLogError(logTag,  "Inserting audio data Failed %d", error);
    if(error == VE_RUNTIME_REC_ERROR)
    {
      return kMediaConduitRecordingError;
    }
    return kMediaConduitUnknownError;

  }
  // we should be good here
  return kMediaConduitNoError;
}

MediaConduitErrorCode
WebrtcAudioConduit::GetAudioFrame(int16_t speechData[],
                                   int32_t samplingFreqHz,
                                   int32_t capture_delay,
                                   int& lengthSamples)
{
  
  CSFLogDebug(logTag,  "%s ", __FUNCTION__);
  unsigned int numSamples = 0;
  
  //validate params
  if(!speechData )
  {
     CSFLogError(logTag,"%s Null Audio Buffer Pointer", __FUNCTION__);
     return kMediaConduitMalformedArgument; 
  }

  // Validate sample length 
  if((numSamples = GetNum10msSamplesForFrequency(samplingFreqHz)) == 0  )
  {
     CSFLogError(logTag,"%s Invalid Sampling Frequency ", __FUNCTION__);
     return kMediaConduitMalformedArgument; 
  }

  //validate capture time
  if(capture_delay < 0 )
  {
     CSFLogError(logTag,"%s Invalid Capture Delay ", __FUNCTION__);
     return kMediaConduitMalformedArgument; 
  }

  //Conduit should have reception enabled before we ask for decoded
  // samples
  if(!mEngineReceiving)
  {
    CSFLogError(logTag, "%s Engine not Receiving ", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }


  lengthSamples = 0;  //output paramter

  memset(speechData, 0, numSamples);
  
  if(mPtrVoEXmedia->ExternalPlayoutGetData( speechData, 
                                            samplingFreqHz,
                                            capture_delay, 
                                            lengthSamples) == -1)
  {
    int error = mPtrVoEBase->LastError();
    CSFLogError(logTag,  "Getting audio data Failed %d", error);
    if(error == VE_RUNTIME_PLAY_ERROR)
    {
      return kMediaConduitPlayoutError;
    }
    return kMediaConduitUnknownError;

  }
  

  CSFLogDebug(logTag,"%s GetAudioFrame:Got samples: length %d ",__FUNCTION__,
                                                               lengthSamples);
  return kMediaConduitNoError;
}

// Transport Layer Callbacks 
MediaConduitErrorCode 
WebrtcAudioConduit::ReceivedRTPPacket(const void *data, int len)
{
  CSFLogDebug(logTag,  "%s : channel %d", __FUNCTION__, mChannel);

  if(mEngineReceiving) 
  {
    if(mPtrVoENetwork->ReceivedRTPPacket(mChannel,data,len) == -1)
    {
      int error = mPtrVoEBase->LastError();
      CSFLogError(logTag, "%s RTP Processing Error %d ", error);
      if(error == VE_RTP_RTCP_MODULE_ERROR)
      {
        return kMediaConduitRTPRTCPModuleError;
      }
      return kMediaConduitUnknownError;
    }
  } else {
    //engine not receiving 
    CSFLogError(logTag, "ReceivedRTPPacket: Engine Error");
    return kMediaConduitSessionNotInited;
  }
  //good here
  return kMediaConduitNoError;
}

MediaConduitErrorCode 
WebrtcAudioConduit::ReceivedRTCPPacket(const void *data, int len)
{
  CSFLogDebug(logTag,  "%s : channel %d ",__FUNCTION__, mChannel);

  if(mEngineReceiving) 
  {
    if(mPtrVoENetwork->ReceivedRTCPPacket(mChannel, data, len) == -1)
    {
      int error = mPtrVoEBase->LastError();
      CSFLogError(logTag, "%s RTCP Processing Error %d ", error);
      if(error == VE_RTP_RTCP_MODULE_ERROR)
      {
        return kMediaConduitRTPRTCPModuleError;
      }
      return kMediaConduitUnknownError;
    }
  } else {
    //engine not running
    CSFLogError(logTag, "ReceivedRTPPacket: Engine Error");
    return kMediaConduitSessionNotInited;
  }

  //good here
  return kMediaConduitNoError;
}

//WebRTC::RTP Callback Implementation

int WebrtcAudioConduit::SendPacket(int channel, const void* data, int len)
{
  CSFLogDebug(logTag,  "%s : channel %d",__FUNCTION__,channel);

   if(mTransport && (mTransport->SendRtpPacket(data, len) == NS_OK))
   {
      CSFLogDebug(logTag, "%s Sent RTP Packet ", __FUNCTION__);
      return 0;
   } else {
     CSFLogError(logTag, "%s RTP Packet Send Failed ", __FUNCTION__);
     return -1;
   }
 
}

int WebrtcAudioConduit::SendRTCPPacket(int channel, const void* data, int len)
{
  CSFLogDebug(logTag,  "%s : channel %d", __FUNCTION__, channel);
  if(mTransport && mTransport->SendRtcpPacket(data, len) == NS_OK)
  {
    CSFLogDebug(logTag, "%s Sent RTCP Packet ", __FUNCTION__);
    return 0;
  } else {
    CSFLogError(logTag, "%s RTCP Packet Send Failed ", __FUNCTION__);
    return -1;
  }
  
}

/**
 * Class Utility Functions
 */

 /**
  * Converts between CodecConfig to WebRTC Codec Structure
  * 
  */

bool 
 WebrtcAudioConduit::CodecConfigToWebRTCCodec(const AudioCodecConfig* codecInfo,
                                              webrtc::CodecInst& cinst)
 {
  
  if(!codecInfo)
  {
    return false;
  }
  
  const unsigned int plNameLength = codecInfo->mName.length()+1;
  memset(&cinst, 0, sizeof(webrtc::CodecInst));
  if(sizeof(cinst.plname) < plNameLength)
  {
    CSFLogError(logTag, "%s Payload name buffer capacity mismatch ",
                                                      __FUNCTION__);
    return false;
  }

  memcpy(cinst.plname, codecInfo->mName.c_str(),codecInfo->mName.length());
  cinst.plname[plNameLength]='\0';
  cinst.pltype   =  codecInfo->mType;
  cinst.rate     =  codecInfo->mRate;
  cinst.pacsize  =  codecInfo->mPacSize; 
  cinst.plfreq   =  codecInfo->mFreq;
  cinst.channels =  codecInfo->mChannels;
  return true;
 }

/**
  *  Supported Sampling Frequncies.
  */
bool 
 WebrtcAudioConduit::IsSamplingFreqSupported(int freq) const
{
  if(GetNum10msSamplesForFrequency(freq))
  {
    return true;
  } else {
    return false;
  }
}

/* Return block-length of 10 ms audio frame in number of samples */
unsigned int 
 WebrtcAudioConduit::GetNum10msSamplesForFrequency(int samplingFreqHz) const
{
  switch(samplingFreqHz)
  {
    case 16000: return 160; //160 samples
    case 32000: return 320; //320 samples
    case 44000: return 440; //440 samples
    case 48000: return 480; //480 samples
    default:    return 0; // invalid or unsupported
  }
}

//Copy the codec passed into Conduit's database
bool 
 WebrtcAudioConduit::CopyCodecToDB(const AudioCodecConfig* codecInfo)
{
  if(!codecInfo)
  {
    return false;
  }

  AudioCodecConfig* cdcConfig = new AudioCodecConfig(codecInfo->mType,
                                                     codecInfo->mName,
                                                     codecInfo->mFreq,
                                                     codecInfo->mPacSize,
                                                     codecInfo->mChannels,
                                                     codecInfo->mRate);
 if(cdcConfig)
 {
    mRecvCodecList.push_back(cdcConfig);
    return true;
 } else
 {
    CSFLogDebug(logTag, "%s Mem Alloc Failure for Codec", __FUNCTION__);
    return false;
 }

}

/**
 * Checks if 2 codec structs are same
 */
bool 
 WebrtcAudioConduit::CheckCodecsForMatch(const AudioCodecConfig* curCodecConfig,
                                         const AudioCodecConfig* codecInfo) const
{
  if(!curCodecConfig || !codecInfo)
  { 
    return false;
  } 

  if(curCodecConfig->mType   == codecInfo->mType &&
      (curCodecConfig->mName.compare(codecInfo->mName) == 0) &&
      curCodecConfig->mFreq   == codecInfo->mFreq &&
      curCodecConfig->mPacSize == codecInfo->mPacSize &&
      curCodecConfig->mChannels == codecInfo->mChannels &&
      curCodecConfig->mRate == codecInfo->mRate)
  {
    return true;
  } 

  return false;
}

/**
 * Checks if the codec is already in Conduit's database
 */
bool 
 WebrtcAudioConduit::CheckCodecForMatch(const AudioCodecConfig* codecInfo)
{
  //local codec db is empty
  if(mRecvCodecList.size() == 0)
  {
    return false;
  }

  if(!codecInfo)
  {
    return false;
  }

  //the db should have atleast one codec
  for(unsigned int i=0 ; i < mRecvCodecList.size(); i++)
  {
    if(CheckCodecsForMatch(
               const_cast<AudioCodecConfig*>(mRecvCodecList[i]),codecInfo))
    {
      //match
      return true;
    }
    continue;  
  }
  //no match
  return false;
}

void
 WebrtcAudioConduit::DumpCodecDB() const
 {
    for(unsigned int i=0;i < mRecvCodecList.size(); i++)
    {
      CSFLogDebug(logTag,"Payload Name: %s", mRecvCodecList[i]->mName.c_str());
      CSFLogDebug(logTag,"Payload Type: %d", mRecvCodecList[i]->mType);
      CSFLogDebug(logTag,"Payload Frequency: %d", mRecvCodecList[i]->mFreq);
      CSFLogDebug(logTag,"Payload PacketSize: %d", mRecvCodecList[i]->mPacSize);
      CSFLogDebug(logTag,"Payload Channels: %d", mRecvCodecList[i]->mChannels);
      CSFLogDebug(logTag,"Paylaod Sampling Rate: %d", mRecvCodecList[i]->mRate);
      
       
    }
 }


}// end namespace

