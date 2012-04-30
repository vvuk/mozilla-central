/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef transportlayerprsock_h__
#define transportlayerprsock_h__

#include "nspr.h"
#include "prio.h"

#include "nsCOMPtr.h"
#include "nsNetCID.h"
#include "nsXPCOMGlue.h"
#include "nsXPCOM.h"

#include "transportflow.h"
#include "transportlayer.h"

class TransportLayerPrsock : public TransportLayer, public nsASocketHandler {
 public:
  void Import(PRFileDesc *fd);

  // Overrides for TransportLayer
  virtual int SendPacket(const unsigned char *data, size_t len);
  
  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

  // nsASocketHandler methods
  NS_DECL_ISUPPORTS

 private:
  void RegisterHandler();

  PRFileDesc *fd_;
};

#endif
