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
#include "nsISocketTransportService.h"
#include "nsXPCOM.h"

#include "m_cpp_utils.h"
#include "transportflow.h"
#include "transportlayer.h"

class TransportLayerPrsock : public TransportLayer {
 public:
  TransportLayerPrsock() : fd_(NULL), handler_() {}
 
  virtual ~TransportLayerPrsock() {
    Detach();
  }

  
  // Internal initializer
  virtual nsresult InitInternal();

  void Import(PRFileDesc *fd, nsresult *result);

  void Detach() {
    handler_->Detach();
  }

  // Implement TransportLayer
  virtual TransportResult SendPacket(const unsigned char *data, size_t len);
  
  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

 private:
  DISALLOW_COPY_ASSIGN(TransportLayerPrsock);

  // Inner class
  class SocketHandler : public nsASocketHandler {
   public:
      SocketHandler(TransportLayerPrsock *prsock, PRFileDesc *fd) :
        prsock_(prsock), fd_(fd) {
        mPollFlags = PR_POLL_READ;
      }
      virtual ~SocketHandler() {}
      
      void Detach() {
        prsock_ = NULL;
      }
      
      // Implement nsASocket
      virtual void OnSocketReady(PRFileDesc *fd, PRInt16 outflags) {
        if (prsock_) {
          prsock_->OnSocketReady(fd, outflags);
        }
      }

      virtual void OnSocketDetached(PRFileDesc *fd) {
        if (prsock_) {
          prsock_->OnSocketDetached(fd);
        }
        PR_Close(fd_);
      }
      
      // nsISupports methods
      NS_DECL_ISUPPORTS
      
      private:
      TransportLayerPrsock *prsock_;
      PRFileDesc *fd_;
   private:
    DISALLOW_COPY_ASSIGN(SocketHandler);
  };

  // Allow SocketHandler to talk to our APIs
  friend class SocketHandler;

  // Functions to be called by SocketHandler
  void OnSocketReady(PRFileDesc *fd, PRInt16 outflags);
  void OnSocketDetached(PRFileDesc *fd) {
    SetState(CLOSED);
  }
  
  PRFileDesc *fd_;
  nsCOMPtr<SocketHandler> handler_;
  nsCOMPtr<nsISocketTransportService> stservice_;

};



#endif
