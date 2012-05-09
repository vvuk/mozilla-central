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
  TransportLayerPrsock() : fd_(NULL), owned_(false) {
        mPollFlags = PR_POLL_READ;
  }

  ~TransportLayerPrsock() { if (owned_) PR_Close(fd_); }

  // TODO: ekr@rtfm.com, this currently must be called on the socket thread.
  // Should we require that or provide a way to pump requests across
  // threads?
  void Import(PRFileDesc *fd, bool owned_, nsresult *result);

  // Implement TransportLayer
  virtual int SendPacket(const unsigned char *data, size_t len);
  
  // Implement nsASocket
  void OnSocketReady(PRFileDesc *fd, PRInt16 outflags);
  
  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

  // nsISupports methods
  NS_DECL_ISUPPORTS

 private:
  void OnSocketDetached(PRFileDesc *fd) { SetState(CLOSED); }  

  void RegisterHandler();

  PRFileDesc *fd_;
  bool owned_;
  nsCOMPtr<nsISocketTransportService> stservice_;
};

#endif
