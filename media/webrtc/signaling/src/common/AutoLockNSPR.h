/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef autolocknspr_h___h__
#define autolocknspr_h___h__

#include "nspr.h"
#include "prlock.h"


class LockNSPR {
 public:
  LockNSPR() : lock_(NULL), acquired_(false) {
    lock_ = PR_NewLock();
    PR_ASSERT(lock_);
  }
  ~LockNSPR() {
    if (acquired_)
      Release();
    PR_DestroyLock(lock_);
  }
  
  void Acquire() {
    PR_ASSERT(!acquired_);
    PR_Lock(lock_);
    acquired_ = true;
  }
  
  void Release() {
    PR_ASSERT(acquired_);
    acquired_ = false;
    PR_Unlock(lock_);
  }
  
 private:
  PRLock *lock_;
  bool acquired_;
};

class AutoLockNSPR {
 public:
  AutoLockNSPR(LockNSPR& lock) : lock_(lock) {
    lock_.Acquire();
  }
  ~AutoLockNSPR() { 
    lock_.Release();
  }

 private:
  LockNSPR& lock_;
};

#endif
