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
#include "MediaPipeline.h"
#include "MediaConduitInterface.h"
#include "runnable_utils.h"
#include "transportflow.h"
#include "transportlayerprsock.h"


#include "mtransport_test_utils.h"

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
      pipeline_() {}
  
  void ConnectSocket(PRFileDesc *fd) {
    nsresult res;
    res = prsock_->Init();
    ASSERT_EQ((nsresult)NS_OK, res);
    
    test_utils.sts_target()->Dispatch(WrapRunnable(prsock_, &TransportLayerPrsock::Import,
                                   fd, &res), NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(res));
    
    ASSERT_EQ((nsresult)NS_OK, flow_.PushLayer(prsock_));
  }
  

 private:
  TransportFlow flow_;
  TransportLayerPrsock *prsock_;
  mozilla::RefPtr<mozilla::MediaPipeline> pipeline_;
};


class MediaPipelineTest : public ::testing::Test {
 public:
  MediaPipelineTest() {
    fds_[0] = fds_[1] = NULL;
  }
  
  void SetUp() {
    PRStatus status = PR_NewTCPSocketPair(fds_);
    ASSERT_EQ(status, PR_SUCCESS);

    p1_.ConnectSocket(fds_[0]);
    p2_.ConnectSocket(fds_[1]);
  }
  

 private:
  PRFileDesc *fds_[2];
  TestAgent p1_;
  TestAgent p2_;
};

TEST_F(MediaPipelineTest, INIT) {
}


}  // end namespace


int main(int argc, char **argv)
{
  test_utils.InitServices();
  // Start the tests
  ::testing::InitGoogleTest(&argc, argv);
  
  return RUN_ALL_TESTS();
}



