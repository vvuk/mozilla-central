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

function log(msg) {
  dump("nsDOMIdentity: " + msg + "\n");
}

function nsDOMIdentity() {
}
nsDOMIdentity.prototype = {

  // nsIDOMIdentity
  /**
   * Relying Party (RP) APIs
   */

  watch: function(params) {
    dump("Called watch for ID " + this._id + " with loggedInEmail " + params.loggedInEmail + "\n");
    // Latest watch call wins in case site makes multiple calls.
    this._watcher = params;

    let message = {
      oid: this._id,
      origin: this._origin,
      loggedInEmail: params.loggedInEmail, // Could be undefined or null
    };
    cpmm.sendAsyncMessage("Identity:RP:Watch", message);
  },

  request: function(aOptions) {
    // Has the caller called watch() before this?
    if (!this._watcher) {
      throw new Error("navigator.id.request called before navigator.id.watch");
    }

    let message = {
      oid: this._id,
      origin: this._origin,
    };

    if (aOptions) {
      if (aOptions.oncancel) {
        this._onCancelRequestCallback = aOptions.oncancel;
      }

      message.requiredEmail = aOptions.requiredEmail;
      message.privacyURL = aOptions.privacyURL;
      message.tosURL = aOptions.tosURL;
    }

    cpmm.sendAsyncMessage("Identity:RP:Request", message);
  },

  logout: function() {
    if (!this._watcher) {
      throw new Error("navigator.id.logout called before navigator.id.watch");
    }
    cpmm.sendAsyncMessage("Identity:RP:Logout", {
      oid: this._id,
      origin: this._origin,
    });
  },

  /**
   *  Identity Provider (IDP) APIs
   */

  beginProvisioning: function(aCallback) {
    dump("DOM beginProvisioning: " + this._id + "\n");
    this._beginProvisioningCallback = aCallback;
    cpmm.sendAsyncMessage("Identity:IDP:BeginProvisioning", {
      oid: this._id,
      origin: this._origin,
    });
  },

  genKeyPair: function(aCallback) {
    dump("DOM genKeyPair\n");
    this._genKeyPairCallback = aCallback;
    cpmm.sendAsyncMessage("Identity:IDP:GenKeyPair", {
      oid: this._id,
      origin: this._origin,
    });
  },

  registerCertificate: function(aCertificate) {
    cpmm.sendAsyncMessage("Identity:IDP:RegisterCertificate", {
      oid: this._id,
      origin: this._origin,
      cert: aCertificate,
    });
  },

  raiseProvisioningFailure: function(aReason) {
    dump("nsDOMIdentity: raiseProvisioningFailure '" + aReason + "'\n");
    cpmm.sendAsyncMessage("Identity:IDP:ProvisioningFailure", {
      oid: this._id,
      origin: this._origin,
      reason: aReason,
    });
  },

  // IDP Authentication
  beginAuthentication: function(aCallback) {
    dump("DOM beginAuthentication: " + this._id + "\n");
    this._beginAuthenticationCallback = aCallback;
    cpmm.sendAsyncMessage("Identity:IDP:BeginAuthentication", {
      oid: this._id,
      origin: this._origin,
    });
  },

  completeAuthentication: function() {
    cpmm.sendAsyncMessage("Identity:IDP:CompleteAuthentication", {
      oid: this._id,
      origin: this._origin,
    });
  },

  raiseAuthenticationFailure: function(aReason) {
    cpmm.sendAsyncMessage("Identity:IDP:AuthenticationFailure", {
      oid: this._id,
      origin: this._origin,
      reason: aReason,
    });
  },

  // nsIFrameMessageListener
  receiveMessage: function(aMessage) {
    let msg = aMessage.json;
    // Is this message intended for this window?
    if (msg.oid != this._id) {
      return;
    }
    log("receiveMessage: " + aMessage.name + " : " + msg.oid);

    switch (aMessage.name) {
      case "Identity:RP:Watch:OnLogin":
        // Do we have a watcher?
        if (!this._watcher) {
          return;
        }

        if (this._watcher.onlogin) {
          this._watcher.onlogin(msg.assertion);
        }
        break;
      case "Identity:RP:Watch:OnLogout":
        // Do we have a watcher?
        if (!this._watcher) {
          return;
        }

        if (this._watcher.onlogout) {
          this._watcher.onlogout();
        }
        break;
      case "Identity:RP:Watch:OnReady":
        // Do we have a watcher?
        if (!this._watcher) {
          return;
        }

        if (this._watcher.onready) {
          this._watcher.onready();
        }
        break;
      case "Identity:RP:Request:OnCancel":
        // Do we have a watcher?
        if (!this._watcher) {
          return;
        }

        if (this._onCancelRequestCallback) {
          this._onCancelRequestCallback();
        }
        break;
      case "Identity:IDP:CallBeginProvisioningCallback":
        this._callBeginProvisioningCallback(msg);
        break;
      case "Identity:IDP:CallGenKeyPairCallback":
        this._callGenKeyPairCallback(msg);
        break;
      case "Identity:IDP:CallBeginAuthenticationCallback":
        this._callBeginAuthenticationCallback(msg);
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
    this._onCancelRequestCallback = null;
    this._beginProvisioningCallback = null;
    this._genKeyPairCallback = null;
    this._beginAuthenticationCallback = null;

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
    this._onCancelRequestCallback = null;
    this._beginProvisioningCallback = null;
    this._genKeyPairCallback = null;
    this._beginAuthenticationCallback = null;
    this._window = aWindow;
    this._origin = aWindow.document.nodePrincipal.origin;

    // Setup identifiers for current window.
    let util = aWindow.QueryInterface(Ci.nsIInterfaceRequestor).
      getInterface(Ci.nsIDOMWindowUtils);
    this._id = util.outerWindowID;
    this._innerWindowID = util.currentInnerWindowID;

    // Setup listeners for messages from parent process.
    this._messages = [
      "Identity:RP:Watch:OnLogin",
      "Identity:RP:Watch:OnLogout",
      "Identity:RP:Watch:OnReady",
      "Identity:RP:Request:OnCancel",
      "Identity:IDP:CallBeginProvisioningCallback",
      "Identity:IDP:CallGenKeyPairCallback",
      "Identity:IDP:CallBeginAuthenticationCallback",
    ];
    this._messages.forEach((function(msgName) {
      cpmm.addMessageListener(msgName, this);
    }).bind(this));

    // Setup observers so we can remove message listeners.
    Services.obs.addObserver(this, "inner-window-destroyed", false);
  },
  
  // Private.
  _callGenKeyPairCallback: function (message) {
    // create a pubkey object that works
    var chrome_pubkey = JSON.parse(message.publicKey);

    // bunch of stuff to create a proper object in window context
    function genPropDesc(value) {  
      return {  
        enumerable: true, configurable: true, writable: true, value: value  
      };
    }
      
    var propList = {};
    
    for (var k in chrome_pubkey) {
      propList[k] = genPropDesc(chrome_pubkey[k]);
    }
    
    var pubkey = Cu.createObjectIn(this._window);
    Object.defineProperties(pubkey, propList);  
    Cu.makeObjectPropsNormal(pubkey);

    // do the callback
    this._genKeyPairCallback.onSuccess(pubkey);
  },

  _callBeginProvisioningCallback: function(message) {
    let identity = message.identity;
    let certValidityDuration = message.certDuration;
    this._beginProvisioningCallback.onBeginProvisioning(identity, certValidityDuration);
  },

  _callBeginAuthenticationCallback: function(message) {
    let identity = message.identity;
    this._beginAuthenticationCallback.onBeginAuthentication(identity);
  },

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
