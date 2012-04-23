/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com
#include "logging.h"
#include "prlog.h"
#include "transportflow.h"
#include "transportlayer.h"

MLOG_INIT("mtransport");

void TransportLayer::Inserted(TransportFlow *flow, TransportLayer *downward) {
  MLOG(PR_LOG_DEBUG, "Flow: " << flow->id() << ": Inserting layer id=" << id() << " downward=" << 
    (downward ? downward->id(): "none"));

  flow_ = flow;
  downward_ = downward;
}
