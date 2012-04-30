/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef transportlayerprsock_h__
#define transportlayerprsock_h__

#include "nspr.h"
#include "prio.h"

#include "nsASocketHandler.h"
#include "nsCOMPtr.h"
#include "nsISocketTransportService.h"o
#include "nsXPCOM.h"

#include "transportflow.h"
#include "transportlayer.h"

class TransportLayerPrsock : public TransportLayer, public nsASocketHandler {
 public:
  // TODO: ekr@rtfm.com, this currently must be called on the socket thread.
  // Should we require that or provide a way to pump requests across
  // threads?
  nsresult Import(PRFileDesc *fd);

  // Overrides for TransportLayer
  virtual int SendPacket(const unsigned char *data, size_t len);
  
  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

  // nsISupports methods
  NS_DECL_ISUPPORTS

  // nsASocketHandler methods
  void OnSocketDetached(PRFileDesc *fd) {}  

 private:
  void RegisterHandler();

  PRFileDesc *fd_;
  nsCOMPtr<nsISocketTransportService> stservice_;
};

#endif
