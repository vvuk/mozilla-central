
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original code by: ekr@rtfm.com

// Implementation of the NR timer interface

#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsIEventTarget.h"
#include "nsITimer.h"
#include "nsNetCID.h"

extern "C" {
#include "nr_api.h"
#include "async_timer.h"
}


class nrappkitTimerCallback : public nsITimerCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSITIMERCALLBACK

  nrappkitTimerCallback(NR_async_cb cb, void *cb_arg) : cb_(cb), cb_arg_(cb_arg) {}

private:
  virtual ~nrappkitTimerCallback() {}

protected:
  /* additional members */
  NR_async_cb cb_;
  void *cb_arg_;
};

NS_IMPL_ISUPPORTS1(nrappkitTimerCallback, nsITimerCallback)

NS_IMETHODIMP nrappkitTimerCallback::Notify(nsITimer *timer) {
  r_log(LOG_GENERIC, LOG_DEBUG, "Timer callback fired");
  cb_(0, 0, cb_arg_);
  
  timer->Release();
  return 0;
}

int NR_async_timer_set(int timeout, NR_async_cb cb, void *arg, char *func,
                       int l, void **handle) {
  nsresult rv;
 
  // TODO(ekr@rtfm.com): See if this is too expensive and if so cache
  nsCOMPtr<nsITimer> timer = do_CreateInstance(NS_TIMER_CONTRACTID, &rv);

  if (!NS_SUCCEEDED(rv)) {
    return(R_FAILED);
  }

  timer->InitWithCallback(new nrappkitTimerCallback(cb, arg), timeout,
                           nsITimer::TYPE_ONE_SHOT);
  
  timer->AddRef();

  if (handle)
    *handle = timer;
  
  return 0;
}

int NR_async_schedule(NR_async_cb cb, void *arg, char *func, int l) {
  return NR_async_timer_set(0, cb, arg, func, l, NULL);
}

int NR_async_timer_cancel(void *handle) {
  nsITimer *timer = static_cast<nsITimer *>(handle);
  
  timer->Cancel();

  return 0;
}
