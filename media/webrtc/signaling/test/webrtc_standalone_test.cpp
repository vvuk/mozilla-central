/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


#include <iostream>
#include <string>
#include <unistd.h>
#include <stdio.h>


#include "prinrval.h"


#include "mozilla/Scoped.h"

#include "common_types.h"

//Audio Engine Includes
#include "voice_engine/main/interface/voe_base.h"
#include "voice_engine/main/interface/voe_file.h"
#include "voice_engine/main/interface/voe_hardware.h"
#include "voice_engine/main/interface/voe_codec.h"
#include "voice_engine/main/interface/voe_external_media.h"
#include "voice_engine/main/interface/voe_errors.h"


//webrtc test utilities
#include "resource_mgr.h"


#include "FakeMediaStreamsImpl.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

const int AUDIO_SAMPLE_LENGTH = 160;
const int DEFAULT_PORT = 55555;


using namespace std;

/**
 * WebRTC Standalone helper test-case for external
 * recording. This test should be used along with
 * media-conduit test-case.
 */

namespace {

class WebrtcTest : public ::testing::Test {
 public:
  WebrtcTest():mVoiceEngine(NULL),
              mPtrVoEBase(NULL),
              mPtrVoEFile(NULL),
              mChannel(-1),
              initDone(false),
              isPlaying(false)
 {

 }

  ~WebrtcTest()
  {
    Cleanup();
  }

  void Init()
  {
   if( initDone )
        return;

   cerr << "  Creating Voice Engine " << endl;
   mVoiceEngine = webrtc::VoiceEngine::Create();
   ASSERT_NE(mVoiceEngine, (void*)NULL);

   mPtrVoEBase = webrtc::VoEBase::GetInterface(mVoiceEngine);
   ASSERT_NE(mPtrVoEBase, (void*)NULL);
   int res = mPtrVoEBase->Init();
   ASSERT_EQ(0, res);

   mPtrVoEFile = webrtc::VoEFile::GetInterface(mVoiceEngine);
   ASSERT_NE(mPtrVoEFile, (void*)NULL);

   mPtrVoEXmedia = webrtc::VoEExternalMedia::GetInterface(mVoiceEngine);
   ASSERT_NE(mPtrVoEXmedia, (void*)NULL);

   mPtrVoECodec = webrtc::VoECodec::GetInterface(mVoiceEngine);
   ASSERT_NE(mPtrVoECodec, (void*)NULL);

   //check if we have audio playout devices for us
   mPtrVoEHardware  = webrtc::VoEHardware::GetInterface(mVoiceEngine);
   ASSERT_NE(mPtrVoEHardware, (void*)NULL);
   int devices = 0;
   if(mPtrVoEHardware->GetNumOfPlayoutDevices(devices) == -1)
   {
    initDone = false;
    lastError = "Getting Playout Devices Failed  !!";
   }
   if(!devices)
   {
    initDone = false;
    lastError = "No Playout Devices Found !!";
   }

   EXPECT_EQ(0, mPtrVoEHardware->Release());

   initDone = true;

 }


  void Cleanup()
  {
    //release interfaces
    EXPECT_EQ(0, mPtrVoEBase->Terminate());


    EXPECT_EQ(0, mPtrVoEFile->Release());


    EXPECT_EQ(0, mPtrVoEXmedia->Release());

    EXPECT_EQ(0, mPtrVoEBase->Release());


    // delete the engine
    webrtc::VoiceEngine::Delete(mVoiceEngine);

    initDone = false;
  }

  //1. Test Case to play he standard audio file used as input
  // for media-conduit test-case.
  void TestAudioFileLocalPlayout()
  {
    std::string rootpath = ProjectRootPath();
    filename = "audio_short16.pcm";
    fileToPlay =
             rootpath+"media"+kPathDelimiter+"webrtc"+kPathDelimiter+"trunk"+kPathDelimiter+"test"+kPathDelimiter+"data"+kPathDelimiter+"voice_engine"+kPathDelimiter+filename;
    cerr << "File is " << fileToPlay << endl;

    Init();

    if(!initDone)
    {

      cerr << " Init failed.. " << lastError << endl;

      return;
    }

    mChannel = mPtrVoEBase->CreateChannel();
    EXPECT_TRUE(mChannel != -1);

    cerr << "  ************************************************" << endl;
    cerr << "  PLAYING ORIGINAL AUDIO FILE NOW FOR 5 SECONDS "<< endl;
    cerr << "  ************************************************" << endl;


    StartMedia(0);
    PR_Sleep(PR_SecondsToInterval(5));
    StopMedia();

    cerr << "  ************************************************" << endl;
    cerr << "  DONE PLAYING ORIGINAL AUDIO FILE NOW "<< endl;
    cerr << "  ************************************************" << endl;

    EXPECT_EQ(0,mPtrVoEBase->DeleteChannel(mChannel));
  }

