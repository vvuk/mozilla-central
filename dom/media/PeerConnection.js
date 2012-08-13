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

function PeerConnection() {}
PeerConnection.prototype = {

  _pc: null,
  _observer: null,
  _identity: null,

  // TODO: Refactor this.
  _onCreateOfferSuccess: null,
  _onCreateOfferFailure: null,
  _onCreateAnswerSuccess: null,
  _onCreateAnswerFailure: null,
  _onSelectIdentitySuccess: null,
  _onSelectIdentityFailure: null,
  _onVerifyIdentitySuccess: null,
  _onVerifyIdentityFailure: null,

  // Everytime we get a request from content, we put it in the queue. If
  // there are no pending operations though, we will execute it immediately.
  // In PeerConnectionObserver, whenever we are notified that an operation
  // has finished, we will check the queue for the next operation and execute
  // if neccesary. The _pending flag indicates whether an operation is currently
  // in progress.
  _queue: [],
  _pending: false,

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
    
    // Nothing starts until ICE gathering completes.
    this._queueOrRun({
      func: this._pc.initialize,
      args: [this._observer, win, Services.tm.currentThread]
    });

    this._win = win;
    this._winID = this._win.QueryInterface(Ci.nsIInterfaceRequestor)
                           .getInterface(Ci.nsIDOMWindowUtils).outerWindowID;

    dump("!!! mozPeerConnection constructor called " + this._win + "\n " + this._winID + "\n\n");
    this._uniqId = Cc["@mozilla.org/uuid-generator;1"]
                   .getService(Ci.nsIUUIDGenerator)
                   .generateUUID().toString();
  },

  // FIXME: Right now we do not enforce proper invocation (eg: calling
  // createOffer twice in a row is allowed).

  _queueOrRun: function(obj) {
    if (!this._pending) {
      dump("calling " + obj.func + "\n");
      obj.func.apply(this, obj.args);
      this._pending = true;
    } else {
      this._queue.push(obj);
    }
  },

  // Pick the next item from the queue and run it
  _executeNext: function() {
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

  _verifyIdentity: function(offer) {
    let self = this;

    // Extract the a=fingerprint and a=identity lines.
    let ire = new RegExp("a=identity:(.+)\r\n");
    let fre = new RegExp("a=fingerprint:(.+)\r\n");
    
    let id = offer.match(ire);
    let fprint = offer.match(fre);
    
    if (id.length == 2 && fprint.length == 2) {
      IDService.verifyIdentity(id[1], function(err, val) {
        if (val && (fprint[1] == val.message)) {
          self._onVerifyIdentitySuccess.onCallback(val);
          return;
        }
        self._onVerifyIdentityFailure.onCallback(err || "Signed message did not match");
      }); 
    } else {
      self._onVerifyIdentityFailure.onCallback("No identity information found");
    }
  },

  selectIdentity: function(onSuccess, onError) {
    dump("!!! selectIdentity called\n");
    this._onSelectIdentitySuccess = onSuccess;
    this._onSelectIdentityFailure = onError;

    this._queueOrRun({func: this._selectIdentity, args: null});
    dump("!!! selectIdentity returned\n");
  },

  verifyIdentity: function(offer, onSuccess, onError) {
    dump("!!! verifyIdentity called with\n");
    this._onVerifyIdentitySuccess = onSuccess;
    this._onVerifyIdentityFailure = onError;

    this._queueOrRun({func: this._verifyIdentity, args: [offer]});
    dump("!!! verifyIdentity returned\n");
  },

  createOffer: function(onSuccess, onError, constraints) {
    dump("!!! createOffer called\n");
    this._onCreateOfferSuccess = onSuccess;
    this._onCreateOfferFailure = onError;

    // TODO: Implement constraints/hints.
    if (!constraints) {
      constraints = "";
    }

    this._queueOrRun({func: this._pc.createOffer, args: [constraints]});
    dump("!!! createOffer returned\n");
  },

  createAnswer: function(offer, onSuccess, onError, constraints, provisional) {
    dump("!!! createAnswer called\n");
    this._onCreateAnswerSuccess = onSuccess;
    this._onCreateAnswerFailure = onError;

    if (!constraints) {
      constraints = "";
    }
    if (!provisional) {
      provisional = false;
    }

    // TODO: Implement provisional answer & constraints.
    this._queueOrRun({func: this._pc.createAnswer, args: ["", offer]});
    dump("!!! createAnswer returned\n");
  },

  setLocalDescription: function(action, description, onSuccess, onError) {
    this._onSetLocalDescriptionSuccess = onSuccess;
    this._onSetLocalDescriptionFailure = onError;

    let type;
    if (action == "offer") {
      type = Ci.IPeerConnection.kActionOffer;
    }
    if (action == "answer") {
      type = Ci.IPeerConnection.kActionAnswer;
    }

    dump("!!! setLocalDescription called\n");
    this._queueOrRun({func: this._pc.setLocalDescription, args: [type, description]});
    dump("!!! setLocalDescription returned\n");
  },

  setRemoteDescription: function(action, description, onSuccess, onError) {
    this._onSetRemoteDescriptionSuccess = onSuccess;
    this._onSetRemoteDescriptionFailure = onError;

    let type;
    if (action == "offer") {
      type = Ci.IPeerConnection.kActionOffer;
    }
    if (action == "answer") {
      type = Ci.IPeerConnection.kActionAnswer;
    }

    dump("!!! setRemoteDescription called\n");
    this._queueOrRun({func: this._pc.setRemoteDescription, args: [type, description]});
    dump("!!! setRemoteDescription returned\n");
  },

  updateIce: function(config, constraints, restart) {
    dump("!!! updateIce called\n");
    dump("!!! updateIce returned\n");
    return Cr.NS_ERROR_NOT_IMPLEMENTED;
  },

  addIceCandidate: function(candidate) {
    dump("!!! addIceCandidate called\n");
    dump("!!! addIceCandidate returned\n");
    return Cr.NS_ERROR_NOT_IMPLEMENTED;
  },

  addStream: function(stream, constraints) {
    dump("!!! addStream called\n");

    // TODO: Implement constraints.
    //this._queueOrRun({func: this._pc.addStream, args: [stream]});
    this._pc.addStream(stream);
    dump("!!! addStream returned\n");
  },

  removeStream: function(stream) {
    dump("!!! removeStream called\n");
    //this._queueOrRun({func: this._pc.removeStream, args: [stream]});
    this._pc.removeStream(stream);
    dump("!!! removeStream returned\n");
  },

  createDataChannel: function() {
    dump("!!! createDataChannel called\n");
    let channel = this._pc.createDataChannel(/*args*/);
    dump("!!! createDataChannel returned\n");
    return channel;
  },

  // FIX - remove connect() and listen()
  listen: function(port) {
    dump("!!! Listen() called\n");
    this._pc.listen(port)
    dump("!!! Listen() returned\n");
  },

  connect: function(addr, port) {
    dump("!!! Connect() called\n");
    this._pc.connect(addr, port);
    dump("!!! Connect() returned\n");
  },

  close: function() {
    dump("!!! close called\n");
    // Don't queue this one, since we just want to shutdown.
    this._pc.closeStreams();
    this._pc.close();
    dump("!!! close returned");
  },

  onRemoteStreamAdded: null,
  onDataChannelx: null,
  onConnectionx: null,
  onClosedConnectionx: null,

  // For testing only.
  createFakeMediaStream: function(type) {
    if (type == "video") {
      return this._pc.createFakeMediaStream(Ci.IPeerConnection.kHintVideo);
    }
    return this._pc.createFakeMediaStream(Ci.IPeerConnection.kHintAudio);
  }
};

