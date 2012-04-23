#include "logging.h"
#include "transportflow.h"
#include "transportlayerlog.h"

MLOG_INIT("mtransport");

std::string TransportLayerLogging::ID("transportlayer_logging");

int TransportLayerLogging::SendPacket(const unsigned char *data, size_t len) {
  MLOG(PR_LOG_DEBUG, "SendPacket(" << len << ")");

  if (downward_) {
    return downward_->SendPacket(data, len);
  }
}

void TransportLayerLogging::StateChange(TransportFlow *flow, State state) {
  MLOG(PR_LOG_DEBUG, "Received StateChange to " << state);

  SetState(state);
}

void TransportLayerLogging::PacketReceived(TransportFlow* flow, const unsigned char *data,
                    size_t len) {
  MLOG(PR_LOG_DEBUG, "PacketReceived(" << len << ")");
  
  SignalPacketReceived(this, data, len);
}



