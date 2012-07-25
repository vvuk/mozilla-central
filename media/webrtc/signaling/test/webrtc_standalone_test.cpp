/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


#include <iostream>
#include <string>
#include <unistd.h>
#include <stdio.h>




#include "mozilla/Scoped.h"

#include "common_types.h"

//Audio Engine Includes
#include "voice_engine/main/interface/voe_base.h"
#include "voice_engine/main/interface/voe_file.h"
#include "voice_engine/main/interface/voe_hardware.h"
#include "voice_engine/main/interface/voe_codec.h"
#include "voice_engine/main/interface/voe_external_media.h"


//Video Engine Includes
#include "video_engine/include/vie_base.h"
#include "video_engine/include/vie_codec.h"
#include "video_engine/include/vie_render.h"
#include "video_engine/include/vie_capture.h"

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
              initDone(false) 
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

   std::cerr << "  Creating Voice Engine " << std::endl;
   mVoiceEngine = webrtc::VoiceEngine::Create();
   ASSERT_TRUE(mVoiceEngine != NULL);

   mPtrVoEBase = webrtc::VoEBase::GetInterface(mVoiceEngine);
   ASSERT_TRUE(mPtrVoEBase != NULL);
   int res = mPtrVoEBase->Init();
   ASSERT_EQ(0, res);

   mPtrVoEFile = webrtc::VoEFile::GetInterface(mVoiceEngine);
   ASSERT_TRUE(mPtrVoEFile != NULL);

   mPtrVoEXmedia = webrtc::VoEExternalMedia::GetInterface(mVoiceEngine);
   ASSERT_TRUE(mPtrVoEXmedia != NULL);

   mPtrVoECodec = webrtc::VoECodec::GetInterface(mVoiceEngine);
   ASSERT_TRUE(mPtrVoECodec != NULL);

   initDone = true;
  
 }


  void Cleanup()
  {
    std::cerr << " Cleanup the Engine " << std::endl;

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
  // for media-cionduit test-case.
  void TestAudioFileLocalPlayout() 
  {
    std::string rootpath = ProjectRootPath();
    filename = "audio_short16.pcm";
    fileToPlay =
             rootpath+"media"+kPathDelimiter+"webrtc"+kPathDelimiter+"trunk"+kPathDelimiter+"test"+kPathDelimiter+"data"+kPathDelimiter+"voice_engine"+kPathDelimiter+filename;
    printf("FILE IS %s", fileToPlay.c_str());

    Init();

    mChannel = mPtrVoEBase->CreateChannel();
    EXPECT_TRUE(mChannel != -1);

    printf("\n ************************************************");
    printf("\n PLAYING ORIGINAL AUDIO FILE NOW FOR 5 SECONDS");
    printf("\n ************************************************");
    
    StartMedia(0); 
    sleep(5);
    StopMedia();
    
    printf("\n ************************************************");
    printf("\n DONE PLAYING ORIGINAL AUDIO FILE NOW ");
    printf("\n ************************************************");
    EXPECT_EQ(0,mPtrVoEBase->DeleteChannel(mChannel));
  }

  //2. Test case to play the external recording and played out 
  // audio file from the media-conduit test case.
  void TestAudioFilePlayoutExternal() 
  {
    sleep(2);
    
    std::string rootpath = ProjectRootPath();
    filename = "recorded.pcm";
    fileToPlay =
             rootpath+"media"+kPathDelimiter+"webrtc"+kPathDelimiter+"signaling"+kPathDelimiter+"test"+kPathDelimiter+filename;
    printf("FILE IS %s", fileToPlay.c_str());

    Init();

    //enable external rocording
    EXPECT_EQ(0, mPtrVoEXmedia->SetExternalRecordingStatus(true)); 

    mChannel = mPtrVoEBase->CreateChannel();
    ASSERT_TRUE(mChannel != -1);
    
    printf("\n ************************************************");
    printf("\n Starting to play RECORDED File for 5 Seconds");
    printf("\n *************************************************");

    StartMedia(1); 
    StopMedia();
    
    printf("\n ***********************************************");
    printf("\n DONE PLAYING RECORDED AUDIO FILE NOW ");
    printf("\n ***********************************************");
    
    EXPECT_EQ(0, mPtrVoEBase->DeleteChannel(mChannel));
    

  }

  void StartMedia(int choice) 
  {
    int16_t audio[AUDIO_SAMPLE_LENGTH];
    unsigned int sampleSizeInBits = AUDIO_SAMPLE_LENGTH * sizeof(short);
    memset(audio,0,sampleSizeInBits);

    EXPECT_EQ(0, mPtrVoEBase->SetLocalReceiver(mChannel,DEFAULT_PORT));

    EXPECT_EQ(0, mPtrVoEBase->SetSendDestination(mChannel,DEFAULT_PORT,"127.0.0.1"));
    
    EXPECT_EQ(0, mPtrVoEBase->StartReceive(mChannel));
    
    EXPECT_EQ(0, mPtrVoEBase->StartSend(mChannel));
    
    EXPECT_EQ(0, mPtrVoEBase->StartPlayout(mChannel));
    
   if(choice == 0)
   {
     EXPECT_EQ(0, mPtrVoEFile->StartPlayingFileAsMicrophone(mChannel,
                                                            fileToPlay.c_str(),
                                                            false,
                                                            false));

   } else if(choice == 1)
   {
       FILE* id = fopen(fileToPlay.c_str(),"rb");
       bool finish = false;
       int t=0;
       //read the recorded sample for 5 seconds
       do 
       {
        int read_ = fread(audio, 1, sampleSizeInBits, id);
        if(read_ != sampleSizeInBits || t > 5000)
        {
          finish = true;
          break;
        }
          mPtrVoEXmedia->ExternalRecordingInsertData(audio,160,16000,10);
          usleep(10*1000);
          t+=10;
       }while(finish == false);
       fclose(id);
    }

  } 

  void StopMedia()
  {
    EXPECT_EQ(0, mPtrVoEBase->StopPlayout(mChannel));

    EXPECT_EQ(0, mPtrVoEBase->StopReceive(mChannel));

    EXPECT_EQ(0, mPtrVoEBase->StopSend(mChannel));
  } 

private:

  webrtc::VoiceEngine*      mVoiceEngine;
  webrtc::VoEBase*          mPtrVoEBase;
  webrtc::VoEFile*          mPtrVoEFile;
  webrtc::VoEExternalMedia* mPtrVoEXmedia;
  webrtc::VoECodec*         mPtrVoECodec;

  int mChannel;
  std::string fileToPlay;
  std::string filename;
  bool initDone;
};


// Test 1: Play a standard file to the speaker
// with file as microphone
TEST_F(WebrtcTest, TestSpeakerPlayout) {
 TestAudioFileLocalPlayout();
}

// Test 2: Plays a pcm file to the speaker with 
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


