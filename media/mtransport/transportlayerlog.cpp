#include "logging.h"
#include "transportflow.h"
#include "transportlayerlog.h"

MLOG_INIT("mtransport");

std::string TransportLayerLogging::ID("mt_logging");

void TransportLayerLogging::WasInserted() {
  if (downward_) {
    downward_->SignalStateChange.connect(
        this, &TransportLayerLogging::StateChange);
    downward_->SignalPacketReceived.connect(
        this, &TransportLayerLogging::PacketReceived);
  }
}

int TransportLayerLogging::SendPacket(const unsigned char *data, size_t len) {
  MLOG(PR_LOG_DEBUG, "SendPacket(" << len << ")");

  if (downward_) {
    return downward_->SendPacket(data, len);
  }
  else {
    return len;
  }
}

void TransportLayerLogging::StateChange(TransportLayer *layer, State state) {
  MLOG(PR_LOG_DEBUG, "Received StateChange to " << state);

  SetState(state);
}

void TransportLayerLogging::PacketReceived(TransportLayer* layer,
                                           const unsigned char *data,
                                           size_t len) {
  MLOG(PR_LOG_DEBUG, "PacketReceived(" << len << ")");
  
  SignalPacketReceived(this, data, len);
}



