/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef transportlayerloopback_h__
#define transportlayerloopback_h__

#include "nspr.h"
#include "prio.h"

#include "m_cpp_utils.h"
#include "transportflow.h"
#include "transportlayer.h"

class TransportLayerLoopback : public TransportLayer {
 public:
  TransportLayerLoopback() {}

  // Connect to the other side
  void Connect(TransportLayerLoopback* peer);

  // Overrides for TransportLayer
  virtual TransportResult SendPacket(const unsigned char *data, size_t len);

  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

 private:
  DISALLOW_COPY_ASSIGN(TransportLayerLoopback);

  TransportLayerLoopback* peer_;
};

#endif
