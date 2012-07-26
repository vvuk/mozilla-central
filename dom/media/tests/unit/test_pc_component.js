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
      do_print("!!!!!!!!!!!!!! got offer: \n" + offer);
      pc.setLocalDescription(1, offer);
    },
    onCreateOfferError: function(code) {
      do_print("!!!!!!!!!!!!!! got error " + error);
    },
    onCreateAnswerSuccess: function(answer) {},
    onCreateAnswerError: function(code) {},
    onSetLocalDescriptionSuccess: function(code) {
      do_print("!!!!!!!!!!!!! onSetLocalDescriptionSuccess: " + code);
      run_next_test();
    },
    onSetRemoteDescriptionSuccess: function(code) {},
    onSetLocalDescriptionError: function(code) {},
    onSetRemoteDescriptionError: function(code) {},
    onStateChange: function(state) {},
    // void onAddStream(MediaTrackTable* stream) = 0; XXX: figure out this one later
    onRemoveStream: function() {},
    onAddTrack: function() {},
    onRemoveTrack: function() {},
    foundIceCandidate: function(candidate) {}
  };

  pc.initialize(observer, Services.tm.currentThread);
  let stream = pc.createMediaStream(Ci.IPeerConnection.kHintAudio);
  pc.addStream(stream);
  pc.createOffer("");
  do_print("!!!!!!!!!!!!!! createOffer dispatched");
});
