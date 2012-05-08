/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include <iostream>

// From libjingle.
#include <talk/base/sigslot.h>

#include "nsThreadUtils.h"
#include "nsXPCOM.h"

#include "mtransport_test_utils.h"

#include "transportflow.h"
#include "transportlayer.h"
#include "transportlayerlog.h"
#include "transportlayerprsock.h"

#include "mtransport_test_utils.h"
#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

MtransportTestUtils test_utils;

namespace {
class TransportTestPeer : public sigslot::has_slots<> {
 public:
  TransportTestPeer() : received_(0), flow_(), 
                        prsock_(new TransportLayerPrsock()),
                        logging_(new TransportLayerLogging()) {
  }

  void Connect(PRFileDesc *fd) {
    ASSERT_TRUE(NS_SUCCEEDED(prsock_->Import(fd, false)));
    flow_.PushLayer(prsock_);
    flow_.PushLayer(logging_);
  }

  void SendPacket(const unsigned char* data, size_t len) {
    flow_.top()->SendPacket(data, len);
  }

  void PacketReceived(TransportLayer* flow, const unsigned char* data,
                      size_t len) {
    ++received_;
  }

  size_t received() { return received_; }

 private:
  size_t received_;
  TransportFlow flow_;
  TransportLayerPrsock *prsock_;
  TransportLayerLogging *logging_;
};


class TransportTest : public ::testing::Test {
 public:
  TransportTest() : fd1_(NULL), fd2_(NULL) {}

  ~TransportTest() {
    PR_Close(fd1_);
    PR_Close(fd2_);
  }

  void Connect() {
    PRStatus status = PR_CreatePipe(&fd1_, &fd2_);
    ASSERT_EQ(status, PR_SUCCESS);

    p1_.Connect(fd1_);
    p2_.Connect(fd2_);
  }

  void TransferTest(size_t count) {
    unsigned char buf[1000];
    
    for (size_t i= 0; i<count; ++i) {
      memset(buf, count & 0xff, sizeof(buf));
      p1_.SendPacket(buf, sizeof(buf));
    }
    
    ASSERT_TRUE_WAIT(count == p2_.received(), 1000);
  }

 private:
  PRFileDesc *fd1_;
  PRFileDesc *fd2_;
  TransportTestPeer p1_;
  TransportTestPeer p2_;
};


TEST_F(TransportTest, TestTransfer) {
  Connect();
  TransferTest(1000);
}

}  // end namespace

int main(int argc, char **argv)
{
  test_utils.InitServices();

  // Start the tests
  ::testing::InitGoogleTest(&argc, argv);
  
  return RUN_ALL_TESTS();
}
