/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var PageActions = {
  _handlers: null,

  init: function init() {
    if (this._handlers)
      return;

    this._handlers = [];
    document.getElementById("pageactions-container").addEventListener("click", this, true);

    this.register("pageaction-reset", this.updatePagePermissions, this);
    this.register("pageaction-password", this.updateForgetPassword, this);
#ifdef NS_PRINTING
    this.register("pageaction-saveas", this.updatePageSaveAs, this);
#endif
    this.register("pageaction-share", this.updateShare, this);

    CharsetMenu.init();
  },

  handleEvent: function handleEvent(aEvent) {
    switch (aEvent.type) {
      case "click":
        IdentityUI.hide();
        break;
    }
  },

  /**
   * @param aId id of a pageaction element
   * @param aCallback function that takes an element and returns true if it should be visible
   * @param aThisObj (optional) scope object for aCallback
   */
  register: function register(aId, aCallback, aThisObj) {
    this.init();
    this._handlers.push({id: aId, callback: aCallback, obj: aThisObj});
  },

  updateSiteMenu: function updateSiteMenu() {
    this.init();
    this._handlers.forEach(function(action) {
      let node = document.getElementById(action.id);
      if (node)
        node.hidden = !action.callback.call(action.obj, node);
    });
    this._updateAttributes();
  },

  get _loginManager() {
    delete this._loginManager;
    return this._loginManager = Cc["@mozilla.org/login-manager;1"].getService(Ci.nsILoginManager);
  },

  // Permissions we track in Page Actions
  _permissions: ["popup", "offline-app", "geolocation", "desktop-notification", "openWebappsManage"],

  _forEachPermissions: function _forEachPermissions(aHost, aCallback) {
    let pm = Services.perms;
    for (let i = 0; i < this._permissions.length; i++) {
      let type = this._permissions[i];
      if (!pm.testPermission(aHost, type))
        continue;

      let perms = pm.enumerator;
      while (perms.hasMoreElements()) {
        let permission = perms.getNext().QueryInterface(Ci.nsIPermission);
        if (permission.host == aHost.asciiHost && permission.type == type)
          aCallback(type);
      }
    }
  },

  updatePagePermissions: function updatePagePermissions(aNode) {
    let host = Browser.selectedBrowser.currentURI;
    let permissions = [];

    this._forEachPermissions(host, function(aType) {
      permissions.push("pageactions." + aType);
    });

    if (!this._loginManager.getLoginSavingEnabled(host.prePath)) {
      // If rememberSignons is false, then getLoginSavingEnabled returns false
      // for all pages, so we should just ignore it (Bug 601163).
      if (Services.prefs.getBoolPref("signon.rememberSignons"))
        permissions.push("pageactions.password");
    }

    let descriptions = permissions.map(function(s) Strings.browser.GetStringFromName(s));
    aNode.setAttribute("description", descriptions.join(", "));

    return (permissions.length > 0);
  },

  updateForgetPassword: function updateForgetPassword(aNode) {
    let host = Browser.selectedBrowser.currentURI;
    let logins = this._loginManager.findLogins({}, host.prePath, "", "");

    return logins.some(function(login) login.hostname == host.prePath);
  },

  forgetPassword: function forgetPassword(aEvent) {
    let host = Browser.selectedBrowser.currentURI;
    let lm = this._loginManager;

    lm.findLogins({}, host.prePath, "", "").forEach(function(login) {
      if (login.hostname == host.prePath)
        lm.removeLogin(login);
    });

    this.hideItem(aEvent.target);
    aEvent.stopPropagation(); // Don't hide the site menu.
  },

  clearPagePermissions: function clearPagePermissions(aEvent) {
    let pm = Services.perms;
    let host = Browser.selectedBrowser.currentURI;
    this._forEachPermissions(host, function(aType) {
      pm.remove(host.asciiHost, aType);

      // reset the 'remember' counter for permissions that support it
      if (["geolocation", "desktop-notification"].indexOf(aType) != -1)
        Services.contentPrefs.setPref(host.asciiHost, aType + ".request.remember", 0);
    });

    let lm = this._loginManager;
    if (!lm.getLoginSavingEnabled(host.prePath))
      lm.setLoginSavingEnabled(host.prePath, true);

    this.hideItem(aEvent.target);
    aEvent.stopPropagation(); // Don't hide the site menu.
  },

  pinSite : function pinCurrentSite() {
    if (Browser.selectedBrowser.currentURI.spec.length == 0) {
      return;
    }
    Browser.pinSite();
  },

  unpinSite : function unpinCurrentSite() {
    if (Browser.selectedBrowser.currentURI.spec.length == 0) {
      return;
    }
    Browser.unpinSite();
  },

  updatePageSaveAs: function updatePageSaveAs(aNode) {
    // Check for local XUL content
    let contentWindow = Browser.selectedBrowser.contentWindow;
    return !(contentWindow && contentWindow.document instanceof XULDocument);
  },

  updateShare: function updateShare(aNode) {
    return Util.isShareableScheme(Browser.selectedBrowser.currentURI.scheme);
  },

  hideItem: function hideItem(aNode) {
    aNode.hidden = true;
    this._updateAttributes();
  },

  _updateAttributes: function _updateAttributes() {
    let container = document.getElementById("pageactions-container");
    let visibleNodes = container.querySelectorAll("pageaction:not([hidden=true])");
    let visibleCount = visibleNodes.length;
    if (visibleCount == 0)
      return;

    for (let i = 0; i < visibleCount; i++)
      visibleNodes[i].classList.remove("odd-last-child");

    visibleNodes[visibleCount - 1].classList.add("last-child");
    if (visibleCount % 2)
      visibleNodes[visibleCount - 1].classList.add("odd");
  }
};
