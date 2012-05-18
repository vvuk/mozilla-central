/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["SignInToWebsiteUX"];

const Cu = Components.utils;
const Ci = Components.interfaces;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/ProfileIdentityUtils.jsm");


let SignInToWebsiteUX = {

  init: function SignInToWebsiteUX_init() {
    Services.obs.addObserver(this, "identity-foo", false); // TODO
  },

  uninit: function SignInToWebsiteUX_uninit() {
    Services.obs.removeObserver(this, "identity-foo");
  },

  observe: function SignInToWebsiteUX_observe(aSubject, aTopic, aData) {
    switch(aTopic) {
      case "identity-foo":
        this.requestLogin();
        break;
    }
  },

  requestLogin: function(aBrowser) {
    // XXX: as a hack just use the most recent window for now
    let win = Services.wm.getMostRecentWindow('navigator:browser');
    let browser = win.gBrowser;
    let message = "Login below:";
    let mainAction = {
      label: "replace this button",
      accessKey: "i", // TODO
      callback: function(notification) {
        // TODO
      },
    };
    let options = {
      eventCallback: function(state) {
        dump(state + "\n");
      },
    };
    let secondaryActions = [];
    win.PopupNotifications.show(browser.selectedBrowser, "identity-request", message, "identity-notification-icon", mainAction,
                                secondaryActions, options);
  },

};

