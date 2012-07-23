/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <vector>

using namespace std;

#include "mozilla/Scoped.h"
#include <MediaConduitInterface.h>
#include "nsStaticComponents.h"


#include "resource_mgr.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

//Video Frame Color
const int COLOR = 0x80; //Gray

/**
  * Global structure to stor video test results.
  */
struct VideoTestStats
{
 int numRawFramesInserted;
 int numFramesRenderedSuccessfully;
 int numFramesRenderedWrongly;
};

VideoTestStats vidStatsGlobal={0,0,0};


/**
 * A Dummy Video Conduit Tester. 
 * The test-case inserts a 640*480 grey imagerevery 33 milliseconds 
 * to the video-conduit for encoding and transporting. 
 */
 
class VideoSendAndReceive
{
public:
  VideoSendAndReceive():width(640),
                        height(480)
  {
  }

  ~VideoSendAndReceive()
  {
  }

  void Init(mozilla::RefPtr<mozilla::VideoSessionConduit> aSession)
  {
        mSession = aSession;
  }
  void GenerateAndReadSamples()
  {
    
    int len = ((width * height) * 3 / 2);
    PRUint8* frame = (PRUint8*) PR_MALLOC(len);
    int numFrames = 121;
    memset(frame, COLOR, len);

    do
    {
      mSession->SendVideoFrame((unsigned char*)frame,
                                len,
                                width,
                                height,
                                mozilla::kVideoI420,
                                0);  
      usleep(33 * 1000);
      vidStatsGlobal.numRawFramesInserted++;
      numFrames--;
    } while(numFrames >= 0);
    PR_Free(frame);
  }

private:
mozilla::RefPtr<mozilla::VideoSessionConduit> mSession;
int width, height;
};



/**
 * A Dummy AudioConduit Tester
 * The test reads PCM samples of a standard test file and 
 * passws to audio-conduit for encoding, RTPfication and 
 * decoding ebery 10 milliseconds.
 * This decoded samples are read-off the conduit for writing 
 * into output audio file in PCM format.
 */
class AudioSendAndReceive
{
public:
  static const unsigned int PLAYOUT_SAMPLE_FREQUENCY; //default is 16000
  static const unsigned int PLAYOUT_SAMPLE_LENGTH; //default is 160

  AudioSendAndReceive()
  {
  }

  ~AudioSendAndReceive()
  {
  }

 void Init(mozilla::RefPtr<mozilla::AudioSessionConduit> aSession, 
           mozilla::RefPtr<mozilla::AudioSessionConduit> aOtherSession,
           std::string fileIn, std::string fileOut)
  {
   
    mSession = aSession;  
    mOtherSession = aOtherSession;  
    iFile = fileIn;
    oFile = fileOut;
 }

  //Kick start the test
  void GenerateAndReadSamples();  

private:

  mozilla::RefPtr<mozilla::AudioSessionConduit> mSession;
  mozilla::RefPtr<mozilla::AudioSessionConduit> mOtherSession;
  std::string iFile;
  std::string oFile;
};

const unsigned int AudioSendAndReceive::PLAYOUT_SAMPLE_FREQUENCY = 16000;
const unsigned int AudioSendAndReceive::PLAYOUT_SAMPLE_LENGTH  = 160;

