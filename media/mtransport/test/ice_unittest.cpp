/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include <iostream>
#include <map>
#include <string>
#include <vector>

// From libjingle.
#include <talk/base/sigslot.h>

#include "nspr.h"
#include "nss.h"
#include "ssl.h"

#include "nsThreadUtils.h"
#include "nsXPCOM.h"

#include "transportlayerice.h"
#include "logging.h"
#include "mtransport_test_utils.h"
#include "runnable_utils.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

MLOG_INIT("ice");

MtransportTestUtils test_utils;

namespace {

class IceTestPeer : public sigslot::has_slots<> {
 public:
  IceTestPeer(const std::string& name, bool offerer) :
      ice_ctx_(NrIceCtx::Create(name, offerer)),
      streams_(),
      candidates_(),
      gathering_complete_(false) {
    ice_ctx_->SignalGatheringComplete.connect(this,
                                              &IceTestPeer::GatheringComplete);
  }

  void AddStream(const std::string& name, int components) {
    mozilla::RefPtr<NrIceMediaStream> stream = 
        ice_ctx_->CreateStream(name, components);
    
    ASSERT_TRUE(stream != NULL);
    streams_.push_back(stream);
    stream->SignalCandidate.connect(this, &IceTestPeer::GotCandidate);
  }

  void Gather() {
    nsresult res;

    test_utils.sts_target()->Dispatch(
        WrapRunnableRet(ice_ctx_, &NrIceCtx::StartGathering, &res),
        NS_DISPATCH_SYNC);

    ASSERT_TRUE(NS_SUCCEEDED(res));
  }

  // Get various pieces of state
  std::vector<std::string> GetGlobalAttributes() {
    return ice_ctx_->GetGlobalAttributes();
  }

  std::vector<std::string>& GetCandidates(const std::string &name) {
    return candidates_[name];
  }

  bool gathering_complete() { return gathering_complete_; }

  
  // Start connecting to another peer
  void Connect(IceTestPeer *remote) {
    nsresult res;

    test_utils.sts_target()->Dispatch(
      WrapRunnableRet(ice_ctx_, 
        &NrIceCtx::ParseGlobalAttributes, remote->GetGlobalAttributes(), &res),
      NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(res));
    
    for (size_t i=0; i<streams_.size(); ++i) {
      test_utils.sts_target()->Dispatch(
        WrapRunnableRet(streams_[i], &NrIceMediaStream::ParseCandidates,
          remote->GetCandidates(streams_[i]->name()), &res),
      NS_DISPATCH_SYNC);

      ASSERT_TRUE(NS_SUCCEEDED(res));
    }

    // Now start checks
    test_utils.sts_target()->Dispatch(
      WrapRunnableRet(ice_ctx_, &NrIceCtx::StartChecks, &res),
      NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(res));
  }

  // Handle events
  void GatheringComplete(NrIceCtx *ctx) {
    gathering_complete_ = true;
  }

  void GotCandidate(NrIceMediaStream *stream, const std::string &candidate) {
    std::cout << "Got candidate " << candidate << std::endl;
    candidates_[stream->name()].push_back(candidate);
  }


 private:
  nsRefPtr<NrIceCtx> ice_ctx_;
  std::vector<mozilla::RefPtr<NrIceMediaStream> > streams_;
  std::map<std::string, std::vector<std::string> > candidates_;
  bool gathering_complete_;
};

class IceTest : public ::testing::Test {
 public:
  IceTest() : p1_("P1", true), p2_("P2", false) {}
  
  void SetUp() {
    nsresult rv;
    target_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
    ASSERT_TRUE(NS_SUCCEEDED(rv));
  }

  void AddStream(const std::string& name, int components) {
    p1_.AddStream(name, components);
    p2_.AddStream(name, components);
  }

  bool Gather(bool wait) {
    p1_.Gather();
    p2_.Gather();
    
    EXPECT_TRUE_WAIT(p1_.gathering_complete(), 10000);
    if (!p1_.gathering_complete())
      return false;
    EXPECT_TRUE_WAIT(p2_.gathering_complete(), 10000);
    if (!p2_.gathering_complete())
      return false;
    
    return true;
  }

  void Connect() {
    p1_.Connect(&p2_);
    p2_.Connect(&p1_);

    ASSERT_TRUE_WAIT(false, 10000);
  }

 protected:
  nsCOMPtr<nsIEventTarget> target_;
  IceTestPeer p1_;
  IceTestPeer p2_;
};

    
}  // end namespace


TEST_F(IceTest, TestGather) {
  AddStream("first", 1);
  ASSERT_TRUE(Gather(true));
}


TEST_F(IceTest, TestConnect) {
  AddStream("first", 1);
  ASSERT_TRUE(Gather(true));
  Connect();
}


int main(int argc, char **argv)
{
  test_utils.InitServices();
  NSS_NoDB_Init(NULL);
  NSS_SetDomesticPolicy();

  // Start the tests
  ::testing::InitGoogleTest(&argc, argv);
  
  return RUN_ALL_TESTS();
}
