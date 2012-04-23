/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef transportflow_h__
#define transportflow_h__

#include <deque>
#include <string>

class TransportLayer;

// A stack of transport layers acts as a flow.
// Generally, one reads and writes to the top layer.
class TransportFlow {
 public:
  TransportFlow() : id_("(anonymous)") {}
  ~TransportFlow();
  
  const std::string& id() const { return id_; }
  // Layer management
  void PushLayer(TransportLayer *layer);
  TransportLayer *top() const;
  TransportLayer *GetLayer(const std::string& id) const;
  
 private:
  std::string id_;
  std::deque<TransportLayer *> layers_;
};

#endif
