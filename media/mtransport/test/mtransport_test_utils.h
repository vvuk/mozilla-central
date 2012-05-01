/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "nsCOMPtr.h"
#include "nsNetCID.h"
#include "nsXPCOMGlue.h"
#include "nsXPCOM.h"

#include "nsIComponentManager.h"
#include "nsIComponentRegistrar.h"
#include "nsIIOService.h"
#include "nsIServiceManager.h"
#include "nsISocketTransportService.h"

#include "nsServiceManagerUtils.h"


class MtransportTestUtils {
 public:
  bool InitServices() {
    nsresult rv;

    NS_InitXPCOM2(getter_AddRefs(servMan_), nsnull, nsnull);
    manager_ = do_QueryInterface(servMan_);
    rv = manager_->CreateInstanceByContractID(NS_IOSERVICE_CONTRACTID,
                                             nsnull, NS_GET_IID(nsIIOService),
                                              getter_AddRefs(ioservice_));
    if (!NS_SUCCEEDED(rv))
      return false;

    return true;
  }
  
 private:
  nsCOMPtr<nsIServiceManager> servMan_;
  nsCOMPtr<nsIComponentManager> manager_;
  nsCOMPtr<nsIIOService> ioservice_;
};