//Hardcoded for 16 bit samples for now
void AudioSendAndReceive::GenerateAndReadSamples()
{
   int16_t audioInput[PLAYOUT_SAMPLE_LENGTH]; 
   int16_t audioOutput[PLAYOUT_SAMPLE_LENGTH];
   int sampleLengthDecoded = 0;

   int sampleLengthInBits = PLAYOUT_SAMPLE_LENGTH * sizeof(short);

   memset(audioInput,0,sampleLengthInBits);
   memset(audioOutput,0,sampleLengthInBits);

   FILE* inFile    = fopen( iFile.c_str(), "rb");
   FILE* outFile   = fopen( oFile.c_str(), "wb");

   bool finish = false;
   //loop thru the samples for 6 seconds
   int t = 0;
   do
   {
     int read_ = fread(audioInput,1,sampleLengthInBits,inFile);

     if(read_ != sampleLengthInBits)
      {
        finish = true;
        printf("\n Couldn't read %d bytes.. Exiting ", sampleLengthInBits);
        break;
      }

      mSession->SendAudioFrame(audioInput,
                               PLAYOUT_SAMPLE_LENGTH,
                               PLAYOUT_SAMPLE_FREQUENCY,10);

      usleep(10*1000);
      mOtherSession->GetAudioFrame(audioOutput, PLAYOUT_SAMPLE_FREQUENCY,
                                   10, sampleLengthDecoded);
      if(sampleLengthDecoded == 0)
      {
        printf("\n Zero length Sample "); 
      }

      int wrote_  = fwrite (audioOutput, 1 , sampleLengthInBits, outFile);
      if(wrote_ != sampleLengthInBits)
      {
        finish = true;
        printf("\n Couldn't Write  %d bytes.. Exiting ", sampleLengthInBits);
        break; 
      }

      t += 10;   
      if(t > 6000)
        break; 

   }while(finish == false);
  
   fclose(inFile);
   fclose(outFile);
}

/**
 * Dummy Video Target for the conduit
 * This class acts as renderer attached to the video conuit
 * As of today we just verify if the frames rendered are exactly
 * the same as frame inserted at the first place
 */

mozilla::VideoRenderer::~VideoRenderer()
{}

class DummyVideoTarget: public mozilla::VideoRenderer
{
public:
  DummyVideoTarget()
  {
  }

  virtual ~DummyVideoTarget()
  {
  }


  void RenderVideoFrame(const unsigned char* buffer,
                        unsigned int buffer_size,
                        uint32_t time_stamp,
                        int64_t render_time)
 {
  //write the frame to the file
  if(VerifyFrame(buffer, buffer_size) == 0)
  {
      vidStatsGlobal.numFramesRenderedSuccessfully++;
  } else
  {
      vidStatsGlobal.numFramesRenderedWrongly++;
  }
 }

 void FrameSizeChange(unsigned int, unsigned int, unsigned int)
 {
    //do nothing
 }
 
 //This is hardcoded to check if the contents of frame is COLOR 
 // as we set while sending.
 int VerifyFrame(const unsigned char* buffer, unsigned int buffer_size)
 {
    int good = 0;
    for(int i=0; i < (int) buffer_size; i++)
    {
      if(buffer[i] == COLOR)
      {
        ++good;
      }
      else
      {
        --good;
      }
    }
   return 0;
 }

};


mozilla::TransportInterface::~TransportInterface()
{}

/**
 *  Fake Audio and Video External Transport Class
 *  The functions in this class will be invoked by the conduit
 *  when it has RTP/RTCP frame to transmit.
 *  For everty RTP/RTCP frame we receive, we pass it back
 *  to the conduit for eventual decoding and rendering.
 */
class FakeMediaTransport : public mozilla::TransportInterface 
{
public:
  FakeMediaTransport():numPkts(0),
                       mAudio(false),
                       mVideo(false)
  {
  }

  ~FakeMediaTransport()
  {
  }

  virtual nsresult SendRtpPacket(const void* data, int len)
  {
    ++numPkts;
    if(mAudio)
    {
      mOtherAudioSession->ReceivedRTPPacket(data,len);
    } else 
    {
      mOtherVideoSession->ReceivedRTPPacket(data,len);
    }
    return NS_OK;
  }

  virtual nsresult SendRtcpPacket(const void* data, int len)
  {
    if(mAudio)
    {
      mOtherAudioSession->ReceivedRTCPPacket(data,len);
    } else
    {
      mOtherVideoSession->ReceivedRTCPPacket(data,len);
    }
    return NS_OK;
  }

  //Treat this object as Audio Transport
  void SetAudioSession(mozilla::RefPtr<mozilla::AudioSessionConduit> aSession,
                        mozilla::RefPtr<mozilla::AudioSessionConduit>
                        aOtherSession)
  {
    mAudioSession = aSession;
    mOtherAudioSession = aOtherSession;
    mAudio = true;
  }

