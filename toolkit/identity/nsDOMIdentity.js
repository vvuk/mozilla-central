/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyGetter(this, "cpmm", function() {
  return Cc["@mozilla.org/childprocessmessagemanager;1"].
    getService(Ci.nsIFrameMessageManager);
});

function nsDOMIdentity() {
}
nsDOMIdentity.prototype = {

  // nsIDOMIdentity
  watch: function(params) {
    // Latest watch call wins in case site makes multiple calls.
    this._watcher = params;

    let message = {
      oid: this._id,
      loggedIn: params.loggedInEmail, // Could be undefined or null
      from: this._window.location.href
    };
    cpmm.sendAsyncMessage("Identity:Watch", message);
  },

  request: function() {
    // Has the caller called watch() before this?
    if (!this._watcher) {
      throw new Error("navigator.id.request called before navigator.id.watch");
    }

    cpmm.sendAsyncMessage("Identity:Request", {
      oid: this._id,
      from: this._window.location.href
    });
  },

  // nsIFrameMessageListener
  receiveMessage: function(aMessage) {
    let msg = aMessage.json;

    // Is this message intended for this window?
    if (msg.oid != this._id) {
      return;
    }

    // Do we have a watcher?
    let params = this._watcher;
    if (!params) {
      return;
    }

    switch (aMessage.name) {
      case "Identity:Watch:OnLogin":
        if (params.onlogin) {
          params.onlogin(msg.assertion);
        }
        break;
      case "Identity:Watch:OnLogout":
        break;
      case "Identity:Watch:OnReady":
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
    this._messages.forEach((function(msgName) {
      cpmm.removeMessageListener(msgName, this);
    }).bind(this));
  },

  // nsIDOMGlobalPropertyInitializer
  init: function(aWindow) {
    // Store window and origin URI.
    this._watcher = null;
    this._window = aWindow;
    this._origin = aWindow.document.nodePrincipal.uri;

    // Setup identifiers for current window.
    let util = aWindow.QueryInterface(Ci.nsIInterfaceRequestor).
      getInterface(Ci.nsIDOMWindowUtils);
    this._id = this._getRandomId();
    this._innerWindowID = util.currentInnerWindowID;

    // Setup listeners for messages from child process.
    this._messages = [
      "Identity:Watch:OnLogin",
      "Identity:Watch:OnLogout",
      "Identity:Watch:OnReady"
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
