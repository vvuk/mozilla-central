// Pull in sigslot from libjingle. If we remove libjingle, we will
// need to either bring that in separately or refactor this code
// to not use sigslot
#include <talk/base/sigslot.h>


// Abstract base class for network transport layers.
class TransportFlow;

class TransportLayer {
 public:
  // The state of the transport flow
  enum State { CONNECTING, OPEN, CLOSED, ERROR };

  // Is this a stream or datagram flow
  enum Mode { STREAM, DGRAM };
  
  // Must be implemented by derived classes
  virtual int SendPacket(const unsigned char *data, size_t len) = 0;

  // Event definitions that one can register for
  // State has changed
  sigslot::signal2<TransportFlow*, State> SignalState;
  // Data received on the flow
  sigslot::signal3<TransportFlow*, const unsigned char *, size_t>
                         PacketReceived;
  
  // Return the layer id for this layer
  static const std::string &LayerId() = 0;

 private:
  TransportFlow *flow_;  // The flow this is part of
  TransportLayer *downward_; // The next layer in the stack
};


// A stack of transport layers acts as a flow.
// Generally, one reads and writes to the top layer.
class TransportFlow {
 public:
  TransportFlow() {}
  ~TransportFlow();

  // Layer management
  void PushLayer(TransportLayer *layer);
  TransportLayer *top() const;
  TransportLayer *GetLayer(const std::string& id) const;
  
 private:
  std::vector<TransportLayer *> layers_;
};

