Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

function run_test() {
  // Load PSM (initializes NSS)
  Cc["@mozilla.org/psm;1"].getService(Ci.nsISupports);
  run_next_test();
}

add_test(function createOfferAnswer() {
  let pc1 = Cc["@mozilla.org/peerconnection;1"]
           .createInstance(Ci.IPeerConnection);
  let pc2 = Cc["@mozilla.org/peerconnection;1"]
           .createInstance(Ci.IPeerConnection);

  let pc1_offer;
  let pc2_answer;

  let observer1 = {
    QueryInterface: XPCOMUtils.generateQI([Ci.IPeerConnectionObserver]),
    onCreateOfferSuccess: function(offer) {
      do_check_neq(offer, false);
      do_print("!!!!!!!!!!!!!! pc1 got offer: \n" + offer);
      pc1.setLocalDescription(Ci.IPeerConnection.kActionOffer, offer);
      pc1_offer = offer;
    },
    onCreateOfferError: function(code) {
      do_check_true(false);
      do_print("!!!!!!!!!!!!!! pc1 got error " + code);
    },
    onSetLocalDescriptionSuccess: function(code) {
      do_print("!!!!!!!!!!!!! pc1 onSetLocalDescriptionSuccess: " + code);
      pc2.setRemoteDescription(Ci.IPeerConnection.kActionOffer, pc1_offer);
    },
    onSetRemoteDescriptionSuccess: function(code) {
      do_print("!!!!!!!!!!!!! pc1 onSetRemoteDescriptionSuccess: " + code);
      run_next_test();
    },
    onSetLocalDescriptionError: function(code) {},
    onSetRemoteDescriptionError: function(code) {},
    onStateChange: function(state) {
      do_printf("!!!!!!!!!!!! state changed to " + state);
    }
  };

  let observer2 = {
    QueryInterface: XPCOMUtils.generateQI([Ci.IPeerConnectionObserver]),
    onCreateOfferSuccess: function(answer) {
      do_print("!!!!!!!!!!!!!! pc2 got answer: \n" + answer);
      pc2.setLocalDescription(Ci.IPeerConnection.kActionAnswer, answer);
      pc2_answer = answer;
    },
    onCreateOfferError: function(code) {
      do_check_true(false);
      do_print("!!!!!!!!!!!!!! pc2 got error " + code);
    },
    onSetLocalDescriptionSuccess: function(code) {
      do_print("!!!!!!!!!!!!!! pc2 onSetLocalDescriptionSuccess: " + code);
      pc1.setRemoteDescription(Ci.IPeerConnection.kActionAnswer, pc2_answer);
    },
    onSetRemoteDescriptionSuccess: function(code) {
      do_print("!!!!!!!!!!!!!! pc2 onSetRemoteDescriptionSuccess: " + code);
      pc2.createAnswer("", pc1_offer);
    },
    onSetLocalDescriptionError: function(code) {},
    onSetRemoteDescriptionError: function(code) {},
    onStateChange: function(state) {
      do_print("!!!!!!!!!!!! pc2 state changed to " + state);
    }
  };


  pc1.initialize(observer1, Services.tm.currentThread);
  pc2.initialize(observer2, Services.tm.currentThread);

  let stream1 = pc1.createMediaStream(Ci.IPeerConnection.kHintAudio);
  pc1.addStream(stream1);

  let stream2 = pc2.createMediaStream(Ci.IPeerConnection.kHintAudio);
  pc2.addStream(stream2);

  pc1.createOffer("");
});
