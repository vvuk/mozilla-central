/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef transportflow_h__
#define transportflow_h__

#include <deque>
#include <string>

#include "nscore.h"

#include "m_cpp_utils.h"

class TransportLayer;

// A stack of transport layers acts as a flow.
// Generally, one reads and writes to the top layer.
class TransportFlow {
 public:
  TransportFlow() : id_("(anonymous)") {}
  ~TransportFlow();
  
  const std::string& id() const { return id_; }
  // Layer management
  nsresult PushLayer(TransportLayer *layer);
  TransportLayer *top() const;
  TransportLayer *GetLayer(const std::string& id) const;
  
 private:
  DISALLOW_COPY_ASSIGN(TransportFlow);

  std::string id_;
  std::deque<TransportLayer *> layers_;
};

#endif
