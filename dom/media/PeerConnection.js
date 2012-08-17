/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const IDService = {};
const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/identity/WebRTC.jsm", IDService);

const PC_CONTRACT = "@mozilla.org/dom/peerconnection;1";
const PC_CID = Components.ID("{7cb2b368-b1ce-4560-acac-8e0dbda7d3d0}");

function PeerConnection() {
  dump("!!! Real PeerConnection constructor called OMG !!!\n\n");

  this._queue = [];

  this._pc = null;
  this._observer = null;
  this._identity = null;

  // TODO: Refactor this.
  this._onCreateOfferSuccess = null;
  this._onCreateOfferFailure = null;
  this._onCreateAnswerSuccess = null;
  this._onCreateAnswerFailure = null;
  this._onSelectIdentitySuccess = null;
  this._onSelectIdentityFailure = null;
  this._onVerifyIdentitySuccess = null;
  this._onVerifyIdentityFailure = null;

  // Everytime we get a request from content, we put it in the queue. If
  // there are no pending operations though, we will execute it immediately.
  // In PeerConnectionObserver, whenever we are notified that an operation
  // has finished, we will check the queue for the next operation and execute
  // if neccesary. The _pending flag indicates whether an operation is currently
  // in progress.
  this._pending = false;
}
PeerConnection.prototype = {
  classID: PC_CID,

  classInfo: XPCOMUtils.generateCI({classID: PC_CID,
                                    contractID: PC_CONTRACT,
                                    classDescription: "PeerConnection",
                                    interfaces: [
                                      Ci.nsIDOMRTCPeerConnection,
                                      Ci.nsIDOMGlobalObjectConstructor
                                    ],
                                    flags: Ci.nsIClassInfo.DOM_OBJECT}),

  QueryInterface: XPCOMUtils.generateQI([
    Ci.nsIDOMRTCPeerConnection, Ci.nsIDOMGlobalObjectConstructor
  ]),

  // Constructor is an explicit function, because of nsIDOMGlobalObjectConstructor
  constructor: function(win) {
    this._pc = Cc["@mozilla.org/peerconnection;1"].
             createInstance(Ci.IPeerConnection);
    this._observer = new PeerConnectionObserver(this);

    this._uniqId = Cc["@mozilla.org/uuid-generator;1"]
                   .getService(Ci.nsIUUIDGenerator)
                   .generateUUID().toString();

    // Nothing starts until ICE gathering completes.
    this._queueOrRun({
      func: this._pc.initialize,
      args: [this._observer, win, Services.tm.currentThread]
    });
    this._printQueue();

    this._win = win;
    this._winID = this._win.QueryInterface(Ci.nsIInterfaceRequestor)
                           .getInterface(Ci.nsIDOMWindowUtils).outerWindowID;

    dump("!!! mozPeerConnection constructor called " + this._win + "\n");
  },

  // FIXME: Right now we do not enforce proper invocation (eg: calling
  // createOffer twice in a row is allowed).

  _printQueue: function() {
    let q = "!!! Queue for " + this._uniqId + " is currently: [";
    for (let i = 0; i < this._queue.length; i++) {
      q = q + this._queue[i].func.name + ",";
    }
    q += "]\n";
    dump(q);
  },

  _queueOrRun: function(obj) {
    if (!this._pending) {
      dump("!!! " + this._uniqId + " : calling " + obj.func.name + "\n");
      obj.func.apply(this, obj.args);
      this._pending = true;
    } else {
      dump("!!! " + this._uniqId + " : queued " + obj.func.name + "\n");
      this._queue.push(obj);
    }
    this._printQueue();
  },

  // Pick the next item from the queue and run it
  _executeNext: function() {
    dump("!!! in executeNext: ");
    this._printQueue();
    if (this._queue.length) {
      let obj = this._queue.shift();
      obj.func.apply(this, obj.args);
    } else {
      this._pending = false;
    }
  },

  _selectIdentity: function() {
    let self = this;
    IDService.selectIdentity(this._uniqId, this._winID, function(err, val) {
      if (err) {
        self._onSelectIdentityFailure.onCallback(err);
      } else {
        self._identity = val;
        self._onSelectIdentitySuccess.onCallback(null);
      }
      self._executeNext();
    });
  },

  _displayVerification: function(email) {
    let browser = this._win.QueryInterface(Ci.nsIInterfaceRequestor)
                         .getInterface(Ci.nsIWebNavigation)
                         .QueryInterface(Ci.nsIDocShell).chromeEventHandler;
    let chromeWin = browser.ownerDocument.defaultView;
    
    dump("going to show identity popup\n");

    chromeWin.PopupNotifications.show(
      browser, "webrtc-id-verified",
      "This call has been verified as originating from " + email,
      "password-notification-icon", // temporary
      {
        label: "Ok", accessKey: "o", callback: function() {}
      },
      [], {dismissed:true}
    );
  },

  _verifyIdentity: function(offer) {
    let self = this;

    // Extract the a=fingerprint and a=identity lines.
    let ire = new RegExp("a=identity:(.+)\r\n");
    let fre = new RegExp("a=fingerprint:(.+)\r\n");

    let id = offer.sdp.match(ire);
    let fprint = offer.sdp.match(fre);
     
    dump("!!! "+ this._uniqId + " : fprint = " + fprint[1] + "\n");

    if (id.length == 2 && fprint.length == 2) {
      IDService.verifyIdentity(id[1], function(err, val) {
        if (!val) {
          dump("!!! : no verified value\n");
          dump("!!! : err =" + err + "\n");
        } else {
          dump("!!! : verified value = " + val.message + "\n");
        }

        if (val && (fprint[1] == val.message)) {
          self._onVerifyIdentitySuccess.onCallback(val);
          self._displayVerification(val.principal.email);
          return;
        }
        self._onVerifyIdentityFailure.onCallback(err || "Signed message did not match");
      });
    } else {
      self._onVerifyIdentityFailure.onCallback("No identity information found");
    }
  },

  selectIdentity: function(onSuccess, onError) {
    dump("!!! " + this._uniqId + " : selectIdentity called\n");
    this._onSelectIdentitySuccess = onSuccess;
    this._onSelectIdentityFailure = onError;

    this._queueOrRun({func: this._selectIdentity, args: null});
    dump("!!! "+ this._uniqId + " :  selectIdentity returned\n");
  },

  verifyIdentity: function(offer, onSuccess, onError) {
    dump("!!! " + this._uniqId + " : verifyIdentity called\n");
    this._onVerifyIdentitySuccess = onSuccess;
    this._onVerifyIdentityFailure = onError;

    if (!offer.type || !offer.sdp) {
      if (onError) {
        onError.onCallback("Invalid offer/answer provided to verifyIdentity");
      }
      return;
    }

    this._queueOrRun({func: this._verifyIdentity, args: [offer]});
    dump("!!! " + this._uniqId + " : verifyIdentity returned\n");
  },

  createOffer: function(onSuccess, onError, constraints) {
    dump("!!! " + this._uniqId + " : createOffer called\n");
    this._onCreateOfferSuccess = onSuccess;
    this._onCreateOfferFailure = onError;

    // TODO: Implement constraints/hints.
    if (!constraints) {
      constraints = "";
    }

    this._queueOrRun({func: this._pc.createOffer, args: [constraints]});
    dump("!!! " + this._uniqId + " : createOffer returned\n");
  },

  createAnswer: function(offer, onSuccess, onError, constraints, provisional) {
    dump("!!! " + this._uniqId + " : createAnswer called\n");
    this._onCreateAnswerSuccess = onSuccess;
    this._onCreateAnswerFailure = onError;

    if (offer.type != "offer") {
      if (onError) {
        onError.onCallback("Invalid type " + offer.type + " passed");
      }
      return;
    }

    if (!offer.sdp) {
      if (onError) {
        onError.onCallback("SDP not provided to createAnswer");
      }
      return;
    }

    if (!constraints) {
      constraints = "";
    }
    if (!provisional) {
      provisional = false;
    }

    // TODO: Implement provisional answer & constraints.
    this._queueOrRun({func: this._pc.createAnswer, args: ["", offer.sdp]});
    dump("!!! " + this._uniqId + " : createAnswer returned\n");
  },

  setLocalDescription: function(desc, onSuccess, onError) {
    this._onSetLocalDescriptionSuccess = onSuccess;
    this._onSetLocalDescriptionFailure = onError;

    let type;
    switch (desc.type) {
      case "offer":
        type = Ci.IPeerConnection.kActionOffer;
        break;
      case "answer":
        type = Ci.IPeerConnection.kActionAnswer;
        break;
      default:
        if (onError) {
          onError.onCallback("Invalid type " + desc.type + " provided to setLocalDescription");
          return;
        }
        break;
    }

    dump("!!! " + this._uniqId + " : setLocalDescription called\n");
    this._queueOrRun({func: this._pc.setLocalDescription, args: [type, desc.sdp]});
    dump("!!! " + this._uniqId + " : setLocalDescription returned\n");
  },

  setRemoteDescription: function(desc, onSuccess, onError) {
    this._onSetRemoteDescriptionSuccess = onSuccess;
    this._onSetRemoteDescriptionFailure = onError;

    let type;
    switch (desc.type) {
      case "offer":
        type = Ci.IPeerConnection.kActionOffer;
        break;
      case "answer":
        type = Ci.IPeerConnection.kActionAnswer;
        break;
      default:
        if (onError) {
          onError.onCallback("Invalid type " + desc.type + " provided to setLocalDescription");
          return;
        }
        break;
    }

    dump("!!! " + this._uniqId + " : setRemoteDescription called\n");
    this._queueOrRun({func: this._pc.setRemoteDescription, args: [type, desc.sdp]});
    dump("!!! " + this._uniqId + " : setRemoteDescription returned\n");
  },

  updateIce: function(config, constraints, restart) {
    return Cr.NS_ERROR_NOT_IMPLEMENTED;
  },

  addIceCandidate: function(candidate) {
    return Cr.NS_ERROR_NOT_IMPLEMENTED;
  },

  addStream: function(stream, constraints) {
    dump("!!! " + this._uniqId + " : addStream " + stream + " called\n");
    // TODO: Implement constraints.
    this._pc.addStream(stream);
    dump("!!! " + this._uniqId + " : addStream returned\n");
  },

  removeStream: function(stream) {
    dump("!!! " + this._uniqId + " : removeStream called\n");
    this._pc.removeStream(stream);
    dump("!!! " + this._uniqId + " : removeStream returned\n");
  },

  createDataChannel: function() {
    dump("!!! " + this._uniqId + " : createDataChannel called\n");
    let channel = this._pc.createDataChannel(/*args*/);
    dump("!!! " + this._uniqId + " : createDataChannel returned\n");
    return channel;
  },

  connectDataConnection: function(localport, remoteport) {
    dump("!!! " + this._uniqId + " : ConnectDataConnection() called\n");
    this._pc.connectDataConnection(localport, remoteport);
    dump("!!! " + this._uniqId + " : ConnectDataConnection() returned\n");
  },

  // FIX - remove connect() and listen()
  listen: function(port) {
    dump("!!! " + this._uniqId + " : listen() called\n");
    this._pc.listen(port)
    dump("!!! " + this._uniqId + " : listen() returned\n");
  },

  connect: function(addr, localport, remoteport) {
    dump("!!! " + this._uniqId + " : connect() called\n");
    this._pc.connect(addr, localport, remoteport);
    dump("!!! " + this._uniqId + " : connect() returned\n");
  },

  close: function() {
    dump("!!! " + this._uniqId + " : close called\n");
    // Don't queue this one, since we just want to shutdown.
    this._pc.closeStreams();
    this._pc.close();
    dump("!!! " + this._uniqId + " : close returned");
  },

  onRemoteStreamAdded: null,
  notifyDataChannel: null,
  notifyConnection: null,
  notifyClosedConnection: null,

  // For testing only.
  createFakeMediaStream: function(type, muted) {
    var hint_mute = muted ? 0x80 : 0;

    if (type == "video") {
      return this._pc.createFakeMediaStream(Ci.IPeerConnection.kHintVideo |
                                            hint_mute);
    }
    return this._pc.createFakeMediaStream(Ci.IPeerConnection.kHintAudio |
                                          hint_mute);
  }
};

