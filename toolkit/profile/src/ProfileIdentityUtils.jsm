/* -*- Mode: js; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80 filetype=javascript: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = [
  "ProfileIdentityUtils",
];

/**
 * Handles profile switching based on identity.
 */

////////////////////////////////////////////////////////////////////////////////
//// Globals

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "NetUtil",
                                  "resource://gre/modules/NetUtil.jsm");

XPCOMUtils.defineLazyServiceGetter(this, "gProfileService",
                                   "@mozilla.org/toolkit/profile-service;1",
                                   "nsIToolkitProfileService");

const DEFAULT_PROFILE_NAME = "Default";

////////////////////////////////////////////////////////////////////////////////
//// ProfileIdentityUtils

/**
 * Handles profile switching based on identity.
 */
const ProfileIdentityUtils = {
  _isProfileAssociated: function(aProfile) {
    // TODO: Add property to nsIToolkitProfile.
    return aProfile.name.indexOf("@") >= 0;
  },

  /**
   * Returns an array of identities (e-mail addresses) that already have an
   * associated local profile.
   */
  get localIdentities() {
    let identities = [];
    let profiles = gProfileService.profiles;
    while (profiles.hasMoreElements()) {
      let profile = profiles.getNext().QueryInterface(Ci.nsIToolkitProfile);

      if (this._isProfileAssociated(profile)) {
        identities.push(profile.name);
      }
    }
    return identities;
  },

  /**
   * @return boolean whether the profile could be launched
   */
  launchProfile: function(aIdentity) {
      let profile = this._getProfileForIdentity(aIdentity);
      if (profile) {
          if (profile == this._currentToolkitProfile)
              return false;
          // TODO: check that user is logged-in to destination profile
          // if not, prompt for authentication
          this._restartInProfile(profile);
      } else {
          profile = this._createProfileForIdentity(aIdentity);
          this._restartInProfile(profile);
      }
      return true;
  },

  // TODO: could use GetProfileByName?
  _getProfileForIdentity: function(aIdentity) {
    let profileList = gProfileService.profiles;
    while (profileList.hasMoreElements()) {
      let profile = profileList.getNext().QueryInterface(Ci.nsIToolkitProfile);
      if (profile.name.toLowerCase() == aIdentity.toLowerCase()) {
        return profile;
      }
    }
    return null;
  },

  get _currentToolkitProfile() { // TODO: memoize
      let profD = Services.dirsvc.get("ProfD", Ci.nsIFile);
      let profileList = gProfileService.profiles;
      while (profileList.hasMoreElements()) {
          let profile = profileList.getNext().QueryInterface(Ci.nsIToolkitProfile);
          if (profile.rootDir.path == profD.path) // TODO: Not the right way
              return profile;
      }
      throw new Error("Not in nsIToolkitProfileService: " + profD.path);
  },

  _createProfile: function() {
    // TODO: create default prefs.js with better first-run. (firstrun page, know your rights?)
    let profile = gProfileService.getProfileByName(DEFAULT_PROFILE_NAME);
    if (!profile) {
      gProfileService.createProfile(null, null, DEFAULT_PROFILE_NAME);
      gProfileService.flush();
      return profile;
    }
    for (let i = 1; i < 100; i++) { // TODO: const for max default profiles
      // Look for a profile name that doesn't already exist
      let name = DEFAULT_PROFILE_NAME + " " + i;
      profile = gProfileService.getProfileByName(name);
      if (profile)
        continue;
      gProfileService.createProfile(null, null, name);
      break;
    }
    gProfileService.flush();
    return profile;
  }

  // TODO: may not need this since we may only allow associating an existing profile with an identity
  _createProfileForIdentity: function(aIdentity) {
    let profile = gProfileService.createProfile(null, null, aIdentity);
    gProfileService.flush();
    return profile;
  },

  associateCurrentProfile: function(aUsername) {
    let profile = this._currentToolkitProfile;
    profile.name = aUsername; // TODO: cleanse? // TODO: can throw
    let env = Cc["@mozilla.org/process/environment;1"].
              getService(Ci.nsIEnvironment); // TODO: Services.env?
    env.set("XRE_PROFILE_NAME", profile.name);

    gProfileService.flush();
  },

  _restartInProfile: function(aProfile) {
    // Check if the profile is already in-use (locked)
    try {
        let profileLock = aProfile.lock({ value: null });
    } catch (ex) {
      Cu.reportError(aProfile.name);
      Cu.reportError(ex);
      // TODO: focus existing other browser window somehow if it's already in use
      let appStartup = Cc["@mozilla.org/toolkit/app-startup;1"].getService(Ci.nsIAppStartup);
      appStartup.quit(Ci.nsIAppStartup.eAttemptQuit);
    }

    let env = Cc["@mozilla.org/process/environment;1"].
              getService(Ci.nsIEnvironment); // TODO: Services.env?
    env.set("XRE_PROFILE_PATH", aProfile.rootDir.path); // TODO: nativePath?
    env.set("XRE_PROFILE_LOCAL_PATH", aProfile.localDir.path);
    env.set("XRE_PROFILE_NAME", aProfile.name);

    gProfileService.selectedProfile = aProfile;
    gProfileService.flush();

    let appStartup = Cc["@mozilla.org/toolkit/app-startup;1"].getService(Ci.nsIAppStartup);
    appStartup.quit(Ci.nsIAppStartup.eRestart | Ci.nsIAppStartup.eAttemptQuit);
  },

  // Switch to an unassociated profile
  logout: function(aDelete) {
    // TODO: delete on shutdown if aDelete
    let selected = gProfileService.selectedProfile;
    if (selected && !this._isProfileAssociated(selected)) {
      // selcted profile is not associated so use it
      this._restartInProfile(selected);
      return;
    }

    let profileList = gProfileService.profiles;
    while (profileList.hasMoreElements()) {
      let profile = profileList.getNext().QueryInterface(Ci.nsIToolkitProfile);
      if (!this._isProfileAssociated(profile)) {
        // HACK: assuming first is best
        this._restartInProfile(profile);
      }
    }
  },

};

/**
 * ###
 */
XPCOMUtils.defineLazyGetter(ProfileIdentityUtils, "test", function () {
  return "test";
});
