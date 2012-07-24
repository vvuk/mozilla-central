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
  this._observer = new PeerConnectionObserver(this._pc);
  dump("!!!!!!! mozPeerConnection constructor called " + this._pc + "\n\n");
  this._pc.initialize(this._observer);
}
PeerConnection.prototype = {

  _pc: null,
  _observer: null,
  _onCreateOfferSuccess: null,
  _onCreateOfferFailure: null,
  _onCreateAnswerSuccess: null,
  _onCreateAnswerFailure: null,

  classID: PC_CID,

  classInfo: XPCOMUtils.generateCI({classID: PC_CID,
                                    contractID: PC_CONTRACT,
                                    classDescription: "PeerConnection",
                                    interfaces: [Ci.nsIDOMRTCPeerConnection],
                                    flags: Ci.nsIClassInfo.DOM_OBJECT}),

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIDOMRTCPeerConnection]),

  createOffer: function(onSuccess, onError, constraints) {
    dump("!!! createOffer called\n");
    this._onCreateOfferSuccess = onSuccess;
    this._onCreateAnswerFailure = onError;

    // TODO: Implement constraints/hints.
    if (!constraints) {
      constraints = "";
    }
    this._pc.createOffer(constraints);

    dump("!!! createOffer returned\n");
  },

  createAnswer: function(offer, onSuccess, onError, constraints, provisional) {
    dump("!!! createAnswer called\n");
    this._onCreateAnswerSuccess = onSuccess;
    this._onCreateAnswerFailure = onError;

    if (!constraints) {
      constraints = "";
    }

    // TODO: Implement provisional answer.
    this._pc.createAnswer(constraints, offer);

    dump("!!! createAnswer returned\n");
  },

  setLocalDescription: function(action, description) {
    dump("!!! setLocalDescription called\n");
    this._pc.setLocalDescription(action, description);
    dump("!!! setLocalDescription returned\n");
  },

  setRemoteDescription: function(action, description) {
    dump("!!! setRemoteDescription called\n");
    this._pc.setRemoteDescription(action, description);
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
    this._pc.addStream(stream);
    dump("!!! addStream returned\n");
  },

  removeStream: function(stream) {
    dump("!!! removeStream called\n");
    this._pc.removeStream(stream);
    dump("!!! removeStream returned\n");
  },

  close: function() {
    dump("!!! close called\n");
    this._pc.closeStreams();
    this._pc.close();
    dump("!!! close returned");
  }
};

// This is a seperate object because we don't want to expose it to DOM.
function PeerConnectionObserver(pc) {
  this._pc = pc;
}
PeerConnectionObserver.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.IPeerConnectionObserver]),

  onCreateOfferSuccess: function(offer) {

  },

  onCreateOfferError: function(code) {

  },

  onCreateAnswerSuccess: function(answer) {

  },

  onCreateAnswerError: function(code) {

  },

  onSetLocalDescriptionSuccess: function(code) {

  },

  onSetRemoteDescriptionSuccess: function(code) {

  },

  onSetLocalDescriptionError: function(code) {

  },

  onSetRemoteDescriptionError: function(code) {

  },

  onStateChange: function(state) {

  },

  // void onAddStream(MediaTrackTable* stream) = 0; XXX: figure out this one later

  onRemoveStream: function() {

  },
  onAddTrack: function() {

  },
  onRemoveTrack: function() {

  },

  foundIceCandidate: function(candidate) {

  }
};

let NSGetFactory = XPCOMUtils.generateNSGetFactory([PeerConnection]);
