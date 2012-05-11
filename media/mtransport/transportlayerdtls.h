/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef transportlayerdtls_h__
#define transportlayerdtls_h__

#include <queue>

#include <talk/base/sigslot.h>

#include "nspr.h"
#include "prio.h"

#include "transportflow.h"
#include "transportlayer.h"

class Packet;

class NSPRHelper {
 public:
  NSPRHelper(TransportLayer *output) :
      output_(output),
      input_() {}
  
  void PacketReceived(const void *data, PRInt32 len);
  PRInt32 Read(void *data, PRInt32 len);
  PRInt32 Write(const void *buf, PRInt32 length);

 private:
  TransportLayer *output_;
  std::queue<Packet *> input_;
};


class TransportLayerDtls : public TransportLayer {
public:
 TransportLayerDtls() :
     TransportLayer(DGRAM),
     pr_fd_(NULL),
     ssl_fd_(NULL),
     helper_(NULL) {}
  virtual ~TransportLayerDtls() {}
  
  // Transport layer overrides.
  virtual void WasInserted();

  // A static version of the layer ID
  static std::string ID;
  
private:
  PRFileDesc *pr_fd_;
  PRFileDesc *ssl_fd_;
  NSPRHelper *helper_;

  static PRDescIdentity nspr_layer_identity;  // The NSPR layer identity
};


#endif
