/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

// This is the child process corresponding to nsIDOMIdentity.

XPCOMUtils.defineLazyGetter(this, "cpmm", function() {
  return Cc["@mozilla.org/childprocessmessagemanager;1"].
    getService(Ci.nsIFrameMessageManager);
});

function nsDOMIdentity() {
}
nsDOMIdentity.prototype = {

  // nsIDOMIdentity
  /**
   * Relying Party (RP) APIs
   */

  watch: function(params) {
    dump("Called watch for ID " + this._id + "\n");
    // Latest watch call wins in case site makes multiple calls.
    this._watcher = params;

    let message = {
      oid: this._id,
      loggedIn: params.loggedInEmail, // Could be undefined or null
      from: this._window.location.href
    };
    cpmm.sendAsyncMessage("Identity:Watch", message);
  },

  request: function(aOptions) {
    // Has the caller called watch() before this?
    if (!this._watcher) {
      throw new Error("navigator.id.request called before navigator.id.watch");
    }

    // TODO: store aOptions.onCancel callback if present

    let message = {
      oid: this._id,
      from: this._window.location.href,
    };

    if (aOptions) {
      message.requiredEmail = aOptions.requiredEmail;
      message.privacyURL = aOptions.privacyURL;
      message.tosURL = aOptions.tosURL;
    }

    cpmm.sendAsyncMessage("Identity:Request", message);
  },

  logout: function(aCallback) {
    cpmm.sendAsyncMessage("Identity:Logout", {
      oid: this._id,
      from: this._window.location.href
    });
    if (aCallback) {
      // TODO: when is aCallback supposed to be called and what are the arguments?
      aCallback();
    }
  },

  /**
   *  Identity Provider (IDP) APIs
   */

  beginProvisioning: function(aCallback) {
    dump("DOM beginProvisioning: " + this._id + "\n");
    this._provisioningCallback = aCallback;
    cpmm.sendAsyncMessage("Identity:IDP:BeginProvisioning", {
      oid: this._id,
      from: this._window.location.href,
    });
  },

  _callBeginProvisioningCallback: function(message) {
    let identity = message.identity;
    let certValidityDuration = message.cert_duration;
    this._provisioningCallback(identity, certValidityDuration);
  },

  genKeyPair: function(aCallback) {
    dump("DOM genKeyPair\n");
    this._genKeyPairCallback = aCallback;
    cpmm.sendAsyncMessage("Identity:IDP:GenKeyPair", {
      oid: this._id,
      from: this._window.location.href,
    });
  },

  registerCertificate: function(aCertificate) {
    cpmm.sendAsyncMessage("Identity:IDP:RegisterCertificate", {
      oid: this._id,
      from: this._window.location.href,
      cert: aCertificate,
    });
  },

  raiseProvisioningFailure: function(aReason) {
    dump("nsDOMIdentity: raiseProvisioningFailure '" + aReason + "'\n");
    cpmm.sendAsyncMessage("Identity:IDP:ProvisioningFailure", {
      oid: this._id,
      from: this._window.location.href,
      reason: aReason,
    });
    // TODO: close provisioning sandbox/window
    this._window.close();
  },

  // nsIFrameMessageListener
  receiveMessage: function(aMessage) {
    let msg = aMessage.json;

    // Is this message intended for this window?
    if (msg.oid != this._id) {
      return;
    }

    switch (aMessage.name) {
      case "Identity:Watch:OnLogin":
        // Do we have a watcher?
        if (!this._watcher) {
          return;
        }

        if (this._watcher.onlogin) {
          this._watcher.onlogin(msg.assertion);
        }
        break;
      case "Identity:Watch:OnLogout":
        // Do we have a watcher?
        if (!this._watcher) {
          return;
        }

        if (this._watcher.onlogout) {
          this._watcher.onlogout();
        }
        break;
      case "Identity:Watch:OnReady":
        // Do we have a watcher?
        if (!this._watcher) {
          return;
        }

        if (this._watcher.onready) {
          this._watcher.onready();
        }
        break;
      case "Identity:IDP:CallBeginProvisioningCallback":
        this._callBeginProvisioningCallback(msg);
        break;
    }
  },

  // nsIObserver
  observe: function(aSubject, aTopic, aData) {
    let wId = aSubject.QueryInterface(Ci.nsISupportsPRUint64).data;
    if (wId != this._innerWindowID) {
      return;
    }

    Services.obs.removeObserver(this, "inner-window-destroyed");       
    this._window = null;
    this._watcher = null;
    this._beginProvisioningCallback = null;
    this._genKeyPairCallback = null;

    // Also send message to DOMIdentity.jsm notifiying window is no longer valid
    this._messages.forEach((function(msgName) {
      cpmm.removeMessageListener(msgName, this);
    }).bind(this));
  },

  // nsIDOMGlobalPropertyInitializer
  init: function(aWindow) {
    dump("init was called from " + aWindow.document.location + "\n\n");

    // Store window and origin URI.
    this._watcher = null;
    this._beginProvisioningCallback = null;
    this._genKeyPairCallback = null;
    this._window = aWindow;
    this._origin = aWindow.document.nodePrincipal.uri;

    // Setup identifiers for current window.
    let util = aWindow.QueryInterface(Ci.nsIInterfaceRequestor).
      getInterface(Ci.nsIDOMWindowUtils);
    this._id = util.outerWindowID;
    this._innerWindowID = util.currentInnerWindowID;

    // Setup listeners for messages from child process.
    this._messages = [
      "Identity:Watch:OnLogin",
      "Identity:Watch:OnLogout",
      "Identity:Watch:OnReady",
      "Identity:IDP:CallBeginProvisioningCallback",
    ];
    this._messages.forEach((function(msgName) {
      cpmm.addMessageListener(msgName, this);
    }).bind(this));

    // Setup observers so we can remove message listeners.
    Services.obs.addObserver(this, "inner-window-destroyed", false);
  },
  
  // Private.
  _getRandomId: function() {
    return Cc["@mozilla.org/uuid-generator;1"].
      getService(Ci.nsIUUIDGenerator).
      generateUUID().toString();
  },

  // Component setup.
  classID: Components.ID("{8bcac6a3-56a4-43a4-a44c-cdf42763002f}"),

  QueryInterface: XPCOMUtils.generateQI(
    [Ci.nsIDOMIdentity, Ci.nsIDOMGlobalPropertyInitializer]
  ),
  
  classInfo: XPCOMUtils.generateCI({
    classID: Components.ID("{8bcac6a3-56a4-43a4-a44c-cdf42763002f}"),
    contractID: "@mozilla.org/identity;1",
    interfaces: [Ci.nsIDOMIdentity],
    flags: Ci.nsIClassInfo.DOM_OBJECT,
    classDescription: "Identity DOM Implementation"
  })
}

const NSGetFactory = XPCOMUtils.generateNSGetFactory([nsDOMIdentity]);
