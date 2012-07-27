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
    onCreateOfferError: function(code) {},
    onSetLocalDescriptionError: function(code) {},
    onSetRemoteDescriptionError: function(code) {},
    onStateChange: function(state) {},
    onAddStream: function(stream) {}
  };

  let observer2 = {
    QueryInterface: XPCOMUtils.generateQI([Ci.IPeerConnectionObserver]),
    onCreateAnswerError: function(code) {},
    onSetLocalDescriptionError: function(code) {},
    onSetRemoteDescriptionError: function(code) {},
    onStateChange: function(state) {},
    onAddStream: function(stream) {}
  };

  // pc1.createOffer -> pc1.setLocal
  observer1.onCreateOfferSuccess = function(offer) {
    do_print("pc1 got offer: \n" + offer);
    pc1.setLocalDescription(Ci.IPeerConnection.kActionOffer, offer);
    pc1_offer = offer;
  };

  // pc1.setLocal -> pc2.setRemote
  observer1.onSetLocalDescriptionSuccess = function(code) {
    do_print("pc1 onSetLocalDescriptionSuccess: " + code);
    pc2.setRemoteDescription(Ci.IPeerConnection.kActionOffer, pc1_offer);
  };

  // pc2.setRemote -> pc2.createAnswer
  observer2.onSetRemoteDescriptionSuccess = function(code) {
    do_print("pc2 onSetRemoteDescriptionSuccess: " + code);
    pc2.createAnswer("", pc1_offer);
  };

  // pc2.createAnswer -> pc2.setLocal
  observer2.onCreateAnswerSuccess = function(answer) {
    do_print("pc2 got answer: \n" + answer);
    pc2.setLocalDescription(Ci.IPeerConnection.kActionAnswer, answer);
    pc2_answer = answer;
  };

  // pc2.setLocal -> pc1.setRemote
  observer2.onSetLocalDescriptionSuccess = function(code) {
    do_print("pc2 onSetLocalDescriptionSuccess: " + code);
    pc1.setRemoteDescription(Ci.IPeerConnection.kActionAnswer, pc2_answer);
  };

  // pc1.setRemote -> finish!
  observer1.onSetRemoteDescriptionSuccess = function(code) {
    do_print("pc1 onSetRemoteDescriptionSuccess: " + code);
    // run traffic for 5 seconds, then terminate.
    let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    timer.initWithCallback(function() {
      run_next_test();
    }, 5000, Ci.nsITimer.TYPE_ONE_SHOT);
  };

  let mainThread = Services.tm.currentThread;
  pc1.initialize(observer1, mainThread);
  pc2.initialize(observer2, mainThread);

  let stream1 = pc1.createFakeMediaStream(Ci.IPeerConnection.kHintAudio);
  pc1.addStream(stream1);

  let stream2 = pc2.createFakeMediaStream(Ci.IPeerConnection.kHintAudio);
  pc2.addStream(stream2);

  // start the chain.
  pc1.createOffer("");
});
