/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["SignInToWebsiteUX"];

const Cu = Components.utils;
const Ci = Components.interfaces;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/ProfileIdentityUtils.jsm");
Cu.import("resource://gre/modules/identity/Identity.jsm");

let SignInToWebsiteUX = {

  init: function SignInToWebsiteUX_init() {
    Services.obs.addObserver(this, "identity-request", false); // TODO
    Services.obs.addObserver(this, "identity-login", false); // TODO
  },

  uninit: function SignInToWebsiteUX_uninit() {
    Services.obs.removeObserver(this, "identity-request");
    Services.obs.removeObserver(this, "identity-login");
  },

  observe: function SignInToWebsiteUX_observe(aSubject, aTopic, aData) {
    switch(aTopic) {
      case "identity-request":
        // XXX: as a hack just use the most recent window for now
        let win = Services.wm.getMostRecentWindow('navigator:browser');
        this.requestLogin(win);
        break;
      case "identity-login":
        dump("Received identity-login: "+aData+"\n");
        break;
    }
  },

  requestLogin: function(aWin) {
    let browser = aWin.gBrowser;
    let selectedBrowser = browser.selectedBrowser;
    // Message is only used to pass the origin. signOut relies on this atm.
    let message = selectedBrowser.currentURI.prePath;
    let mainAction = {
      label: "Next",
      accessKey: "i", // TODO
      callback: function(notification) {
        // TODO: mostly handled in the binding already
        dump("requestLogin callback fired\n");
      },
    };
    let options = {
      eventCallback: function(state) {
        dump(state + "\n");
      },
    };
    let secondaryActions = [];
    aWin.PopupNotifications.show(selectedBrowser, "identity-request", message, "identity-notification-icon", mainAction,
                                secondaryActions, options);
  },

  getIdentitiesForSite: function getIdentitiesForSite(aOrigin) {
    return IdentityService.getIdentitiesForSite(aOrigin);
  },

  signOut: function signOut(aNotification) {
    // TODO: handle signing out of other tabs for the same domain or let ID service notify those tabs.
    let origin = aNotification.message; // XXX: hack
    dump("signOut for: " + origin + "\n");
    IdentityService.logout(origin);
  },
};

