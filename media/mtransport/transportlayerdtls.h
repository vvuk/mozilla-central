/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef transportlayerdtls_h__
#define transportlayerdtls_h__

#include <talk/base/sigslot.h>

#include "nspr.h"
#include "prio.h"

#include "transportflow.h"
#include "transportlayer.h"

class TransportLayerDtls : public TransportLayer {
public:
  TransportLayerDtls() {}
  virtual ~TransportLayerDtls() {}
  

  // A static version of the layer ID
  static std::string ID;

private:
  PRFileDesc *pr_fd_;
  PRFileDesc *ssl_fd_;
};


#endif
