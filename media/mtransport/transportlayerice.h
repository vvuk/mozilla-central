/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

// This is a wrapper around the nICEr ICE stack
#ifndef transportlayerice_h__
#define transportlayerice_h__

#include <vector>

#include <talk/base/sigslot.h>

#include "mozilla/RefPtr.h"
#include "mozilla/Scoped.h"
#include "nsCOMPtr.h"
#include "nsIEventTarget.h"
#include "nsITimer.h"

#include "m_cpp_utils.h"

#include "nricemediastream.h"

class TransportLayerIce;

// An ICE transport layer -- corresponds to a single ICE
class TransportLayerIce {
 public:
//  TransportLayerIce(const std::string& name)
//                    NrIceCtx *ctx);
  virtual ~TransportLayerIce();

  // Allow this to be refcountable
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TransportLayerIce);

  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

 private:
  DISALLOW_COPY_ASSIGN(TransportLayerIce);
};

#endif
