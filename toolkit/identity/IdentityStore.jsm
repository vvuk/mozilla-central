/* -*- Mode: js2; js2-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Cu = Components.utils;
const Ci = Components.interfaces;
const Cc = Components.classes;
const Cr = Components.results;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const EXPORTED_SYMBOLS = ["IdentityStore", "IDLog"];

const PREF_DEBUG = "toolkit.identity.debug";

/**
 * IDLog() - utility function to print a list of arbitrary things
 * Depends on IdentityStore (bottom of this file) for _debug.
 *
 * Enable with about:config pref toolkit.identity.debug
 */
function IDLog(aPrefix, ...args) {
  if (!IdentityStore._debug) {
    return;
  }
  aPrefix = aPrefix || "";
  let strings = [];

  args.forEach(function(arg) {
    if (typeof arg === 'string') {
      strings.push(arg);
    } else if (typeof arg === 'undefined') {
      strings.push('undefined');
    } else if (arg === null) {
      strings.push('null');
    } else {
      try {
        strings.push(JSON.stringify(arg, null, 2));
      } catch(err) {
        strings.push("<<something>>");
      }
    }
  });
  let output = 'Identity ' + aPrefix + ': ' + strings.join(' ') + '\n';
  dump(output);

  // Additionally, make the output visible in the Error Console
  Services.console.logStringMessage(output);
};


// the data store for IDService
// written as a separate thing so it can easily be mocked
function IDServiceStore() {
  Services.prefs.addObserver(PREF_DEBUG, this, false);
  this._debug = Services.prefs.getBoolPref(PREF_DEBUG);

  this.init();
}

IDServiceStore.prototype._identities = null;

IDServiceStore.prototype._loginStates = null;

// Note: eventually these methods may be async, but we haven no need for this
// for now, since we're not storing to disk.
IDServiceStore.prototype = {
  addIdentity: function addIdentity(aEmail, aKeyPair, aCert) {
    this._identities[aEmail] = {keyPair: aKeyPair, cert: aCert};
  },
  fetchIdentity: function fetchIdentity(aEmail) {
    return aEmail in this._identities ? this._identities[aEmail] : null;
  },
  removeIdentity: function removeIdentity(aEmail) {
    let data = this._identities[aEmail];
    delete this._identities[aEmail];
    return data;
  },
  getIdentities: function getIdentities() {
    // XXX - should clone?
    return this._identities;
  },
  clearCert: function clearCert(aEmail) {
    // XXX - should remove key from store?
    this._identities[aEmail].cert = null;
    this._identities[aEmail].keyPair = null;
  },

  /**
   * set the login state for a given origin
   *
   * @param aOrigin
   *        (string) a web origin
   *
   * @param aState
   *        (boolean) whether or not the user is logged in
   *
   * @param aEmail
   *        (email) the email address the user is logged in with,
   *                or, if not logged in, the default email for that origin.
   */
  setLoginState: function setLoginState(aOrigin, aState, aEmail) {
    if (aState && !aEmail) {
      throw "isLoggedIn cannot be set to true without an email";
    }
    return this._loginStates[aOrigin] = {isLoggedIn: aState, email: aEmail};
  },
  getLoginState: function getLoginState(aOrigin) {
    return aOrigin in this._loginStates ? this._loginStates[aOrigin] : null;
  },
  clearLoginState: function clearLoginState(aOrigin) {
    delete this._loginStates[aOrigin];
  },

  init: function init() {
    // _identities associates emails with keypairs and certificates
    this._identities = {};

    // _loginStates associates. remote origins with a login status and
    // the email the user has chosen as his or her identity when logging
    // into that origin.
    this._loginStates = {};
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports, Ci.nsIObserver]),

  observe: function observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "quit-application-granted":
        Services.obs.removeObserver(this, "quit-application-granted");
        Services.prefs.removeObserver(PREF_DEBUG, this);
        this.init();
        break;
      case "nsPref:changed":
        this._debug = Services.prefs.getBoolPref(PREF_DEBUG);
        break;
    }
  },
};

let IdentityStore = new IDServiceStore();
