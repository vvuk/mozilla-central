/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include <iostream>
#include <string>
#include <map>

// From libjingle.
#include <talk/base/sigslot.h>

#include "nspr.h"
#include "nss.h"
#include "ssl.h"

#include "nsThreadUtils.h"
#include "nsXPCOM.h"

#include "dtlsidentity.h"
#include "nricectx.h"
#include "nricemediastream.h"
#include "transportflow.h"
#include "transportlayer.h"
#include "transportlayerdtls.h"
#include "transportlayerice.h"
#include "transportlayerlog.h"
#include "transportlayerloopback.h"

#include "logging.h"
#include "mtransport_test_utils.h"
#include "runnable_utils.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

MLOG_INIT("mtransport");

MtransportTestUtils test_utils;


// Class to simulate various kinds of network lossage
class TransportLayerLossy : public TransportLayer {
 public:
  TransportLayerLossy() : loss_mask_(0), packet_(0) {}

  virtual TransportResult SendPacket(const unsigned char *data, size_t len) {
    MLOG(PR_LOG_NOTICE, LAYER_INFO << "SendPacket(" << len << ")");

    if (loss_mask_ & (1 << (packet_ % 32))) {
      MLOG(PR_LOG_NOTICE, "Dropping packet");
      ++packet_;
      return len;
    }

    ++packet_;

    return downward_->SendPacket(data, len);
  }

  void SetLoss(PRUint32 packet) {
    loss_mask_ |= (1 << (packet & 32));
  }

  void StateChange(TransportLayer *layer, State state) {
    SetState(state);
  }

  void PacketReceived(TransportLayer *layer, const unsigned char *data,
                      size_t len) {
    SignalPacketReceived(this, data, len);
  }

  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

 protected:
  virtual void WasInserted() {
    downward_->SignalPacketReceived.
        connect(this,
                &TransportLayerLossy::PacketReceived);
    downward_->SignalStateChange.
        connect(this,
                &TransportLayerLossy::StateChange);

    SetState(downward_->state());
  }

 private:
  PRUint32 loss_mask_;
  PRUint32 packet_;
};

std::string TransportLayerLossy::ID = "lossy";


namespace {
class TransportTestPeer : public sigslot::has_slots<> {
 public:
  TransportTestPeer(nsCOMPtr<nsIEventTarget> target, std::string name)
      : name_(name), target_(target),
        received_(0), flow_(name),
        loopback_(new TransportLayerLoopback()),
        logging_(new TransportLayerLogging()),
        lossy_(new TransportLayerLossy()),
        dtls_(new TransportLayerDtls()),
        identity_(DtlsIdentity::Generate(name)),
        ice_ctx_(NrIceCtx::Create(name,
                                  name == "P2" ?
                                  TransportLayerDtls::CLIENT :
                                  TransportLayerDtls::SERVER)),
        streams_(), candidates_(),
        peer_(NULL),
        gathering_complete_(false)
 {
    dtls_->SetIdentity(identity_);
    dtls_->SetRole(name == "P2" ?
                   TransportLayerDtls::CLIENT :
                   TransportLayerDtls::SERVER);

    nsresult res = identity_->ComputeFingerprint("sha-1",
                                             fingerprint_,
                                             sizeof(fingerprint_),
                                             &fingerprint_len_);
    EXPECT_TRUE(NS_SUCCEEDED(res));
    EXPECT_EQ(20, fingerprint_len_);
  }

  ~TransportTestPeer() {
    loopback_->Disconnect();
  }


  void SetDtlsAllowAll() {
    nsresult res = dtls_->SetVerificationAllowAll();
    ASSERT_TRUE(NS_SUCCEEDED(res));
  }
  void SetDtlsPeer(TransportTestPeer *peer, bool damage) {
    unsigned char fingerprint_to_set[TransportLayerDtls::kMaxDigestLength];
    memcpy(fingerprint_to_set,
           peer->fingerprint_,
           peer->fingerprint_len_);
    if (damage)
      fingerprint_to_set[0]++;

    nsresult res = dtls_->SetVerificationDigest(
        "sha-1",
        fingerprint_to_set,
        peer->fingerprint_len_);

    ASSERT_TRUE(NS_SUCCEEDED(res));
  }


