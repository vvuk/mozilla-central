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

class TestEvent : public nsRunnable {
public:
    TestEvent() {};
    
    NS_IMETHOD Run() {
      std::cout << "RAN!!!!!\n";

      return NS_OK;
  }
};

int main(int argc, char **argv) {
  nsresult rv;

  nsCOMPtr<nsIServiceManager> servMan;
  NS_InitXPCOM2(getter_AddRefs(servMan), nsnull, nsnull);
  
  nsCOMPtr<nsIComponentManager> manager = do_QueryInterface(servMan);

   // Create an instance of our component
   nsCOMPtr<nsIIOService> ioservice;
   rv = manager->CreateInstanceByContractID(NS_IOSERVICE_CONTRACTID,
                                            nsnull, NS_GET_IID(nsIIOService),
                                            getter_AddRefs(ioservice));
   
   nsCOMPtr<nsIEventTarget> eventTarget
     = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);

   rv = eventTarget->Dispatch(new TestEvent(), NS_DISPATCH_NORMAL);
}
