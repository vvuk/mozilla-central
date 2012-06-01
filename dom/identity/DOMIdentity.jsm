/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

// This is the parent process corresponding to nsDOMIdentity.
let EXPORTED_SYMBOLS = ["DOMIdentity"];

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyGetter(this, "ppmm", function() {
  return Cc["@mozilla.org/parentprocessmessagemanager;1"].getService(
    Ci.nsIFrameMessageManager
  );
});

XPCOMUtils.defineLazyModuleGetter(this, "IdentityService",
                                  "resource://gre/modules/identity/Identity.jsm");

function log(msg) { // TODO: debug
  dump("DOMIdentity: " + msg + "\n");
}

function IDDOMMessage(aID) {
  this.id = this.oid = aID; // TODO: decide on id or oid
}

function IDPProvisioningContext(aID, aOrigin) {
  this._id = aID;
  this._origin = aOrigin;
}

IDPProvisioningContext.prototype = {
  get id() this._id,
  get origin() this._origin,

  doBeginProvisioningCallback: function IDPProvisioningContext_doBeginProvisioningCallback(aID, aCertDuration) {
    let message = new IDDOMMessage(this.id);
    message.identity = aID;
    message.certDuration = aCertDuration;
    ppmm.sendAsyncMessage("Identity:IDP:CallBeginProvisioningCallback", message);
  },

  doGenKeyPairCallback: function IDPProvisioningContext_doGenKeyPairCallback(aPublicKey) {
    let message = new IDDOMMessage(this.id);
    message.publicKey = aPublicKey;
    ppmm.sendAsyncMessage("Identity:IDP:CallGenKeyPairCallback", message);
  },

  doError: function(msg) {
    log("Provisioning ERROR: " + msg);
  },
};

function IDPAuthenticationContext(aID, aOrigin) {
  this._id = aID;
  this._origin = aOrigin;
}

IDPAuthenticationContext.prototype = {
  get id() this._id,
  get origin() this._origin,

  doBeginAuthenticationCallback: function(aIdentity) {
    let message = new IDDOMMessage(this.id);
    message.identity = aIdentity;
    ppmm.sendAsyncMessage("Identity:IDP:CallBeginAuthenticationCallback", message);
  },
};

function RPWatchContext(aID, aOrigin, aLoggedInEmail) {
  this._id = aID;
  this._origin = aOrigin;
  this._loggedInEmail = aLoggedInEmail;
}

RPWatchContext.prototype = {
  get id() this._id,
  get origin() this._origin,
  get loggedInEmail() this._loggedInEmail,

  doLogin: function RPWatchContext_onlogin(aAssertion) {
    log("doLogin: " + this.id + " : " + aAssertion);
    let message = new IDDOMMessage(this.id);
    message.assertion = aAssertion;
    log(message.id + " : " + message.oid);
    ppmm.sendAsyncMessage("Identity:RP:Watch:OnLogin", message);
  },

  doLogout: function RPWatchContext_onlogout() {
    log("doLogout :" + this.id);
    let message = new IDDOMMessage(this.id);
    ppmm.sendAsyncMessage("Identity:RP:Watch:OnLogout", message);
  },

  doReady: function RPWatchContext_onready() {
    log("doReady: " + this.id);
    let message = new IDDOMMessage(this.id);
    ppmm.sendAsyncMessage("Identity:RP:Watch:OnReady", message);
  }
};

let DOMIdentity = {
  // nsIFrameMessageListener
  receiveMessage: function(aMessage) {
    let msg = aMessage.json;
    switch (aMessage.name) {
      // RP
      case "Identity:RP:Watch":
        this._watch(msg);
        break;
      case "Identity:RP:Request":
        this._request(msg);
        break;
      case "Identity:RP:Logout":
        this._logout(msg);
        break;
      // IDP
      case "Identity:IDP:BeginProvisioning":
        this._beginProvisioning(msg);
        break;
      case "Identity:IDP:GenKeyPair":
        this._genKeyPair(msg);
        break;
      case "Identity:IDP:RegisterCertificate":
        this._registerCertificate(msg);
        break;
      case "Identity:IDP:ProvisioningFailure":
        this._provisioningFailure(msg);
        break;
      case "Identity:IDP:BeginAuthentication":
        this._beginAuthentication(msg);
        break;
      case "Identity:IDP:CompleteAuthentication":
        this._completeAuthentication(msg);
        break;
      case "Identity:IDP:AuthenticationFailure":
        this._authenticationFailure(msg);
        break;
    }
  },

  // nsIObserver
  observe: function(aSubject, aTopic, aData) {
    if (aTopic != "xpcom-shutdown") {
      return;
    }

    this.messages.forEach((function(msgName) {
      ppmm.removeMessageListener(msgName, this);
    }).bind(this));
    Services.obs.removeObserver(this, "xpcom-shutdown");
    ppmm = null;
  },

  // Methods for IdentityService to call
  /**
   * Call the onlogout RP callback
   */
  onLogout: function(oid) {
    // TODO: check rv above and then fire onlogout?
    let message = {
      oid: oid,
    };
    ppmm.sendAsyncMessage("Identity:RP:Watch:OnLogout", message);
  },

  // Private.
  _init: function() {
    this.messages = ["Identity:RP:Watch", "Identity:RP:Request", "Identity:RP:Logout",
                     "Identity:IDP:ProvisioningFailure", "Identity:IDP:BeginProvisioning",
                     "Identity:IDP:GenKeyPair", "Identity:IDP:RegisterCertificate",
                     "Identity:IDP:BeginAuthentication", "Identity:IDP:CompleteAuthentication",
                     "Identity:IDP:AuthenticationFailure"];

    this.messages.forEach((function(msgName) {
      ppmm.addMessageListener(msgName, this);
    }).bind(this));

    Services.obs.addObserver(this, "xpcom-shutdown", false);

    this._pending = {};
  },

  _watch: function(message) {
    // Forward to Identity.jsm and stash the oid somewhere so we can make
    // callback after sending a message to parent process.
    this._pending[message.oid] = true; // TODO?
    let context = new RPWatchContext(message.oid, message.origin, message.loggedInEmail);
    IdentityService.watch(context);
  },

  _request: function(message) {
    IdentityService.request(message.oid, message);
  },

  _logout: function(message) {
    IdentityService.logout(message.oid);
  },

  _beginProvisioning: function(message) {
    let context = new IDPProvisioningContext(message.oid, message.origin);
    IdentityService.beginProvisioning(context);
  },

  _genKeyPair: function(message) {
    IdentityService.genKeyPair(message.oid); // TODO: pass ref. to callback?
  },

  _registerCertificate: function(message) {
    IdentityService.registerCertificate(message.oid, message.cert);
  },

  _provisioningFailure: function(message) {
    IdentityService.raiseProvisioningFailure(message.oid, message.reason);
  },

  _beginAuthentication: function(message) {
    let context = new IDPAuthenticationContext(message.oid, message.origin);
    IdentityService.beginAuthentication(context);
  },

  _completeAuthentication: function(message) {
    IdentityService.completeAuthentication(message.oid);
  },

  _authenticationFailure: function(message) {
    IdentityService.cancelAuthentication(message.oid); // TODO: see issue #4
  },
};

// Object is initialized by nsIDService.js
