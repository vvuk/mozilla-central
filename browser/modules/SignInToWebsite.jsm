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

function log(msg) {
  dump("SignInToWebsiteUX: " + msg + "\n");
}

let SignInToWebsiteUX = {

  init: function SignInToWebsiteUX_init() {
    Services.obs.addObserver(this, "identity-request", false);
    Services.obs.addObserver(this, "identity-login", false);
    Services.obs.addObserver(this, "identity-auth", false);
  },

  uninit: function SignInToWebsiteUX_uninit() {
    Services.obs.removeObserver(this, "identity-request");
    Services.obs.removeObserver(this, "identity-login");
    Services.obs.removeObserver(this, "identity-auth");
  },

  observe: function SignInToWebsiteUX_observe(aSubject, aTopic, aData) {
    log("observe: received " + aTopic + " with " + aData + " for " + aSubject);
    switch(aTopic) {
      case "identity-request":
        this.requestLogin(aSubject);
        break;
      case "identity-login": // User chose a new or exiting identity after a request() call
        let requestID = aSubject.QueryInterface(Ci.nsIPropertyBag).getProperty("requestID");
        log("identity-login: requestID: " + requestID);
        // aData is the email address chosen (TODO: or null if cancelled?)
        let identity = aData; // String
        IdentityService.selectIdentity(requestID, identity);
        break;
      case "identity-auth":
        let authURI = aData;
        let context = aSubject;
        this._openAuthenticationUI(authURI, context);
        break;
    }
  },

  requestLogin: function(aOptions) {
    let windowID = aOptions.QueryInterface(Ci.nsIPropertyBag).getProperty("requestID");
    log("requestLogin for " + windowID);
    // XXX: as a hack just use the most recent window for now
    let someWindow = Services.wm.getMostRecentWindow('navigator:browser');
    let windowUtils = someWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                                .getInterface(Ci.nsIDOMWindowUtils);
    let content = windowUtils.getOuterWindowWithId(windowID);

    let win = null, browser = null;
    if (content) {
      browser = content.QueryInterface(Ci.nsIInterfaceRequestor)
                       .getInterface(Ci.nsIWebNavigation)
                       .QueryInterface(Ci.nsIDocShell).chromeEventHandler;
      win = browser.ownerDocument.defaultView;
      if (!win) {
        log("Could not get a window for requestLogin");
        return;
      }
    } else {
      log("no content");
    }

    let selectedBrowser = win.gBrowser.getBrowserForDocument(content.document);
    // Message is only used to pass the origin. signOut relies on this atm.
    let message = selectedBrowser.currentURI.prePath;
    let mainAction = {
      label: "Next",
      accessKey: "i", // TODO
      callback: function(notification) {
        // TODO: mostly handled in the binding already
        log("requestLogin callback fired");
      },
    };
    let options = {
      eventCallback: function(state) {
        log("requestLogin: doorhanger " + state);
      },
    };
    let secondaryActions = [];
    let reqNot = win.PopupNotifications.show(selectedBrowser, "identity-request", message,
                                              "identity-notification-icon", mainAction,
                                              secondaryActions, options);
    if (aOptions) {
      aOptions.QueryInterface(Ci.nsIPropertyBag);
      reqNot.requestID = aOptions.getProperty("requestID");
      log("requestLogin: requestID: " + reqNot.requestID);
    }
  },

  getIdentitiesForSite: function getIdentitiesForSite(aOrigin) {
    return IdentityService.getIdentitiesForSite(aOrigin);
  },

  signOut: function signOut(aNotification) {
    // TODO: handle signing out of other tabs for the same domain or let ID service notify those tabs.
    let origin = aNotification.message; // XXX: hack
    log("signOut for: " + origin);
    IdentityService.logout(origin);
  },

  // Private
  _openAuthenticationUI: function _openAuthenticationUI(aAuthURI, aContext) {
    // Open a tab/window with aAuthURI with an identifier (aID) attached so that the DOM APIs know this is an auth. window.
    let win = Services.wm.getMostRecentWindow('navigator:browser');
    let features = "chrome=false,width=640,height=480,centerscreen,location=yes,resizable=yes,scrollbars=yes,status=yes";
    log("aAuthURI: " + aAuthURI);
    let authWin = Services.ww.openWindow(win, aAuthURI, "", features, null);
    let windowID = authWin.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).outerWindowID;
    log("authWin outer id: " + windowID);

    IdentityService.setAuthenticationFlow(windowID, aContext);
  }
};

