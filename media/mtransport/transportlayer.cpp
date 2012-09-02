/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com
#include <prlog.h>

#include "logging.h"
#include "transportflow.h"
#include "transportlayer.h"

// Logging context
MLOG_INIT("mtransport");

nsresult TransportLayer::Init() {
  if (state_ != NONE)
    return state_ == ERROR ? false : true;

  nsresult rv = InitInternal();

  if (!NS_SUCCEEDED(rv)) {
    state_ = ERROR;
    return rv;
  }
  state_ = INIT;

  return NS_OK;
}

void TransportLayer::Inserted(TransportFlow *flow, TransportLayer *downward) {
  flow_ = flow;
  downward_ = downward;

  MLOG(PR_LOG_DEBUG, LAYER_INFO << "Inserted: downward='" << 
    (downward ? downward->id(): "none") << "'");
  
  WasInserted();
}

void TransportLayer::SetState(State state) {
  if (state != state_) {
    MLOG(PR_LOG_DEBUG, LAYER_INFO << "state " << state_ << "->" << state);
    state_ = state;
    SignalStateChange(this, state);
  }
}

const std::string& TransportLayer::flow_id() { 
    static std::string empty;

    return flow_ ? flow_->id() : empty;
  }