// This is a seperate object because we don't want to expose it to DOM.
function PeerConnectionObserver(dompc) {
  this._dompc = dompc;
}
PeerConnectionObserver.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.IPeerConnectionObserver]),

  onCreateOfferSuccess: function(offer) {
    dump("!!! " + this._dompc._uniqId + " : onCreateOfferSuccess called\n");

    // Before calling the success callback, check if selectIdentity was
    // previously called and that an identity was obtained. If so, add
    // a signed string to the SDP before sending it to content.
    if (!this._dompc._identity) {
      this._dompc._onCreateOfferSuccess.onCallback({type: "offer", sdp: offer});
      this._dompc._executeNext();
      return;
    }

    let sig = this._dompc._pc.fingerprint;
    dump("!!! " + this._dompc._uniqId + " : fingerprint = " + sig + "\n");

    // FIXME! Save the origin of the window that created the dompc.
    let self = this;
    this._dompc._identity.sign("http://example.org", sig, function(e, ast) {
      if (e && self._dompc._onCreateOfferFailure) {
        self._dompc._onCreateOfferFailure(e);
        self._dompc._executeNext();
        return;
      }

      // Got assertion, add to the SDP along with the fingerprint. We put
      // it right at the top, because these must come before the first
      // m= line.
      let sigline = "a=fingerprint:" + sig + "\r\n";
      let idline = "a=identity:" + ast + "\r\n";

      let parts = offer.split("m=");
      let finalOffer = parts[0] + idline;
      for (let i = 1; i < parts.length; i++) {
        finalOffer += "m=" + parts[i];
      }

      dump("!!! " + self._dompc._uniqId + " : Generated final offer: " + finalOffer + "\n\n");
      self._dompc._onCreateOfferSuccess.onCallback({
        type: "offer", sdp: finalOffer
      });
      self._dompc._executeNext();
    });
  },

  onCreateOfferError: function(code) {
    dump("!!! " + this._dompc._uniqId + " : onCreateOfferError called: " + code + "\n");
    if (this._dompc._onCreateOfferFailure) {
      this._dompc._onCreateOfferFailure.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onCreateAnswerSuccess: function(answer) {
    dump("!!! " + this._dompc._uniqId + " : onCreateAnswerSuccess called\n");
    if (this._dompc._onCreateAnswerSuccess) {
      this._dompc._onCreateAnswerSuccess.onCallback({
        type: "answer", sdp: answer
      });
    }
    this._dompc._executeNext();
  },

  onCreateAnswerError: function(code) {
    dump("!!! " + this._dompc._uniqId + " : onCreateAnswerError called: " + code + "\n");
    if (this._dompc._onCreateAnswerFailure) {
      this._dompc._onCreateAnswerFailure.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onSetLocalDescriptionSuccess: function(code) {
    dump("!!! " + this._dompc._uniqId + " : onSetLocalDescriptionSuccess called\n");
    if (this._dompc._onSetLocalDescriptionSuccess) {
      this._dompc._onSetLocalDescriptionSuccess.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onSetRemoteDescriptionSuccess: function(code) {
    dump("!!! " + this._dompc._uniqId + " : onSetRemoteDescriptionSuccess called\n");
    if (this._dompc._onSetRemoteDescriptionSuccess) {
      this._dompc._onSetRemoteDescriptionSuccess.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onSetLocalDescriptionError: function(code) {
    dump("!!! " + this._dompc._uniqId + " : onSetLocalDescriptionError called: " + code + "\n");
    if (this._dompc._onSetLocalDescriptionFailure) {
      this._dompc._onSetLocalDescriptionFailure.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onSetRemoteDescriptionError: function(code) {
    dump("!!! " + this._dompc._uniqId + " : onSetRemoteDescriptionError called: " + code + "\n");
    if (this._dompc._onSetRemoteDescriptionFailure) {
      this._dompc._onSetRemoteDescriptionFailure.onCallback(code);
    }
    this._dompc._executeNext();
  },

  // FIXME: Following observer events should update state on this._dompc.
  onStateChange: function(state) {
    dump("!!! " + this._dompc._uniqId + " : onStateChange called: " + state + "\n");

    if (state == Ci.IPeerConnectionObserver.kIceState) {
      switch (this._dompc._pc.iceState) {
        case Ci.IPeerConnection.kIceWaiting:
          dump("!!! ICE waiting...\n");
          this._dompc._executeNext();
	  break
        case Ci.IPeerConnection.kIceChecking:
          dump("!!! ICE checking...\n");
          this._dompc._executeNext();
	  break
        case Ci.IPeerConnection.kIceConnected:
          dump("!!! " + this._dompc._uniqId + " : ICE gathering is complete, calling _executeNext! \n");
          this._dompc._executeNext();
          break;
        default:
          dump("!!! " + this._dompc._uniqId + " : ICE invalid state, " + this._dompc._pc.iceState + "\n");
          break;
      }
    }
  },

  onAddStream: function(stream, type) {
    dump("!!! " + this._dompc._uniqId + " : onAddStream called: " + stream + " :: " + type + "\n");
    if (this._dompc.onRemoteStreamAdded) {
      this._dompc.onRemoteStreamAdded.onCallback({stream: stream, type: type});
    }
    this._dompc._executeNext();
  },

  onRemoveStream: function() {
    dump("!!! " + this._dompc._uniqId + " : onRemoveStream called\n");
    this._dompc._executeNext();
  },

  onAddTrack: function() {
    dump("!!! " + this._dompc._uniqId + " : onAddTrack called\n");
    this._dompc._executeNext();
  },

  onRemoveTrack: function() {
    dump("!!! " + this._dompc._uniqId + " : onRemoveTrack called\n");
    this._dompc._executeNext();
  },

  notifyConnection: function() {
    dump("!!! " + this._dompc._uniqId + " : onConnection called\n");
    if (this._dompc.onConnection) {
      this._dompc.onConnection.onCallback();
    }
    this._dompc._executeNext();
  },

  notifyClosedConnection: function() {
    dump("!!! " + this._dompc._uniqId + " : onClosedConnection called\n");
    if (this._dompc.onClosedConnection) {
      this._dompc.onClosedConnection.onCallback();
    }
    this._dompc._executeNext();
  },

  notifyDataChannel: function(channel) {
    dump("!!! " + this._dompc._uniqId + " : onDataChannel called: " + channel + "\n");
    if (this._dompc.onDataChannel) {
      this._dompc.onDataChannel.onCallback(channel);
    }
    this._dompc._executeNext();
  },

  foundIceCandidate: function(candidate) {
    dump("!!! " + this._dompc._uniqId + " : foundIceCandidate called: " + candidate + "\n");
    this._dompc._executeNext();
  }
};

let NSGetFactory = XPCOMUtils.generateNSGetFactory([PeerConnection]);
