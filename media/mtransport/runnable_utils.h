/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef runnable_utils_h__
#define runnable_utils_h__

// Abstract base class for all of our templates
class runnable_args_base : public nsRunnable {
 public:
  
  NS_IMETHOD Run() = 0;
};



#include "runnable_utils_generated.h"

#endif
