#ifndef transportlayerlog_h__
#define transportlayerlog_h__

#include "transportflow.h"
#include "transportlayer.h"

class TransportLayerLogging : public TransportLayer {
  // Overrides for TransportLayer
  virtual int SendPacket(const unsigned char *data, size_t len);

  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;
};

#endif
