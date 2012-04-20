// Pull in sigslot from libjingle. If we remove libjingle, we will
// need to either bring that in separately or refactor this code
// to not use sigslot
#include <talk/base/sigslot.h>

class TransportFlow {
 public:
  // The state of the transport flow
  enum State { CONNECTING, OPEN, CLOSED, ERROR };

  // Is this a stream or datagram flow
  enum Mode { STREAM, DGRAM };
  
  // Must be implemented by derived classes


  // Event definitions that one can register for
  // State has changed
  sigslot::signal2<TransportFlow*, State> SignalState;
  // Data received on the flow
  sigslot::signal3<TransportFlow*, unsigned char *, size_t> DataReceived;

};
