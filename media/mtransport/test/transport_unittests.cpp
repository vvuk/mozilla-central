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
#include "transportlayerloopback.h"

#include "TestHarness.h"

MtransportTestUtils *test_utils;


class TransportTestPeer : public sigslot::has_slots<> {
 public:
  TransportTestPeer() : received_(0), flow_(), 
                        loopback_(new TransportLayerLoopback()),
                        logging_(new TransportLayerLogging()) {
    flow_.PushLayer(loopback_);
    flow_.PushLayer(logging_);
    logging_->SignalPacketReceived.connect(this,
                                           &TransportTestPeer::PacketReceived);
  }

  void Connect(TransportTestPeer* peer) {
    loopback_->Connect(peer->loopback_);
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
  TransportLayerLoopback *loopback_;
  TransportLayerLogging *logging_;
};

class TransportTest {
 public:
  void Connect() {
    p1_.Connect(&p2_);
    p2_.Connect(&p1_);
  }

  void TransferTest(size_t count) {
    unsigned char buf[1000];
    
    for (size_t i= 0; i<count; ++i) {
      memset(buf, count & 0xff, sizeof(buf));
      p1_.SendPacket(buf, sizeof(buf));
    }
    
    std::cout << "received => " << p2_.received() << std::endl;
  }

 private:
  TransportTestPeer p1_;
  TransportTestPeer p2_;
};

class MyEvent : public nsRunnable {
public:
    MyEvent() {};
    
    NS_IMETHOD Run() {
      // do stuff
      std::cout << "RAN!!!!!\n";

      return NS_OK;
  }
};

int main(int argc, char **argv)
{
  test_utils = new MtransportTestUtils();

  return 0;
}