  //2. Test case to play the external recording and played out
  // audio file from the media-conduit test case.
  void TestAudioFilePlayoutExternal()
  {
    PR_Sleep(PR_SecondsToInterval(2));

    std::string rootpath = ProjectRootPath();
    filename = "recorded.wav";
    fileToPlay =
             rootpath+"media"+kPathDelimiter+"webrtc"+kPathDelimiter+"signaling"+kPathDelimiter+"test"+kPathDelimiter+filename;
    cerr << "File is " << fileToPlay << endl;

    Init();
    if(!initDone)
    {

      printf("Init Failed. %s", lastError.c_str());
      return;
    }

    //enable external rocording
    EXPECT_EQ(0, mPtrVoEXmedia->SetExternalRecordingStatus(true));

    mChannel = mPtrVoEBase->CreateChannel();
    ASSERT_NE(mChannel, -1);

    cerr << "  ************************************************" << endl;
    cerr << "  Starting to play RECORDED File for 5 Seconds "<< endl;
    cerr << "  ************************************************" << endl;

    StartMedia(1);
    StopMedia();

    cerr << "  ************************************************" << endl;
    cerr << "  DONE PLAYING RECORDED AUDIO FILE NOW  "<< endl;
    cerr << "  ************************************************" << endl;

    EXPECT_EQ(0, mPtrVoEBase->DeleteChannel(mChannel));


  }

  void StartMedia(int choice)
  {
    int error = 0;
    int16_t audio[AUDIO_SAMPLE_LENGTH];
    unsigned int sampleSizeInBits = AUDIO_SAMPLE_LENGTH * sizeof(short);
    memset(audio,0,sampleSizeInBits);

    EXPECT_EQ(0, mPtrVoEBase->SetLocalReceiver(mChannel,DEFAULT_PORT));

    EXPECT_EQ(0, mPtrVoEBase->SetSendDestination(mChannel,DEFAULT_PORT,"127.0.0.1"));

    EXPECT_EQ(0, mPtrVoEBase->StartReceive(mChannel));

    // Fix for Audio Playout Error on some Linux builds that
    // has Audio device access issues
    if(mPtrVoEBase->StartSend(mChannel) == -1)
    {
      isPlaying = false;
      error = mPtrVoEBase->LastError();
      cerr << " StartSend Failed: Error " << error << endl;
      if(CheckForOkAudioErrors(error))
      {
        return;
      }
    }

    // Fix for Audio Playout Error on some Linux builds that
    // has Audio device access issues
    if(mPtrVoEBase->StartPlayout(mChannel) == -1)
    {
      isPlaying = false;
      int error = mPtrVoEBase->LastError();
      cerr << " StartPlayoutFailed: Error " << error << endl;

      if(CheckForOkAudioErrors(error))
      {
        return;
      }
    } else {
      isPlaying = true;
    }

    if(choice == 0)
    {
      EXPECT_EQ(0, mPtrVoEFile->StartPlayingFileAsMicrophone(mChannel,
                                                            fileToPlay.c_str(),
                                                            false,
                                                            false));

    } else if(choice == 1)
    {
      FILE* id = fopen(fileToPlay.c_str(),"rb");
      ASSERT_NE(id, (void*)NULL);
      bool finish = false;
      int t=0;
      //read the recorded sample for 5 seconds
      do
      {
        unsigned int read_ = fread(audio, 1, sampleSizeInBits, id);
        if(read_ != sampleSizeInBits || t > 5000)
        {
          finish = true;
          break;
        }
        mPtrVoEXmedia->ExternalRecordingInsertData(audio,160,16000,10);
        PR_Sleep(PR_MillisecondsToInterval(10));
        t+=10;
      }while(finish == false);
      fclose(id);
    }

  }

  void StopMedia()
  {
    if(initDone)
    {
      if(isPlaying)
      {
       EXPECT_EQ(0, mPtrVoEBase->StopPlayout(mChannel));
      }

      EXPECT_EQ(0, mPtrVoEBase->StopReceive(mChannel));

      EXPECT_EQ(0, mPtrVoEBase->StopSend(mChannel));
    }

  }

  //Function is a fix to get the test-pass in the case of
  // WbeRTC Audio Device errors on systems where we don't
  // have right playout device or device drivers or
  // media pipelines
  bool CheckForOkAudioErrors(int error)
  {
    if(error == VE_PLAY_UNDEFINED_SC_ERR ||
        error == VE_PLAY_UNDEFINED_SC_ERR ||
        error == VE_PLAY_CANNOT_OPEN_SC ||
        error == VE_RUNTIME_PLAY_ERROR ||
        error == VE_SOUNDCARD_ERROR ||
        error == VE_AUDIO_DEVICE_MODULE_ERROR ||
        error == VE_AUDIO_CODING_MODULE_ERROR ||
        error == VE_CANNOT_START_PLAYOUT)
    {
      return true;
    } else {
      return false;
    }
  }

private:

  webrtc::VoiceEngine*      mVoiceEngine;
  webrtc::VoEBase*          mPtrVoEBase;
  webrtc::VoEFile*          mPtrVoEFile;
  webrtc::VoEExternalMedia* mPtrVoEXmedia;
  webrtc::VoECodec*         mPtrVoECodec;
  webrtc::VoEHardware*      mPtrVoEHardware;
  int mChannel;
  std::string fileToPlay;
  std::string filename;
  bool initDone;
  std::string lastError;
  bool isPlaying;
};


// Test 1: Play a standard file to the speaker
// with file as microphone
TEST_F(WebrtcTest, TestSpeakerPlayout) {
 TestAudioFileLocalPlayout();
}

// Test 2: Play a pcm file to the speaker with
// samples inserted externally
TEST_F(WebrtcTest, TestSpeakerPlayoutExternalRecording) {
 TestAudioFilePlayoutExternal();
}

}  // end namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


