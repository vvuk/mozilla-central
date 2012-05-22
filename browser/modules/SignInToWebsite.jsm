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
    let message = "Login below:";
    let mainAction = {
      label: "Next",
      accessKey: "i", // TODO
      callback: function(notification) {
        // TODO
        dump("requestLogin callback fired\n");
      },
    };
    let options = {
      eventCallback: function(state) {
        dump(state + "\n");
      },
    };
    let secondaryActions = [];
    aWin.PopupNotifications.show(browser.selectedBrowser, "identity-request", message, "identity-notification-icon", mainAction,
                                secondaryActions, options);
  },

};

