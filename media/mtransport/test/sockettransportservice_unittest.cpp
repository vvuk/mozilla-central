/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com
#include <iostream>

#include "nsCOMPtr.h"
#include "nsNetCID.h"
#include "nsXPCOMGlue.h"
#include "nsXPCOM.h"

#include "nsIComponentManager.h"
#include "nsIComponentRegistrar.h"
#include "nsIIOService.h"
#include "nsIServiceManager.h"

#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

class PacketReceived;

namespace {
class SocketTransportServiceTest : public ::testing::Test {
 public:
  SocketTransportServiceTest() : received_(0) {
  }

  void SetUp() {
    nsresult rv;

    target_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
    ASSERT_TRUE(NS_SUCCEEDED(rv));
  }

  void Run();

  void ReceivePacket() {
    ++received_;
  }

  size_t Received() {
    return received_;
  }

 private:
  nsCOMPtr<nsIEventTarget> target_;
  size_t received_;
};


class PacketReceived : public nsRunnable {
public:
  PacketReceived(SocketTransportServiceTest *test) :
      test_(test) {};
    
  NS_IMETHOD Run() {
    test_->ReceivePacket();
    return NS_OK;
  }

  SocketTransportServiceTest *test_;
};

TEST_F(SocketTransportServiceTest, Test) {
  Run();
}

void SocketTransportServiceTest::Run() {
  nsresult rv;

  target_->Dispatch(new PacketReceived(this), rv);
  ASSERT_TRUE(rv);
  ASSERT_TRUE_WAIT(Received() == 1, 10000);
}

}  // end namespace


int main(int argc, char **argv) {
  nsresult rv;

  // Start the IO and socket transport service
  nsCOMPtr<nsIServiceManager> servMan;
  NS_InitXPCOM2(getter_AddRefs(servMan), nsnull, nsnull);
  nsCOMPtr<nsIComponentManager> manager = do_QueryInterface(servMan);
  nsCOMPtr<nsIIOService> ioservice;

  rv = manager->CreateInstanceByContractID(NS_IOSERVICE_CONTRACTID,
                                            nsnull, NS_GET_IID(nsIIOService),
                                            getter_AddRefs(ioservice));
  assert(NS_SUCCEEDED(rv));
  
  // Start the tests
  ::testing::InitGoogleTest(&argc, argv);
  
  return RUN_ALL_TESTS();
}
