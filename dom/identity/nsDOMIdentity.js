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

  watch: function nsDOMIdentity_watch(aOptions) {
    log("Called watch for ID " + this._id + " with loggedInEmail " + aOptions.loggedInEmail);

    if (typeof(aOptions) !== "object") {
      throw "options argument to watch is required";
    }

    // Check for required callbacks
    let requiredCallbacks = ["onlogin", "onlogout"];
    for (let cbName of requiredCallbacks) {
      if (typeof(aOptions[cbName]) !== "function") {
        throw cbName + " callback is required.";
      }
    }

    // Optional callback "onready"
    if (aOptions["onready"] && typeof(aOptions['onready']) !== "function") {
      throw "onready must be a function";
    }

    // loggedInEmail - TODO: check email format?
    let emailType = typeof(aOptions["loggedInEmail"]);
    if (aOptions["loggedInEmail"] && emailType !== "string") {
      throw "loggedInEmail must be a String or null";
    }

    // Latest watch call wins in case site makes multiple calls.
    this._watcher = aOptions;

    let message = this.DOMIdentityMessage();
    message.loggedInEmail = aOptions.loggedInEmail, // Could be undefined or null

    cpmm.sendAsyncMessage("Identity:RP:Watch", message);
  },

  request: function nsDOMIdentity_request(aOptions) {
    // TODO: "This function must be invoked from within a click handler."
    // This is doable once nsEventStateManager::IsHandlingUserInput is scriptable.

    // Has the caller called watch() before this?
    if (!this._watcher) {
      throw new Error("navigator.id.request called before navigator.id.watch");
    }

    if (aOptions) {
      // requiredEmail - TODO: check email format?
      let emailType = typeof(aOptions["requiredEmail"]);
      if (aOptions["requiredEmail"] && emailType !== "string") {
        throw "requiredEmail must be a String or null";
      }

      // Optional string properties
      let optionalStringProps = ["privacyURL", "tosURL"];
      for (let propName of optionalStringProps) {
        if (aOptions[propName] && typeof(aOptions[propName]) !== "string") {
          throw propName + " must be a string representing a URL.";
        }
      }

      if (aOptions["oncancel"] && typeof(aOptions["oncancel"]) !== "function") {
        throw "oncancel is not a function";
      }
    }

    let message = this.DOMIdentityMessage();

    if (aOptions) {
      // Store optional cancel callback for later.
      this._onCancelRequestCallback = aOptions.oncancel;

      message.requiredEmail = aOptions.requiredEmail;
      message.privacyURL = aOptions.privacyURL;
      message.tosURL = aOptions.tosURL;
    }

    cpmm.sendAsyncMessage("Identity:RP:Request", message);
  },

  logout: function nsDOMIdentity_logout() {
    if (!this._watcher) {
      throw new Error("navigator.id.logout called before navigator.id.watch");
    }

    let message = this.DOMIdentityMessage();
    cpmm.sendAsyncMessage("Identity:RP:Logout", message);
  },

  /**
   *  Identity Provider (IDP) APIs
   */

  beginProvisioning: function nsDOMIdentity_beginProvisioning(aCallback) {
    dump("DOM beginProvisioning: " + this._id + "\n");
    this._beginProvisioningCallback = aCallback;
    cpmm.sendAsyncMessage("Identity:IDP:BeginProvisioning", this.DOMIdentityMessage());
  },

  genKeyPair: function nsDOMIdentity_genKeyPair(aCallback) {
    dump("DOM genKeyPair\n");
    this._genKeyPairCallback = aCallback;
    cpmm.sendAsyncMessage("Identity:IDP:GenKeyPair", this.DOMIdentityMessage());
  },

  registerCertificate: function nsDOMIdentity_registerCertificate(aCertificate) {
dump("*********** registerCertificate:");
dump(aCertificate);
    let message = this.DOMIdentityMessage();
    message.cert = aCertificate;
    cpmm.sendAsyncMessage("Identity:IDP:RegisterCertificate", message);
  },

  raiseProvisioningFailure: function nsDOMIdentity_raiseProvisioningFailure(aReason) {
    dump("nsDOMIdentity: raiseProvisioningFailure '" + aReason + "'\n");
    let message = this.DOMIdentityMessage();
    message.reason = aReason;
    cpmm.sendAsyncMessage("Identity:IDP:ProvisioningFailure", message);
  },

  // IDP Authentication
  beginAuthentication: function nsDOMIdentity_beginAuthentication(aCallback) {
    dump("DOM beginAuthentication: " + this._id + "\n");
    this._beginAuthenticationCallback = aCallback;
    cpmm.sendAsyncMessage("Identity:IDP:BeginAuthentication",
                          this.DOMIdentityMessage());
  },

  completeAuthentication: function nsDOMIdentity_completeAuthentication() {
    cpmm.sendAsyncMessage("Identity:IDP:CompleteAuthentication",
                          this.DOMIdentityMessage());
  },

  raiseAuthenticationFailure: function nsDOMIdentity_raiseAuthenticationFailure(aReason) {
    let message = this.DOMIdentityMessage();
    message.reason = aReason;
    cpmm.sendAsyncMessage("Identity:IDP:AuthenticationFailure", message);
  },

  // nsIFrameMessageListener
  receiveMessage: function nsDOMIdentity_receiveMessage(aMessage) {
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
  observe: function nsDOMIdentity_observe(aSubject, aTopic, aData) {
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
  init: function nsDOMIdentity_init(aWindow) {
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
  _callGenKeyPairCallback: function nsDOMIdentity__callGenKeyPairCallback(message) {
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

  _callBeginProvisioningCallback:
      function nsDOMIdentity__callBeginProvisioningCallback(message) {
    let identity = message.identity;
    let certValidityDuration = message.certDuration;
    this._beginProvisioningCallback.onBeginProvisioning(identity, certValidityDuration);
  },

  _callBeginAuthenticationCallback:
      function nsDOMIdentity__callBeginAuthenticationCallback(message) {
    let identity = message.identity;
    this._beginAuthenticationCallback.onBeginAuthentication(identity);
  },

  /**
   * Helper to create messages to send using a message manager
   */
  DOMIdentityMessage: function DOMIdentityMessage() {
    return {
      oid: this._id,
      origin: this._origin,
    };
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
