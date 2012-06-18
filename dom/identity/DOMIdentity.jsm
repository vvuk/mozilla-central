/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

// This is the parent process corresponding to nsDOMIdentity.
let EXPORTED_SYMBOLS = ["DOMIdentity"];

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "IdentityService",
                                  "resource://gre/modules/identity/Identity.jsm");

function log(msg) {
  if (!IdentityService._debugMode) {
    return;
  }

  dump("DOMIdentity: " + msg + "\n");
}

// Maps the callback objects to the message manager
// to be used to send the message back to the child.
let mmMap = new WeakMap();

function IDDOMMessage(aID) {
  this.id = this.oid = aID; // TODO: decide on id or oid
}

function IDPProvisioningContext(aID, aOrigin, aTargetMM) {
  this._id = aID;
  this._origin = aOrigin;
  mmMap.set(this, aTargetMM);
}

IDPProvisioningContext.prototype = {
  get id() this._id,
  get origin() this._origin,
  get mm() mmMap.get(this),

  doBeginProvisioningCallback: function IDPProvisioningContext_doBeginProvisioningCallback(aID, aCertDuration) {
    let message = new IDDOMMessage(this.id);
    message.identity = aID;
    message.certDuration = aCertDuration;
    this.mm.sendAsyncMessage("Identity:IDP:CallBeginProvisioningCallback", message);
  },

  doGenKeyPairCallback: function IDPProvisioningContext_doGenKeyPairCallback(aPublicKey) {
log("DOMIdentity: doGenKeyPairCallback");
    let message = new IDDOMMessage(this.id);
    message.publicKey = aPublicKey;
    this.mm.sendAsyncMessage("Identity:IDP:CallGenKeyPairCallback", message);
  },

  doError: function(msg) {
    log("Provisioning ERROR: " + msg);
  },
};

function IDPAuthenticationContext(aID, aOrigin, aTargetMM) {
  this._id = aID;
  this._origin = aOrigin;
  mmMap.set(this, aTargetMM);
}

IDPAuthenticationContext.prototype = {
  get id() this._id,
  get origin() this._origin,
  get mm() mmMap.get(this),

  doBeginAuthenticationCallback: function(aIdentity) {
    let message = new IDDOMMessage(this.id);
    message.identity = aIdentity;
    this.mm.sendAsyncMessage("Identity:IDP:CallBeginAuthenticationCallback", message);
  },

  doError: function(msg) {
    log("Authentication ERROR: " + msg);
  },
};

function RPWatchContext(aID, aOrigin, aLoggedInEmail, aTargetMM) {
  this._id = aID;
  this._origin = aOrigin;
  this._loggedInEmail = aLoggedInEmail;
  mmMap.set(this, aTargetMM);
}

RPWatchContext.prototype = {
  get id() this._id,
  get origin() this._origin,
  get loggedInEmail() this._loggedInEmail,
  get mm() mmMap.get(this),

  doLogin: function RPWatchContext_onlogin(aAssertion) {
    log("doLogin: " + this.id + " : " + aAssertion);
    let message = new IDDOMMessage(this.id);
    message.assertion = aAssertion;
    log(message.id + " : " + message.oid);
    this.mm.sendAsyncMessage("Identity:RP:Watch:OnLogin", message);
  },

  doLogout: function RPWatchContext_onlogout() {
    log("doLogout :" + this.id);
    let message = new IDDOMMessage(this.id);
    this.mm.sendAsyncMessage("Identity:RP:Watch:OnLogout", message);
  },

  doReady: function RPWatchContext_onready() {
    log("doReady: " + this.id);
    let message = new IDDOMMessage(this.id);
    this.mm.sendAsyncMessage("Identity:RP:Watch:OnReady", message);
  },

  doError: function RPWatchContext_onerror() {
    // XXX handle errors that might be raised in the execution
    // of watch().
    log("Ow!");
  }
};

let DOMIdentity = {
  // nsIFrameMessageListener
  receiveMessage: function(aMessage) {
    let msg = aMessage.json;

    // Target is the frame message manager that called us and is
    // used to send replies back to the proper window.
    let targetMM = aMessage.target
                           .QueryInterface(Ci.nsIFrameLoaderOwner)
                           .frameLoader.messageManager;

    switch (aMessage.name) {
      // RP
      case "Identity:RP:Watch":
        this._watch(msg, targetMM);
        break;
      case "Identity:RP:Request":
        this._request(msg);
        break;
      case "Identity:RP:Logout":
        this._logout(msg);
        break;
      // IDP
      case "Identity:IDP:BeginProvisioning":
        this._beginProvisioning(msg, targetMM);
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
        this._beginAuthentication(msg, targetMM);
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
    switch (aTopic) {
      case "domwindowopened":
      case "domwindowclosed":
        let win = aSubject.QueryInterface(Ci.nsIInterfaceRequestor)
                          .getInterface(Ci.nsIDOMWindow);
        this._configureMessages(win, aTopic == "domwindowopened");
        break;

      case "xpcom-shutdown":
        Services.ww.unregisterNotification(this);
        Services.obs.removeObserver(this, "xpcom-shutdown");
        break;
    }
  },

  messages: ["Identity:RP:Watch", "Identity:RP:Request", "Identity:RP:Logout",
             "Identity:IDP:ProvisioningFailure", "Identity:IDP:BeginProvisioning",
             "Identity:IDP:GenKeyPair", "Identity:IDP:RegisterCertificate",
             "Identity:IDP:BeginAuthentication", "Identity:IDP:CompleteAuthentication",
             "Identity:IDP:AuthenticationFailure"],

  // Private.
  _init: function() {
    Services.ww.registerNotification(this);
    Services.obs.addObserver(this, "xpcom-shutdown", false);

    this._pending = {};
  },

  _configureMessages: function(aWindow, aRegister) {
    if (!aWindow.messageManager)
      return;
 
    let func = aRegister ? "addMessageListener" : "removeMessageListener";

    for (let message of this.messages) {
      aWindow.messageManager[func](message, this);
    }
  },

  _watch: function(message, targetMM) {
    // Forward to Identity.jsm and stash the oid somewhere so we can make
    // callback after sending a message to parent process.
    this._pending[message.oid] = true; // TODO?
    let context = new RPWatchContext(message.oid, message.origin,
                                     message.loggedInEmail, targetMM);
    IdentityService.RP.watch(context);
  },

  _request: function(message) {
    IdentityService.RP.request(message.oid, message);
  },

  _logout: function(message) {
    IdentityService.RP.logout(message.oid, message.origin);
  },

  _beginProvisioning: function(message, targetMM) {
    let context = new IDPProvisioningContext(message.oid, message.origin, targetMM);
    IdentityService.IDP.beginProvisioning(context);
  },

  _genKeyPair: function(message) {
    IdentityService.IDP.genKeyPair(message.oid); // TODO: pass ref. to callback?
  },

  _registerCertificate: function(message) {
    IdentityService.IDP.registerCertificate(message.oid, message.cert);
  },

  _provisioningFailure: function(message) {
    IdentityService.IDP.raiseProvisioningFailure(message.oid, message.reason);
  },

  _beginAuthentication: function(message, targetMM) {
    let context = new IDPAuthenticationContext(message.oid, message.origin, targetMM);
    IdentityService.IDP.beginAuthentication(context);
  },

  _completeAuthentication: function(message) {
    IdentityService.IDP.completeAuthentication(message.oid);
  },

  _authenticationFailure: function(message) {
    IdentityService.IDP.cancelAuthentication(message.oid); // TODO: see issue #4
  },
};

// Object is initialized by nsIDService.js