  // Treat this object as Video Transport
  void SetVideoSession(mozilla::RefPtr<mozilla::VideoSessionConduit> aSession,
                       mozilla::RefPtr<mozilla::VideoSessionConduit>
                       aOtherSession)
  {
    mVideoSession = aSession;
    mOtherVideoSession = aOtherSession;
    mVideo = true;
  }

private:
  mozilla::RefPtr<mozilla::AudioSessionConduit> mAudioSession;
  mozilla::RefPtr<mozilla::VideoSessionConduit> mVideoSession;
  mozilla::RefPtr<mozilla::VideoSessionConduit> mOtherVideoSession;
  mozilla::RefPtr<mozilla::AudioSessionConduit> mOtherAudioSession;
  int numPkts;
  bool mAudio, mVideo;
};


namespace {

class TransportConduitTest : public ::testing::Test 
{
 public:
  TransportConduitTest() 
  {
    //input and output file names
    iAudiofilename = "audio_short16.pcm";
    oAudiofilename = "recorded.pcm";
    std::string rootpath = ProjectRootPath();

    fileToPlay = rootpath+"media"+kPathDelimiter+"webrtc"+kPathDelimiter+"trunk"+kPathDelimiter+"test"+kPathDelimiter+"data"+kPathDelimiter+"voice_engine"+kPathDelimiter+iAudiofilename;

    fileToRecord =
                  rootpath+kPathDelimiter+"media"+kPathDelimiter+"webrtc"+kPathDelimiter+"signaling"+kPathDelimiter+"test"+kPathDelimiter+oAudiofilename;

  }

  ~TransportConduitTest() 
  {
  }

  //1. Dump audio samples to dummy external transport
  void TestDummyAudioAndTransport() 
  {
    //get pointer to AudioSessionConduit
    int err=0;
    mAudioSession = mozilla::AudioSessionConduit::Create();
    if( !mAudioSession )
      ASSERT_TRUE(mAudioSession != NULL);

    mAudioSession2 = mozilla::AudioSessionConduit::Create();
    if( !mAudioSession2 )
      ASSERT_TRUE(mAudioSession2 != NULL);

    FakeMediaTransport* xport = new FakeMediaTransport();
    ASSERT_TRUE(xport != NULL);
    xport->SetAudioSession(mAudioSession, mAudioSession2);
    mAudioTransport = xport;

    // attach the transport to audio-conduit
    err = mAudioSession->AttachTransport(mAudioTransport);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);
    err = mAudioSession2->AttachTransport(mAudioTransport);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);

    //configure send and recv codecs on the audio-conduit
    mozilla::AudioCodecConfig cinst1(124,"PCMU",8000,80,1,64000); 
    mozilla::AudioCodecConfig cinst2(125,"L16",16000,320,1,256000);


    std::vector<mozilla::AudioCodecConfig*> rcvCodecList;
    rcvCodecList.push_back(&cinst1);
    rcvCodecList.push_back(&cinst2);

