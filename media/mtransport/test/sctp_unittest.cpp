/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include <iostream>
#include <string>
#include <map>

// From libjingle.
#include <talk/base/sigslot.h>

#include "nsThreadUtils.h"
#include "nsXPCOM.h"

#include "transportflow.h"
#include "transportlayer.h"

#include "logging.h"
#include "mtransport_test_utils.h"
#include "runnable_utils.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"


MLOG_INIT("mtransport");

MtransportTestUtils test_utils;

#if 0

namespace {
class TransportTestPeer : public sigslot::has_slots<> {
 public:
  TransportTestPeer(nsCOMPtr<nsIEventTarget> target, std::string name)
      : name_(name), target_(target),
        received_(0), flow_(),
        prsock_(new TransportLayerPrsock()),
        peer_(NULL),
        gathering_complete_(false) {}

  void ConnectSocket(PRFileDesc *fd) {
    nsresult res;
    res = prsock_->Init();
    ASSERT_EQ((nsresult)NS_OK, res);

    target_->Dispatch(WrapRunnable(prsock_, &TransportLayerPrsock::Import,
                                   fd, &res), NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(res));
    ASSERT_EQ((nsresult)NS_OK, flow_.PushLayer(prsock_));

    flow_.SignalPacketReceived.connect(this, &TransportTestPeer::PacketReceived);
  }

  TransportResult SendPacket(const unsigned char* data, size_t len) {
    return flow_.SendPacket(data, len);
  }


  void PacketReceived(TransportFlow * flow, const unsigned char* data,
                      size_t len) {
    std::cerr << "Received " << len << " bytes" << std::endl;
    ++received_;
  }

  void SetLoss(PRUint32 loss) {
    lossy_->SetLoss(loss);
  }

  bool connected() {
    return flow_.state() == TransportLayer::OPEN;
  }

  size_t received() { return received_; }

 private:
  std::string name_;
  size_t received_;
  TransportFlow flow_;
  TransportLayerPrsock *prsock_;
  TransportTestPeer *peer_;
};


class TransportTest : public ::testing::Test {
 public:
  TransportTest() {
    fds_[0] = NULL;
    fds_[1] = NULL;
  }

  ~TransportTest() {
    delete p1_;
    delete p2_;
  }

  void SetUp() {
    p1_ = new TransportTestPeer(target_, "P1");
    p2_ = new TransportTestPeer(target_, "P2");
  }

  void ConnectSocket() {

    PRStatus status = PR_NewTCPSocketPair(fds_);
    ASSERT_EQ(status, PR_SUCCESS);

    PRSocketOptionData opt;
    opt.option = PR_SockOpt_Nonblocking;
    opt.value.non_blocking = PR_FALSE;
    status = PR_SetSocketOption(fds_[0], &opt);
    ASSERT_EQ(status, PR_SUCCESS);
    status = PR_SetSocketOption(fds_[1], &opt);
    ASSERT_EQ(status, PR_SUCCESS);

    p1_->ConnectSocket(fds_[0]);
    p2_->ConnectSocket(fds_[1]);
    ASSERT_TRUE_WAIT(p1_->connected(), 10000);
    ASSERT_TRUE_WAIT(p2_->connected(), 10000);
  }

 protected:
  PRFileDesc *fds_[2];
  TransportTestPeer *p1_;
  TransportTestPeer *p2_;
};

}  // end namespace
#endif

int main(int argc, char **argv)
{
  test_utils.InitServices();
  // Start the tests
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}

