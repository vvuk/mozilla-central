Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

function run_test() {
  // Load PSM (initializes NSS)
  Cc["@mozilla.org/psm;1"].getService(Ci.nsISupports);
  run_next_test();
}

add_test(function createComponent() {
  try {
    let component = Cc["@mozilla.org/peerconnection;1"]
                    .createInstance(Ci.IPeerConnection);
  } catch(e) {
    // Failed to create the component.
    do_check_true(false);
  }
  run_next_test();
});

add_test(function createOffer() {
  let pc = Cc["@mozilla.org/peerconnection;1"]
           .createInstance(Ci.IPeerConnection);
  do_check_eq(typeof pc.createOffer, "function");

  let observer = {
    QueryInterface: XPCOMUtils.generateQI([Ci.IPeerConnectionObserver]),
    onCreateOfferSuccess: function(offer) {
      do_check_neq(offer, false);
      do_print("!!!!!!!!!!!!!! got offer: \n" + offer);
      pc.setLocalDescription(0, offer);
    },
    onCreateOfferError: function(code) {
      do_check_true(false);
      do_print("!!!!!!!!!!!!!! got error " + error);
    },
    onSetLocalDescriptionSuccess: function(code) {
      do_print("!!!!!!!!!!!!! onSetLocalDescriptionSuccess: " + code);
      run_next_test();
    },
    onSetLocalDescriptionError: function(code) {
      do_check_true(false);
      do_print("!!!!!!!!!!!!! got error " + code);
    },
    onStateChange: function(state) {
      do_print("!!!!!!!!!!!! state changed to " + state);
    }
  };

  pc.initialize(observer, Services.tm.currentThread);
  let stream = pc.createMediaStream(Ci.IPeerConnection.kHintAudio);
  pc.addStream(stream);
  pc.createOffer("");
});
