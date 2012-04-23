/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "logging.h"
#include "transportflow.h"
#include "transportlayerloopback.h"

MLOG_INIT("mtransport");

std::string TransportLayerLoopback::ID("mt_loopback");

// Connect to the other side
void TransportLayerLoopback::Connect(TransportLayerLoopback* peer) {
  peer_ = peer;

  SetState(OPEN);
}

int TransportLayerLoopback::SendPacket(const unsigned char *data, size_t len) {
  MLOG(PR_LOG_DEBUG, LAYER_INFO << "SendPacket(" << len << ")");

  peer_->SignalPacketReceived(peer_, data, len);
  
  return len;
}