    err = mAudioSession->ConfigureSendMediaCodec(&cinst1);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);
    err = mAudioSession->ConfigureRecvMediaCodecs(rcvCodecList);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);

    err = mAudioSession2->ConfigureSendMediaCodec(&cinst1);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);
    err = mAudioSession2->ConfigureRecvMediaCodecs(rcvCodecList);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);

    //start generating samples
    audioTester.Init(mAudioSession,mAudioSession2, fileToPlay,fileToRecord);
    printf("\n ********************************************************\n");
    printf("\n           Generating Samples for 6 seconds ");
    printf("\n *********************************************************\n");
    sleep(2);
    audioTester.GenerateAndReadSamples();
    sleep(2);
    printf("\n ********************************************************\n");
    printf("\n Output Audio Recorded to %s ", fileToRecord.c_str()); 
    printf("\n Run webrtc_ex-playout for verifying the test results ..");
    printf("\n *********************************************************\n");
  }

  //2. Dump audio samples to dummy external transport
  void TestDummyVideoAndTransport()
  {
    int err = 0;
    //get pointer to VideoSessionConduit
    mVideoSession = mozilla::VideoSessionConduit::Create();
    if( !mVideoSession )
      ASSERT_TRUE(mVideoSession != NULL);

   // This session is for other one
    mVideoSession2 = mozilla::VideoSessionConduit::Create();
    if( !mVideoSession2 )
      ASSERT_TRUE(mVideoSession2 != NULL);

    mVideoRenderer = new DummyVideoTarget();
    ASSERT_TRUE(mVideoRenderer != NULL);

    FakeMediaTransport* xport = new FakeMediaTransport();
    ASSERT_TRUE(xport != NULL);
    xport->SetVideoSession(mVideoSession,mVideoSession2);
    mVideoTransport = xport;

    // attach the transport and renderer to video-conduit
    err = mVideoSession2->AttachRenderer(mVideoRenderer);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);
    err = mVideoSession->AttachTransport(mVideoTransport);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);
    err = mVideoSession2->AttachTransport(mVideoTransport);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);

    //configure send and recv codecs on theconduit
    mozilla::VideoCodecConfig cinst1(120, "VP8", 640, 480);
    mozilla::VideoCodecConfig cinst2(124, "I420", 640, 480);


    std::vector<mozilla::VideoCodecConfig* > rcvCodecList;
    rcvCodecList.push_back(&cinst1);
    rcvCodecList.push_back(&cinst2);

    err = mVideoSession->ConfigureSendMediaCodec(&cinst1);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);
    err = mVideoSession->ConfigureRecvMediaCodecs(rcvCodecList);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);

    err = mVideoSession2->ConfigureSendMediaCodec(&cinst1);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);
    err = mVideoSession2->ConfigureRecvMediaCodecs(rcvCodecList);
    ASSERT_EQ(mozilla::kMediaConduitNoError, err);

    //start generating samples
    printf("\n **************************************\n");
    printf("\n  Starting the Video Sample Generation ");
    printf("\n **************************************\n");
    sleep(1);
    videoTester.Init(mVideoSession);
    videoTester.GenerateAndReadSamples();
    sleep(1);
    printf("\n **************************************\n");
    printf("\n      Done With The Testing                  ");
    printf("\n VIDEO TEST STATS:                      \n");
    printf("\n Num Raw Frames Inserted            :%d ",
                                      vidStatsGlobal.numRawFramesInserted);
    printf("\n Num Frames Successfuly Rendered    :%d ",
                                      vidStatsGlobal.numFramesRenderedSuccessfully);
    printf("\n Num Frames Wrongly Rendered        :%d ",
                                      vidStatsGlobal.numFramesRenderedWrongly);
    printf("\n **************************************\n");
    
  }

 void TestVideoConduitCodecAPI()
  {
    int err = 0;
    mozilla::RefPtr<mozilla::VideoSessionConduit> mVideoSession;
    //get pointer to VideoSessionConduit
    mVideoSession = mozilla::VideoSessionConduit::Create();
    if( !mVideoSession )
      ASSERT_TRUE(mVideoSession != NULL);

    //Test Configure Recv Codec APIS
    printf("\n **************************************\n");
    printf("\n  Test Receive Codec Configuration API Now ");
    printf("\n **************************************\n");
    std::vector<mozilla::VideoCodecConfig* > rcvCodecList;
    
    //Same APIs
    printf("\n **************************************\n");
    printf("\n  1. Same Codec Repeated Twice ");
    printf("\n **************************************\n");
    printf("\n Setting VP8 Twice ");
    mozilla::VideoCodecConfig cinst1(120, "VP8", 640, 480);
    mozilla::VideoCodecConfig cinst2(120, "VP8", 640, 480);
    rcvCodecList.push_back(&cinst1);
    rcvCodecList.push_back(&cinst2);
    err = mVideoSession->ConfigureRecvMediaCodecs(rcvCodecList);
    EXPECT_EQ(mozilla::kMediaConduitNoError, err);
    rcvCodecList.pop_back();
    rcvCodecList.pop_back();


    sleep(2);
    printf("\n **************************************\n");
    printf("\n  2. Codec With Invalid Payload Names ");
    printf("\n **************************************\n");
    printf("\n Setting Payload 1 with name: I4201234tttttthhhyyyy89087987y76t567r7756765rr6u6676");
    printf("\n Setting Payload 2 with name of zero length ");
    
    mozilla::VideoCodecConfig cinst3(124, "I4201234tttttthhhyyyy89087987y76t567r7756765rr6u6676", 352, 288);
    mozilla::VideoCodecConfig cinst4(124, "", 352, 288);

    rcvCodecList.push_back(&cinst3);
    rcvCodecList.push_back(&cinst4);

    err = mVideoSession->ConfigureRecvMediaCodecs(rcvCodecList);
    EXPECT_TRUE(err != mozilla::kMediaConduitNoError);
    rcvCodecList.pop_back();
    rcvCodecList.pop_back();


    sleep(2);
    printf("\n **************************************\n");
    printf("\n  3. Null Codec Parameter ");
    printf("\n **************************************\n");
   
    rcvCodecList.push_back(0);

    err = mVideoSession->ConfigureRecvMediaCodecs(rcvCodecList);
    EXPECT_TRUE(err != mozilla::kMediaConduitNoError);
    rcvCodecList.pop_back();

    printf("\n **************************************\n");
    printf("\n  Test Send Codec Configuration API Now ");
    printf("\n **************************************\n");
    sleep(2);

    printf("\n **************************************\n");
    printf("\n  1. Same Send Codec Twice ");
    printf("\n **************************************\n");
    err = mVideoSession->ConfigureSendMediaCodec(&cinst1);
    EXPECT_EQ(mozilla::kMediaConduitNoError, err);
    err = mVideoSession->ConfigureSendMediaCodec(&cinst1);
    EXPECT_EQ(mozilla::kMediaConduitNoError, err);
   
    printf("\n **************************************\n");
    printf("\n  2. Invalid Send Codec payload Name ");
    printf("\n **************************************\n");
    printf("\n Setting Payload with name: I4201234tttttthhhyyyy89087987y76t567r7756765rr6u6676");
    
    err = mVideoSession->ConfigureSendMediaCodec(&cinst3);
    EXPECT_TRUE(err != mozilla::kMediaConduitNoError);
    
    printf("\n **************************************\n");
    printf("\n  3. Null Send Codec Pointer ");
    printf("\n **************************************\n");    
    err = mVideoSession->ConfigureSendMediaCodec(NULL);
    EXPECT_TRUE(err != mozilla::kMediaConduitNoError);
    
  }

