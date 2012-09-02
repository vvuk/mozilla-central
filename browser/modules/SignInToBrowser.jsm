/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["SignInToBrowser"];

const DEBUG_EMAILS = ["user@example.com", "user2@example.com"];
const Cu = Components.utils;

Components.utils.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/ProfileIdentityUtils.jsm");

let userEmail;
let signedIn = false;
let checkedProfile = false;

let SignInToBrowser = {

  get signedIn() {
    if (!checkedProfile) {
      try {
        let profile = ProfileIdentityUtils._currentToolkitProfile;
        if (ProfileIdentityUtils._isProfileAssociated(profile)) {
          userEmail = profile.name;
          signedIn = true;
        }
      } catch (e) { Cu.reportError(e); /* not signed in to anything */ }
      //checkedProfile = true; // TODO: disabled memoization
    }
    return signedIn;
  },

  get userInfo() {
    if (!this.signedIn)
      return {};

    return { email: userEmail };
  },

  register: function register(aUserInfo) {
    ProfileIdentityUtils.associateCurrentProfile(aUserInfo.email);
    return this.signIn(aUserInfo);
  },

  signIn: function signIn(aUserInfo, aCallback) {
    // TODO: checks?
    if (this.accounts.indexOf(aUserInfo.email) != -1) {
      userEmail = aUserInfo.email;
      signedIn = true;
      Services.obs.notifyObservers(null, "sitb-signin", userEmail);
      ProfileIdentityUtils.launchProfile(userEmail);
      return true;
    }

    return false;
    // TODO: async
  },

  get localIdentities() {
    return ProfileIdentityUtils.localIdentities;
  },

  signOut: function signOut() {
    userEmail = null;
    signedIn = false;
    ProfileIdentityUtils.logout();
  },

  // aCallback(errors, exists)
  isUserEmail: function isUserEmail(aEmail, aCallback) {
    return this.accounts.indexOf(aEmail) != -1;
    // TODO: async
    // setTimeout(function() {
    //   aCallback(null, aEmail == DEBUG_EMAIL);
    // }, 1000);
  },

  get accounts() {
    let accounts = ProfileIdentityUtils.localIdentities;
    for (let extra of DEBUG_EMAILS)
      accounts.push(extra);
    return accounts;
  },

};

let SignInToBrowserUX = {
  

};