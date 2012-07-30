const Cc = Components.classes;
const Ci = Components.interfaces;

Components.utils.import("resource://gre/modules/Services.jsm");
Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

Cc["@mozilla.org/psm;1"].getService(Ci.nsISupports);

let gScriptDone = false;

let pc1 = Cc["@mozilla.org/peerconnection;1"]
         .createInstance(Ci.IPeerConnection);
let pc2 = Cc["@mozilla.org/peerconnection;1"]
         .createInstance(Ci.IPeerConnection);

let dc1;
let dc2;

let pc1_offer;
let pc2_answer;

let observer1 = {
  QueryInterface: XPCOMUtils.generateQI([Ci.IPeerConnectionObserver]),
  onCreateOfferError: function(code) {
    print("pc1 onCreateOfferError " + code);
  },
  onSetLocalDescriptionError: function(code) {
    print("pc1 onSetLocalDescriptionError " + code);
  },
  onSetRemoteDescriptionError: function(code) {
    print("pc1 onSetRemoteDescriptionError " + code);
  },
  onStateChange: function(state) {
    print("pc1 onStateChange " + state);
  },
  onAddStream: function(stream) {
    print("pc1 onAddStream " + stream);
  },
  onDataChannel: function(channel) {
    print("pc1 onDataChannel " + channel);
    channel1 = channel;
    channel.onopen = function() {
        print("pc1 onopen fired");
	channel1.send("Hello...");
    };
    channel.onmessage = function(evt) {
        print('pc1 RESPONSE: ' + evt.data  + "state = " + channel.readyState);
    };
  },
  onConnection: function() {
    print("pc1 onConnection ");
    dc1 = pc1.createDataChannel();
  },
  onClosedConnection: function() {
    print("pc1 onClosedConnection ");
  },
};

let observer2 = {
  QueryInterface: XPCOMUtils.generateQI([Ci.IPeerConnectionObserver]),
  onCreateAnswerError: function(code) {
    print("pc2 onCreateAnswerError " + code);
  },
  onSetLocalDescriptionError: function(code) {
    print("pc2 onSetLocalDescriptionError " + code);
  },
  onSetRemoteDescriptionError: function(code) {
    print("pc2 onSetRemoteDescriptionError " + code);
  },
  onStateChange: function(state) {
    print("pc2 onStateChange " + state);
  },
  onAddStream: function(stream) {
    print("pc2 onAddStream " + stream);
  },
  onDataChannel: function(channel) {
    print("pc2 onDataChannel " + channel);
    channel2 = channel;
    channel.onopen = function() {
        print("pc2 onopen fired");
	channel2.send("Hi there!");
    };
    channel.onmessage = function(evt) {
        print('pc2 RESPONSE: ' + evt.data  + "state = " + channel.readyState);
    };
  },
  onConnection: function() {
    print("pc2 onConnection ");
    dc2 = pc2.createDataChannel();
  },
  onClosedConnection: function() {
    print("pc2 onClosedConnection ");
  },
};

// pc1.createOffer -> pc1.setLocal
observer1.onCreateOfferSuccess = function(offer) {
  print("pc1 got offer: \n" + offer);
  pc1.setLocalDescription(Ci.IPeerConnection.kActionOffer, offer);
  pc1_offer = offer;
};

// pc1.setLocal -> pc2.setRemote
observer1.onSetLocalDescriptionSuccess = function(code) {
  print("pc1 onSetLocalDescriptionSuccess: " + code);
  pc2.setRemoteDescription(Ci.IPeerConnection.kActionOffer, pc1_offer);
};

// pc2.setRemote -> pc2.createAnswer
observer2.onSetRemoteDescriptionSuccess = function(code) {
  print("pc2 onSetRemoteDescriptionSuccess: " + code);
  pc2.createAnswer("", pc1_offer);
};

// pc2.createAnswer -> pc2.setLocal
observer2.onCreateAnswerSuccess = function(answer) {
  print("pc2 got answer: \n" + answer);
  pc2.setLocalDescription(Ci.IPeerConnection.kActionAnswer, answer);
  pc2_answer = answer;
};

// pc2.setLocal -> pc1.setRemote
observer2.onSetLocalDescriptionSuccess = function(code) {
  print("pc2 onSetLocalDescriptionSuccess: " + code);
  pc1.setRemoteDescription(Ci.IPeerConnection.kActionAnswer, pc2_answer);
};

// pc1.setRemote -> finish!
observer1.onSetRemoteDescriptionSuccess = function(code) {
  print("pc1 onSetRemoteDescriptionSuccess: " + code);
  // run traffic for 5 seconds, then terminate
  let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(function() {
    gScriptDone = true;
  }, 5000, Ci.nsITimer.TYPE_ONE_SHOT);
};

let mainThread = Services.tm.currentThread;
pc1.initialize(observer1, mainThread);
pc2.initialize(observer2, mainThread);

pc1.listen(6747);
pc2.connect("192.168.1.17",6747);

let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
timer.initWithCallback(function() {
  gScriptDone = true;
}, 10000, Ci.nsITimer.TYPE_ONE_SHOT);

while (!gScriptDone)
  mainThread.processNextEvent(true);
while (mainThread.hasPendingEvents())
  mainThread.processNextEvent(true);
