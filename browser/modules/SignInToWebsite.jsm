/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["SignInToWebsiteUX"];

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/ProfileIdentityUtils.jsm");
Cu.import("resource://gre/modules/identity/Identity.jsm");

function log(msg) {
  dump("SignInToWebsiteUX: " + msg + "\n");
}

let SignInToWebsiteUX = {

  init: function SignInToWebsiteUX_init() {
    Services.obs.addObserver(this, "identity-request", false);
    Services.obs.addObserver(this, "identity-auth", false);
    Services.obs.addObserver(this, "identity-auth-complete", false);
    Services.obs.addObserver(this, "identity-login-state-changed", false);

    /* Sample data */
    [
      "foo@eyedee.me",
      "benadida@eyedee.me",
      "joe@mockmyid.com",
      "matt@browserid.linuxsecured.net",
    ].forEach(function(identity) {
      IdentityService._store.addIdentity(identity, null, null);
    });
  },

  uninit: function SignInToWebsiteUX_uninit() {
    Services.obs.removeObserver(this, "identity-request");
    Services.obs.removeObserver(this, "identity-auth");
    Services.obs.removeObserver(this, "identity-auth-complete");
    Services.obs.removeObserver(this, "identity-login-state-changed");
  },

  observe: function SignInToWebsiteUX_observe(aSubject, aTopic, aData) {
    log("observe: received " + aTopic + " with " + aData + " for " + aSubject);
    switch(aTopic) {
      case "identity-request":
        this.requestLogin(aSubject);
        break;
      case "identity-auth":
        this._openAuthenticationUI(aData, aSubject);
        break;
      case "identity-auth-complete":
        this._closeAuthenticationUI(aData);
        break;
      case "identity-login-state-changed":
        if (aData) { // if there is en email address
          this._removeRequestUI(aSubject);
          this._showLoggedInUI(aData, aSubject);
        } else {
          this._removeLoggedInUI(aSubject);
        }
        break;
    }
  },

  /**
   * The website is requesting login so the user must choose an identity to use.
   */
  requestLogin: function SignInToWebsiteUX_requestLogin(aOptions) {
    let windowID = aOptions.QueryInterface(Ci.nsIPropertyBag).getProperty("rpId");
    log("requestLogin for " + windowID);
    let [win, browserEl] = this._getUIForID(windowID);

    // Message is only used to pass the origin. signOut relies on this atm.
    let message = browserEl.currentURI.prePath;
    let mainAction = {
      label: win.gNavigatorBundle.getString("identity.next.label"),
      accessKey: win.gNavigatorBundle.getString("identity.next.accessKey"),
      callback: function() {
        let requestNot = win.PopupNotifications.getNotification("identity-request", browserEl);
        // TODO: mostly handled in the binding already
        log("requestLogin callback fired for " + requestNot.options.identity.rpId);
      },
    };
    let options = {
      eventCallback: function(state) {
        //log("requestLogin: doorhanger " + state);
      },
      identity: {
        origin: browserEl.currentURI.prePath,
      },
    };
    let secondaryActions = [];

    // add some extra properties to the notification to store some identity-related state
    let requestOptions = aOptions.QueryInterface(Ci.nsIPropertyBag).enumerator;
    while (requestOptions.hasMoreElements()) {
      let opt = requestOptions.getNext().QueryInterface(Ci.nsIProperty);
      options.identity[opt.name] = opt.value;
    }
    log("requestLogin: rpId: " + options.identity.rpId);

    let reqNot = win.PopupNotifications.show(browserEl, "identity-request", message,
                                              "identity-notification-icon", mainAction,
                                              secondaryActions, options);
  },

  /**
   * Get the list of possible identities to login to the given origin.
   */
  getIdentitiesForSite: function SignInToWebsiteUX_getIdentitiesForSite(aOrigin) {
    return IdentityService.getIdentitiesForSite(aOrigin);
  },

  /**
   * User chose a new or existing identity from the doorhanger after a request() call
   */
  selectIdentity: function SignInToWebsiteUX_selectIdentity(aRpId, aIdentity) {
    log("selectIdentity: rpId: " + aRpId + " identity: " + aIdentity);
    IdentityService.selectIdentity(aRpId, aIdentity);
  },

  /**
   * User clicked sign out on the given notification.  Notify the identity service.
   */
  signOut: function signOut(aNotification) {
    let origin = aNotification.options.identity.origin;
    log("signOut for: " + origin);
    IdentityService.logout(origin);
  },

  // Private

  /**
   * Return the window and <browser> for the given outer window ID.
   */
  _getUIForID: function(aWindowID) {
    // XXX: as a hack just use the most recent window for now
    let someWindow = Services.wm.getMostRecentWindow('navigator:browser');
    let windowUtils = someWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                                .getInterface(Ci.nsIDOMWindowUtils);
    let content = windowUtils.getOuterWindowWithId(aWindowID);

    if (content) {
      let browser = content.QueryInterface(Ci.nsIInterfaceRequestor)
                           .getInterface(Ci.nsIWebNavigation)
                           .QueryInterface(Ci.nsIDocShell).chromeEventHandler;
      let win = browser.ownerDocument.defaultView;
      return [win, browser];
    } else {
      log("no content");
    }
    return [null, null];
  },

  /**
   * Open UI with a content frame displaying aAuthURI so that the user can authenticate with their
   * IDP.  Then tell Identity.jsm the identifier for the window so that it knows that the DOM API
   * calls are for this authentication flow.
   */
  _openAuthenticationUI: function _openAuthenticationUI(aAuthURI, aContext) {
    // Open a tab/window with aAuthURI with an identifier (aID) attached so that the DOM APIs know this is an auth. window.
    let win = Services.wm.getMostRecentWindow('navigator:browser');
    let features = "chrome=false,width=640,height=480,centerscreen,location=yes,resizable=yes,scrollbars=yes,status=yes";
    log("aAuthURI: " + aAuthURI);
    let authWin = Services.ww.openWindow(win, "about:blank", "", features, null);
    let windowID = authWin.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).outerWindowID;
    log("authWin outer id: " + windowID);

    let provId = aContext.QueryInterface(Ci.nsIPropertyBag).getProperty("provId");
    // Tell the ID service about the id before loading the url
    IdentityService.setAuthenticationFlow(windowID, provId);

    authWin.location = aAuthURI;
  },

  _closeAuthenticationUI: function _closeAuthenticationUI(aAuthId) {
    let [win, browserEl] = this._getUIForID(aAuthId);
    if (win)
      win.close();
  },

  /**
   * Show a doorhanger indicating the currently logged-in user.
   */
  _showLoggedInUI: function _showLoggedInUI(aIdentity, aContext) {
    let windowID = aContext.QueryInterface(Ci.nsIPropertyBag).getProperty("rpId");
    log("_showLoggedInUI for " + windowID);
    let [win, browserEl] = this._getUIForID(windowID);

    let message = win.gNavigatorBundle.getFormattedString("identity.loggedIn.description",
                                                          [aIdentity]);
    let mainAction = {
      label: win.gNavigatorBundle.getString("identity.loggedIn.signOut.label"),
      accessKey: win.gNavigatorBundle.getString("identity.loggedIn.signOut.accessKey"),
      callback: function(notification) {
        log("sign out callback fired");
        IdentityService.logout(windowID);
      },
    };
    let secondaryActions = [];
    let options = {
      dismissed: true,
    };
    let loggedInNot = win.PopupNotifications.show(browserEl, "identity-logged-in", message,
                                                  "identity-notification-icon", mainAction,
                                                  secondaryActions, options);
    loggedInNot.rpId = windowID;
  },

  /**
   * Remove the doorhanger indicating the currently logged-in user.
   */
  _removeLoggedInUI: function _removeLoggedInUI(aContext) {
    let windowID = aContext.QueryInterface(Ci.nsIPropertyBag).getProperty("rpId");
    log("_removeLoggedInUI for " + windowID);
    let [win, browserEl] = this._getUIForID(windowID);

    let loggedInNot = win.PopupNotifications.getNotification("identity-logged-in", browserEl);
    if (loggedInNot)
      win.PopupNotifications.remove(loggedInNot);
  },

  /**
   * Remove the doorhanger indicating the currently logged-in user.
   */
  _removeRequestUI: function _removeRequestUI(aContext) {
    let windowID = aContext.QueryInterface(Ci.nsIPropertyBag).getProperty("rpId");
    log("_removeRequestUI for " + windowID);
    let [win, browserEl] = this._getUIForID(windowID);

    let requestNot = win.PopupNotifications.getNotification("identity-request", browserEl);
    if (requestNot)
      win.PopupNotifications.remove(requestNot);
  },

};

