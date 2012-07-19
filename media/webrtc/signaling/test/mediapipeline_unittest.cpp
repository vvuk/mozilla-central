/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include <iostream>

// From libjingle.
#include <talk/base/sigslot.h>

#include "nsThreadUtils.h"
#include "nsXPCOM.h"

#include "logging.h"
#include "mozilla/RefPtr.h"
#include "FakeMediaStreams.h"
#include "FakeMediaStreamsImpl.h"
#include "MediaPipeline.h"
#include "MediaConduitInterface.h"
#include "runnable_utils.h"
#include "transportflow.h"
#include "transportlayerprsock.h"


#include "mtransport_test_utils.h"
#include "runnable_utils.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"


MLOG_INIT("mediapipeline");

MtransportTestUtils test_utils;


namespace {
class TestAgent {
 public:
  TestAgent() :
      flow_(),
      prsock_(new TransportLayerPrsock()),
      conduit_(),
      audio_(),
      pipeline_() {
  }
  
  void ConnectSocket(PRFileDesc *fd) {
    nsresult res;
    res = prsock_->Init();
    ASSERT_EQ((nsresult)NS_OK, res);
    
    test_utils.sts_target()->Dispatch(WrapRunnable(prsock_, &TransportLayerPrsock::Import,
                                   fd, &res), NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(res));
    
    ASSERT_EQ((nsresult)NS_OK, flow_.PushLayer(prsock_));
  }

 protected:
  TransportFlow flow_;
  TransportLayerPrsock *prsock_;
  mozilla::RefPtr<mozilla::MediaSessionConduit> conduit_;
  nsRefPtr<nsDOMMediaStream> audio_;
  mozilla::RefPtr<mozilla::MediaPipeline> pipeline_;
};

class TestAgentSend : public TestAgent {
 public:
  TestAgentSend() {
    audio_ = new Fake_nsDOMMediaStream(new Fake_AudioStreamSource());
    pipeline_ = new mozilla::MediaPipelineTransmit(audio_, conduit_, &flow_, &flow_);
  }

  void StartSending() {
    nsresult ret;
    
    MLOG(PR_LOG_DEBUG, "Starting sending");

    test_utils.sts_target()->Dispatch(
        WrapRunnableRet(audio_->GetStream(),
                        &Fake_MediaStream::Start, &ret),
        NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(ret));    
  }

  void StopSending() {
    nsresult ret;
    
    MLOG(PR_LOG_DEBUG, "Stopping sending");

    test_utils.sts_target()->Dispatch(
        WrapRunnableRet(audio_->GetStream(),
                        &Fake_MediaStream::Stop, &ret),
        NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(ret));    
  }
  
 private:
};


class MediaPipelineTest : public ::testing::Test {
 public:
  MediaPipelineTest() : p1_() {
    fds_[0] = fds_[1] = NULL;
  }
  
  void SetUp() {
    PRStatus status = PR_NewTCPSocketPair(fds_);
    ASSERT_EQ(status, PR_SUCCESS);

    p1_.ConnectSocket(fds_[0]);
    //    p2_.ConnectSocket(fds_[1]);
  }


 protected:
  PRFileDesc *fds_[2];
  TestAgentSend p1_;
  // TestAgent p2_;
};

TEST_F(MediaPipelineTest, AudioSend) {
  p1_.StartSending();
  ASSERT_TRUE_WAIT(false, 1000);
  p1_.StopSending();
}


}  // end namespace


int main(int argc, char **argv)
{
  test_utils.InitServices();
  // Start the tests
  ::testing::InitGoogleTest(&argc, argv);
  
  return RUN_ALL_TESTS();
}



