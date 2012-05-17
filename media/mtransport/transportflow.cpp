/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com
#include <deque>

#include "transportflow.h"
#include "transportlayer.h"

TransportFlow::~TransportFlow() {
  for (std::deque<TransportLayer *>::iterator it = layers_.begin();
       it != layers_.end(); ++it) {
    delete *it;
  }
}

nsresult TransportFlow::PushLayer(TransportLayer *layer) {
  nsresult rv = layer->Init();
  if (!NS_SUCCEEDED(rv))
    return rv;

  TransportLayer *old_layer = layers_.empty() ? NULL : layers_.front();

  layers_.push_front(layer);
  layer->Inserted(this, old_layer);

  return NS_OK;
}

TransportLayer *TransportFlow::top() const {
  return layers_.front();
}

TransportLayer *TransportFlow::GetLayer(const std::string& id) const {
  for (std::deque<TransportLayer *>::const_iterator it = layers_.begin();
       it != layers_.end(); ++it) {
    if ((*it)->id() == id)
      return *it;
  }
    
  return NULL;
}