// This is a seperate object because we don't want to expose it to DOM.
function PeerConnectionObserver(dompc) {
  this._dompc = dompc;
}
PeerConnectionObserver.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.IPeerConnectionObserver]),

  onCreateOfferSuccess: function(offer) {
    dump("!!! onCreateOfferSuccess called\n");

    // Before calling the success callback, check if selectIdentity was
    // previously called and that an identity was obtained. If so, add
    // a signed string to the SDP before sending it to content.
    if (!this._dompc._identity) {
      this._dompc._onCreateOfferSuccess.onCallback(offer);
      this._dompc._executeNext();
      return;
    }

    let sig = this._dompc._pc.fingerprint;

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
      let finalOffer = parts[0] + sigline + idline;
      for (let i = 1; i < parts.length; i++) {
        finalOffer += "m=" + parts[i];
      }

      dump("!!! Generated final offer: " + finalOffer + "\n\n");
      self._dompc._onCreateOfferSuccess.onCallback(finalOffer);
      self._dompc._executeNext();
    });
  },

  onCreateOfferError: function(code) {
    dump("!!! onCreateOfferError called: " + code + "\n");
    if (this._dompc._onCreateOfferFailure) {
      this._dompc._onCreateOfferFailure.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onCreateAnswerSuccess: function(answer) {
    dump("!!! onCreateAnswerSuccess called\n");
    if (this._dompc._onCreateAnswerSuccess) {
      this._dompc._onCreateAnswerSuccess.onCallback(answer);
    }
    this._dompc._executeNext();
  },

  onCreateAnswerError: function(code) {
    dump("!!! onCreateAnswerError called: " + code + "\n");
    if (this._dompc._onCreateAnswerFailure) {
      this._dompc._onCreateAnswerFailure.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onSetLocalDescriptionSuccess: function(code) {
    dump("!!! onSetLocalDescriptionSuccess called\n");
    if (this._dompc._onSetLocalDescriptionSuccess) {
      this._dompc._onSetLocalDescriptionSuccess.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onSetRemoteDescriptionSuccess: function(code) {
    dump("!!! onSetRemoteDescriptionSuccess called\n");
    if (this._dompc._onSetRemoteDescriptionSuccess) {
      this._dompc._onSetRemoteDescriptionSuccess.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onSetLocalDescriptionError: function(code) {
    dump("!!! onSetLocalDescriptionError called: " + code + "\n");
    if (this._dompc._onSetLocalDescriptionFailure) {
      this._dompc._onSetLocalDescriptionFailure.onCallback(code);
    }
    this._dompc._executeNext();
  },

  onSetRemoteDescriptionError: function(code) {
    dump("!!! onSetRemoteDescriptionError called: " + code + "\n");
    if (this._dompc._onSetRemoteDescriptionFailure) {
      this._dompc._onSetRemoteDescriptionFailure.onCallback(code);
    }
    this._dompc._executeNext();
  },

  // FIXME: Following observer events should update state on this._dompc.
  onStateChange: function(state) {
    dump("!!! onStateChange called: " + state + "\n");

    if (state == Ci.IPeerConnectionObserver.kIceState) {
      switch (this._dompc._pc.iceState) {
        case Ci.IPeerConnection.kIceWaiting:
        case Ci.IPeerConnection.kIceChecking:
        case Ci.IPeerConnection.kIceConnected:
          dump("!!! ICE gathering is complete, calling _executeNext! \n");
          this._dompc._executeNext();
          break;
      }
    }
  },

  onAddStream: function(stream, type) {
    dump("!!! onAddStream called: " + stream + " :: " + type + "\n");
    if (this._dompc.onRemoteStreamAdded) {
      this._dompc.onRemoteStreamAdded.onCallback({stream: stream, type: type});
    }
    this._dompc._executeNext();
  },

  onRemoveStream: function() {
    dump("!!! onRemoveStream called\n");
    this._dompc._executeNext();
  },

  onAddTrack: function() {
    dump("!!! onAddTrack called\n");
    this._dompc._executeNext();
  },

  onRemoveTrack: function() {
    dump("!!! onRemoveTrack called\n");
    this._dompc._executeNext();
  },

  onConnection: function() {
    dump("!!! onConnection called\n");
    if (this._dompc.onConnectionx) {
      this._dompc.onConnectionx.onCallback();
    }
    this._dompc._executeNext();
  },

  onClosedConnection: function() {
    dump("!!! onClosedConnection called\n");
    if (this._dompc.onClosedConnectionx) {
      this._dompc.onClosedConnectionx.onCallback();
    }
    this._dompc._executeNext();
  },

  onDataChannel: function(channel) {
    dump("!!! onDataChannel called: " + channel + "\n");
    if (this._dompc.onDataChannelx) {
      this._dompc.onDataChannelx.onCallback(channel);
    }
    this._dompc._executeNext();
  },

  foundIceCandidate: function(candidate) {
    dump("!!! foundIceCandidate called: " + candidate + "\n");
    this._dompc._executeNext();
  }
};

let NSGetFactory = XPCOMUtils.generateNSGetFactory([PeerConnection]);
