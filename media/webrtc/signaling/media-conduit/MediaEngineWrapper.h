/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
  * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef  MEDIA_ENGINE_WRAPPER_H_
#define MEDIA_ENGINE_WRAPPER_H_

#include <mozilla/Scoped.h>

//WebRTC includes
#include "common_types.h"
#include "voice_engine/main/interface/voe_base.h"
#include "video_engine/include/vie_base.h"



namespace mozilla
{

/**
 * Simple Wrapper class to return one and only instance of the
 * Voice and Video Engines
 * TODO:Crypt: Improve this - this  is pretty dumb as of this version
 */
class WebRTCEngineWrapper
{
public:
 

  webrtc::VoiceEngine* GetVoiceEngine()
  {
    if(mVoiceEngine)
    {
      return mVoiceEngine;
    } else {
      mVoiceEngine = webrtc::VoiceEngine::Create();
      return mVoiceEngine;
    }

  }

  webrtc::VideoEngine* GetVideoEngine()
  {
    if(mVideoEngine)
    {
      return mVideoEngine;
    } else {
      mVideoEngine = webrtc::VideoEngine::Create();
      return mVideoEngine;
    }
    
  }

 static WebRTCEngineWrapper* Instance();

private:
  //private constructor
  WebRTCEngineWrapper(): mVoiceEngine(NULL)
                        ,mVideoEngine(NULL)
  {

  }

  ~WebRTCEngineWrapper()
  {
    webrtc::VoiceEngine::Delete(mVoiceEngine);
    webrtc::VideoEngine::Delete(mVideoEngine);
  }

  webrtc::VoiceEngine* mVoiceEngine;
  webrtc::VideoEngine* mVideoEngine;
  static WebRTCEngineWrapper* _instance;
};


/**
 * A Custom scoped template to release a resoure of Type T
 * with a function of Type F
 * ScopedCustomReleasePtr<webrtc::VoENetwork> ptr =
 *               webrtc::VoENetwork->GetInterface(voiceEngine);
 *         
 */
template<typename T>
struct ScopedCustomReleaseTraits0 : public ScopedFreePtrTraits<T>
{
  static void release(T* ptr)
  {
    (ptr)->Release();
  }
};

SCOPED_TEMPLATE(ScopedCustomReleasePtr, ScopedCustomReleaseTraits0)
}//namespace


#endif
