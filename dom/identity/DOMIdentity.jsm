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

let DOMIdentity = {
  // nsIFrameMessageListener
  receiveMessage: function(aMessage) {
    let msg = aMessage.json;
    switch (aMessage.name) {
      // RP
      case "Identity:Watch":
        this._watch(msg);
        break;
      case "Identity:Request":
        this._request(msg);
        break;
      case "Identity:Logout":
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
    ppmm.sendAsyncMessage("Identity:Watch:OnLogout", message);
  },

  // Private.
  _init: function() {
    this.messages = ["Identity:Watch", "Identity:Request", "Identity:Logout",
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
    this._pending[message.oid] = true;
    IdentityService.watch(message.loggedIn, message.oid);
  },

  _request: function(message) {
    IdentityService.request(message.oid, message);

    // TODO: Oh look we got a fake assertion back from the JSM, send it onward.
    let message = {
      // Should not be empty because _watch was *definitely* called before this.
      // Right? Right.
      oid: message.oid,
      assertion: "fake.jwt.token"
    };
    ppmm.sendAsyncMessage("Identity:Watch:OnLogin", message);

  },

  _logout: function(message) {
    IdentityService.logout(message.oid);
  },

  _beginProvisioning: function(message) {
    IdentityService.beginProvisioning(message.oid);

    // TODO: move below code to function that Identity.jsm calls
    let data = IdentityService._provisionFlows[message.oid];

    ppmm.sendAsyncMessage("Identity:IDP:CallBeginProvisioningCallback", {
      identity: data.identity,
      cert_duration: IdentityService.certDuration,
      oid: message.oid,
    });
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
    IdentityService.beginAuthentication(message.oid);
  },

  _completeAuthentication: function(message) {
    IdentityService.completeAuthentication(message.oid);
  },

  _authenticationFailure: function(message) {
    IdentityService.cancelAuthentication(message.oid); // TODO: see issue #4
  },
};

// Object is initialized by nsIDService.js
