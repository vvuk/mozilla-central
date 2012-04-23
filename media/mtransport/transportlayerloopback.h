/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef transportlayerloopback_h__
#define transportlayerloopback_h__

#include "transportflow.h"
#include "transportlayer.h"

class TransportLayerLoopback : public TransportLayer {
 public:
  // Connect to the other side
  void Connect(TransportLayerLoopback* peer);

  // Overrides for TransportLayer
  virtual int SendPacket(const unsigned char *data, size_t len);

  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

 private:
  TransportLayerLoopback* peer_;
};

#endif