  void ConnectSocket(TransportTestPeer *peer) {
    nsresult res;
    res = loopback_->Init();
    ASSERT_EQ((nsresult)NS_OK, res);

    loopback_->Connect(peer->loopback_);

    ASSERT_EQ((nsresult)NS_OK, flow_.PushLayer(loopback_));
    ASSERT_EQ((nsresult)NS_OK, flow_.PushLayer(logging_));
    ASSERT_EQ((nsresult)NS_OK, flow_.PushLayer(lossy_));
    ASSERT_EQ((nsresult)NS_OK, flow_.PushLayer(dtls_));

    flow_.SignalPacketReceived.connect(this, &TransportTestPeer::PacketReceived);
  }

  void InitIce() {
    nsresult res;

    // Attach our slots
    ice_ctx_->SignalGatheringCompleted.
        connect(this, &TransportTestPeer::GatheringComplete);

    char name[100];
    snprintf(name, sizeof(name), "%s:stream%d", name_.c_str(),
             (int)streams_.size());

    // Create the media stream
    mozilla::RefPtr<NrIceMediaStream> stream =
        ice_ctx_->CreateStream(static_cast<char *>(name), 1);
    ASSERT_TRUE(stream != NULL);
    streams_.push_back(stream);

    // Listen for candidates
    stream->SignalCandidate.
        connect(this, &TransportTestPeer::GotCandidate);

    // Create the transport layer
    ice_ = new TransportLayerIce(name, ice_ctx_, stream, 1);

    // Assemble the stack
    ASSERT_EQ((nsresult)NS_OK, flow_.PushLayer(ice_));
    ASSERT_EQ((nsresult)NS_OK, flow_.PushLayer(dtls_));

    // Listen for media events
    flow_.SignalPacketReceived.connect(this, &TransportTestPeer::PacketReceived);
    flow_.SignalStateChange.connect(this, &TransportTestPeer::StateChanged);

    // Start gathering
    test_utils.sts_target()->Dispatch(
        WrapRunnableRet(ice_ctx_, &NrIceCtx::StartGathering, &res),
        NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(res));
  }

  void ConnectIce(TransportTestPeer *peer) {
    peer_ = peer;

    // If gathering is already complete, push the candidates over
    if (gathering_complete_)
      GatheringComplete(ice_ctx_);
  }

  // New candidate
  void GotCandidate(NrIceMediaStream *stream, const std::string &candidate) {
    std::cerr << "Got candidate " << candidate << std::endl;
    candidates_[stream->name()].push_back(candidate);
  }

  // Gathering complete, so send our candidates and start
  // connecting on the other peer.
  void GatheringComplete(NrIceCtx *ctx) {
    nsresult res;

    // Don't send to the other side
    if (!peer_) {
      gathering_complete_ = true;
      return;
    }

    // First send attributes
    test_utils.sts_target()->Dispatch(
      WrapRunnableRet(peer_->ice_ctx_,
                      &NrIceCtx::ParseGlobalAttributes,
                      ice_ctx_->GetGlobalAttributes(), &res),
      NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(res));

    for (size_t i=0; i<streams_.size(); ++i) {
      test_utils.sts_target()->Dispatch(
        WrapRunnableRet(peer_->streams_[i], &NrIceMediaStream::ParseCandidates,
                        candidates_[streams_[i]->name()], &res), NS_DISPATCH_SYNC);

      ASSERT_TRUE(NS_SUCCEEDED(res));
    }

    // Start checks on the other peer.
    test_utils.sts_target()->Dispatch(
      WrapRunnableRet(peer_->ice_ctx_, &NrIceCtx::StartChecks, &res),
      NS_DISPATCH_SYNC);
    ASSERT_TRUE(NS_SUCCEEDED(res));
  }

  TransportResult SendPacket(const unsigned char* data, size_t len) {
    return flow_.SendPacket(data, len);
  }


