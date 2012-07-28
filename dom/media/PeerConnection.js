/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

const PC_CONTRACT = "@mozilla.org/dom/peerconnection;1";
const PC_CID = Components.ID("{7cb2b368-b1ce-4560-acac-8e0dbda7d3d0}");

function PeerConnection() {
  this._pc = Cc["@mozilla.org/peerconnection;1"].
             createInstance(Ci.IPeerConnection);
  this._observer = new PeerConnectionObserver(this);

  dump("!!! mozPeerConnection constructor called " + this._pc + "\n\n");
  this._pc.initialize(this._observer, Services.tm.currentThread);
}
PeerConnection.prototype = {

  _pc: null,
  _observer: null,
  _onCreateOfferSuccess: null,
  _onCreateOfferFailure: null,
  _onCreateAnswerSuccess: null,
  _onCreateAnswerFailure: null,

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
                                    interfaces: [Ci.nsIDOMRTCPeerConnection],
                                    flags: Ci.nsIClassInfo.DOM_OBJECT}),

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIDOMRTCPeerConnection]),

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

  close: function() {
    dump("!!! close called\n");
    // Don't queue this one, since we just want to shutdown.
    this._pc.closeStreams();
    this._pc.close();
    dump("!!! close returned");
  },

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

  // Pick the next item from the queue and run it
  _executeNext: function() {
    if (this._dompc._queue.length) {
      let obj = this._dompc._queue.shift();
      obj.func.apply(this._dompc, obj.args);
    } else {
      this._dompc._pending = false;
    }
  },

  onCreateOfferSuccess: function(offer) {
    dump("!!! onCreateOfferSuccess called\n");
    if (this._dompc._onCreateOfferSuccess) {
      this._dompc._onCreateOfferSuccess.onCallback(offer);
    }
    this._executeNext();
  },

  onCreateOfferError: function(code) {
    dump("!!! onCreateOfferError called: " + code + "\n");
    if (this._dompc._onCreateOfferFailure) {
      this._dompc._onCreateOfferFailure.onCallback(code);
    }
    this._executeNext();
  },

  onCreateAnswerSuccess: function(answer) {
    dump("!!! onCreateAnswerSuccess called\n");
    if (this._dompc._onCreateAnswerSuccess) {
      this._dompc._onCreateAnswerSuccess.onCallback(answer);
    }
    this._executeNext();
  },

  onCreateAnswerError: function(code) {
    dump("!!! onCreateAnswerError called: " + code + "\n");
    if (this._dompc._onCreateAnswerFailure) {
      this._dompc._onCreateAnswerFailure.onCallback(code);
    }
    this._executeNext();
  },

  onSetLocalDescriptionSuccess: function(code) {
    dump("!!! onSetLocalDescriptionSuccess called\n");
    if (this._dompc._onSetLocalDescriptionSuccess) {
      this._dompc._onSetLocalDescriptionSuccess.onCallback(code);
    }
    this._executeNext();
  },

  onSetRemoteDescriptionSuccess: function(code) {
    dump("!!! onSetRemoteDescriptionSuccess called\n");
    if (this._dompc._onSetRemoteDescriptionSuccess) {
      this._dompc._onSetRemoteDescriptionSuccess.onCallback(code);
    }
    this._executeNext();
  },

  onSetLocalDescriptionError: function(code) {
    dump("!!! onSetLocalDescriptionError called: " + code + "\n");
    if (this._dompc._onSetLocalDescriptionFailure) {
      this._dompc._onSetLocalDescriptionFailure.onCallback(code);
    }
    this._executeNext();
  },

  onSetRemoteDescriptionError: function(code) {
    dump("!!! onSetRemoteDescriptionError called: " + code + "\n");
    if (this._dompc._onSetRemoteDescriptionFailure) {
      this._dompc._onSetRemoteDescriptionFailure.onCallback(code);
    }
    this._executeNext();
  },

  // FIXME: Following observer events should update state on this._dompc.
  onStateChange: function(state) {
    dump("!!! onStateChange called: " + state + "\n");
    this._executeNext();
  },

  onAddStream: function(stream) {
    dump("!!! onAddStream called: " + stream + "\n");
    this._executeNext();
  },

  onRemoveStream: function() {
    dump("!!! onRemoveStream called\n");
    this._executeNext();
  },

  onAddTrack: function() {
    dump("!!! onAddTrack called\n");
    this._executeNext();
  },

  onRemoveTrack: function() {
    dump("!!! onRemoveTrack called\n");
    this._executeNext();
  },

  foundIceCandidate: function(candidate) {
    dump("!!! foundIceCandidate called: " + candidate + "\n");
    this._executeNext();
  }
};

let NSGetFactory = XPCOMUtils.generateNSGetFactory([PeerConnection]);
