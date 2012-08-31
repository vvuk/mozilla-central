/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef mtransport_test_utils_h__
#define mtransport_test_utils_h__

#include "nsCOMPtr.h"
#include "nsNetCID.h"
#include "nsXPCOMGlue.h"
#include "nsXPCOM.h"

#include "nsIComponentManager.h"
#include "nsIComponentRegistrar.h"
#include "nsIIOService.h"
#include "nsIServiceManager.h"
#include "nsISocketTransportService.h"
#include "nsDirectoryServiceUtils.h"
#include "nsDirectoryServiceDefs.h"
#ifdef MOZ_CRASHREPORTER
#include "nsICrashReporter.h"
#endif

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

    sts_target_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
    if (!NS_SUCCEEDED(rv))
      return false;

#ifdef MOZ_CRASHREPORTER
    //TODO: move this to an even-more-common location to use in all
    // C++ unittests
    crashreporter_ = do_GetService("@mozilla.org/toolkit/crash-reporter;1");
    if (crashreporter_) {
      nsCOMPtr<nsIProperties> dirsvc =
	do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID);
      nsCOMPtr<nsIFile> cwd;
      rv = dirsvc->Get(NS_XPCOM_CURRENT_PROCESS_DIR,
		       NS_GET_IID(nsIFile),
		       getter_AddRefs(cwd));
      if (!NS_SUCCEEDED(rv))
	return false;
      crashreporter_->SetEnabled(true);
      crashreporter_->SetMinidumpPath(cwd);
    }
#endif
    return true;
  }

  nsCOMPtr<nsIEventTarget> sts_target() { return sts_target_; }

 private:
  nsCOMPtr<nsIServiceManager> servMan_;
  nsCOMPtr<nsIComponentManager> manager_;
  nsCOMPtr<nsIIOService> ioservice_;
  nsCOMPtr<nsIEventTarget> sts_target_;
#ifdef MOZ_CRASHREPORTER
  nsCOMPtr<nsICrashReporter> crashreporter_;
#endif
};


#endif