  void StateChanged(TransportFlow *flow, TransportLayer::State state) {
    if (state == TransportLayer::OPEN) {
      std::cerr << "Now connected" << std::endl;
    }
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

  bool failed() {
    return flow_.state() == TransportLayer::ERROR;
  }

  size_t received() { return received_; }

 private:
  std::string name_;
  nsCOMPtr<nsIEventTarget> target_;
  size_t received_;
  TransportFlow flow_;
  TransportLayerLoopback *loopback_;
  TransportLayerLogging *logging_;
  TransportLayerLossy *lossy_;
  TransportLayerDtls *dtls_;
  TransportLayerIce *ice_;
  mozilla::RefPtr<DtlsIdentity> identity_;
  mozilla::RefPtr<NrIceCtx> ice_ctx_;
  std::vector<mozilla::RefPtr<NrIceMediaStream> > streams_;
  std::map<std::string, std::vector<std::string> > candidates_;
  TransportTestPeer *peer_;
  bool gathering_complete_;
  unsigned char fingerprint_[TransportLayerDtls::kMaxDigestLength];
  size_t fingerprint_len_;
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

    //    Can't detach these
    //    PR_Close(fds_[0]);
    //    PR_Close(fds_[1]);
  }

  void SetUp() {
    nsresult rv;
    target_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
    ASSERT_TRUE(NS_SUCCEEDED(rv));

    p1_ = new TransportTestPeer(target_, "P1");
    p2_ = new TransportTestPeer(target_, "P2");
  }

  void SetDtlsPeer(bool damage = false) {
    p1_->SetDtlsPeer(p2_, damage);
    p2_->SetDtlsPeer(p1_, damage);
  }

  void SetDtlsAllowAll() {
    p1_->SetDtlsAllowAll();
    p2_->SetDtlsAllowAll();
  }

  void ConnectSocket() {
    p1_->ConnectSocket(p2_);
    p2_->ConnectSocket(p1_);
    ASSERT_TRUE_WAIT(p1_->connected(), 10000);
    ASSERT_TRUE_WAIT(p2_->connected(), 10000);
  }

  void ConnectSocketExpectFail() {
    p1_->ConnectSocket(p2_);
    p2_->ConnectSocket(p1_);
    ASSERT_TRUE_WAIT(p1_->failed(), 10000);
    ASSERT_TRUE_WAIT(p2_->failed(), 10000);
  }

  void InitIce() {
    p1_->InitIce();
    p2_->InitIce();
  }

  void ConnectIce() {
    p1_->InitIce();
    p2_->InitIce();
    p1_->ConnectIce(p2_);
    p2_->ConnectIce(p1_);
    ASSERT_TRUE_WAIT(p1_->connected(), 10000);
    ASSERT_TRUE_WAIT(p2_->connected(), 10000);
  }

  void TransferTest(size_t count) {
    unsigned char buf[1000];

    for (size_t i= 0; i<count; ++i) {
      memset(buf, count & 0xff, sizeof(buf));
      TransportResult rv = p1_->SendPacket(buf, sizeof(buf));
      ASSERT_TRUE(rv > 0);
    }

    std::cerr << "Received == " << p2_->received() << std::endl;
    ASSERT_TRUE_WAIT(count == p2_->received(), 10000);
  }

 protected:
  PRFileDesc *fds_[2];
  TransportTestPeer *p1_;
  TransportTestPeer *p2_;
  nsCOMPtr<nsIEventTarget> target_;
};


TEST_F(TransportTest, TestNoDtlsVerificationSettings) {
  ConnectSocketExpectFail();
}

TEST_F(TransportTest, TestConnect) {
  SetDtlsPeer();
  ConnectSocket();
}

TEST_F(TransportTest, TestConnectAllowAll) {
  SetDtlsAllowAll();
  ConnectSocket();
}

TEST_F(TransportTest, TestConnectBadDigest) {
  SetDtlsPeer(true);
  ConnectSocketExpectFail();
}

TEST_F(TransportTest, TestTransfer) {
  SetDtlsPeer();
  ConnectSocket();
  TransferTest(1);
}

TEST_F(TransportTest, TestConnectLoseFirst) {
  SetDtlsPeer();
  p1_->SetLoss(0);
  ConnectSocket();
  TransferTest(1);
}

TEST_F(TransportTest, TestConnectIce) {
  SetDtlsPeer();
  ConnectIce();
}

TEST_F(TransportTest, TestTransferIce) {
  SetDtlsPeer();
  ConnectIce();
  TransferTest(1);
}

}  // end namespace

int main(int argc, char **argv)
{
  test_utils.InitServices();
  NSS_NoDB_Init(NULL);
  NSS_SetDomesticPolicy();
  // Start the tests
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