private:
  //Audio Conduit Test Objects
  mozilla::RefPtr<mozilla::AudioSessionConduit> mAudioSession;
  mozilla::RefPtr<mozilla::AudioSessionConduit> mAudioSession2;
  mozilla::RefPtr<mozilla::TransportInterface> mAudioTransport;
  AudioSendAndReceive audioTester;

  //Video Conduit Test Objects
  mozilla::RefPtr<mozilla::VideoSessionConduit> mVideoSession;
  mozilla::RefPtr<mozilla::VideoSessionConduit> mVideoSession2;
  mozilla::RefPtr<mozilla::VideoRenderer> mVideoRenderer;
  mozilla::RefPtr<mozilla::TransportInterface> mVideoTransport;
  VideoSendAndReceive videoTester;

  std::string fileToPlay;
  std::string fileToRecord;
  std::string iAudiofilename;
  std::string oAudiofilename;
};


// Test 1: Test Dummy External Xport
TEST_F(TransportConduitTest, TestDummyAudioWithTransport) {
  TestDummyAudioAndTransport();
}

// Test 2: Test Dummy External Xport
TEST_F(TransportConduitTest, TestDummyVideoWithTransport) {
  TestDummyVideoAndTransport();
 }

TEST_F(TransportConduitTest, TestVideoConduitCodecAPI) {
  TestVideoConduitCodecAPI();
 }

}  // end namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


