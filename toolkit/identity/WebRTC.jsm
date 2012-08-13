/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["selectIdentity", "verifyIdentity"];
const DEFAULT_IDP = "browserid.org";
const DEFAULT_PROTOCOL = "persona";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/identity/jwcrypto.jsm");
Cu.import("resource://gre/modules/identity/Identity.jsm");
Cu.import("resource://gre/modules/identity/RelyingParty.jsm");

Cu.import("resource://services-common/utils.js");

function AuthModule(aPeerConnectionId, aIdentity) {
  // XXX: How do we know aIDP.idp aIDP.origin are valid?
  this.peerConnectionId = aPeerConnectionId;
  this.identity = aIdentity.identity;
  this.idp = aIdentity.idp || DEFAULT_IDP;
  this.protocol = aIdentity.protocol || DEFAULT_PROTOCOL;
  return this;
}

AuthModule.prototype = {
  sign: function(aOrigin, aMessage, aCallback) {
    // XXX need to deal with the case where the cert has expired between
    // selectIdentity and now
    RelyingParty._generateAssertion(
      aOrigin, this.identity, {message:aMessage}, aCallback
    );
  },
  verify: function(aAssertion, aCallback) {
    // XXX: Use verifyIdentity for now until we get real crypto verification
  }
};

/**
 * Verify an identity based on a signature provided by a WEBRTC PeerConnection
 *
 * @param aSignature        The signature provided by the remote peer.
 *        (string)
 *
 * @param aCallback         Called after the signature has been verified. First
 *        (function)        argument will be a string error (if any) or null.
 *                          If no error, second argument will be a dictionary
 *                          describing the verified identity (such as the one
 *                          provided by selectIdentity).
 */
let verifyIdentity = function verifyIdentity(aSignature, aCallback) {
  let parts = aSignature.split(".");
  if (parts.length != 5) {
    aCallback(new Error("Invalid signature"), null);
    return;
  }

  let pubkey = "blah";
  let message = JSON.parse(jwcrypto.base64Decode(parts[3]));
  let assertion = [parts[0], parts[1], parts[2]].join(".");

  jwcrypto.verifyAssertion(assertion, pubkey, function(err, val) {
    if (err) {
      aCallback(err, null);
      return;
    }
    let ret = {
      aud: message.aud,
      message: message.message,
      iss: val.iss,
      exp: val.exp,
      iat: val.iat,
      principal: val.principal
    };
    aCallback(null, ret);
  });
};

/**
 * Select an identity for use by a WEBRTC PeerConnection
 *
 * @param aPeerConnectionId
 *        (string)          The unique ID of the PeerConnection.  Something
 *                          like a uuid would be lovely.
 *
 * @param aCallback
 *        (function)        Called after the user has successfully selected
 *                          an identity.  First argument to callback will be
 *                          a string error (if any) or null.  If no error,
 *                          second argument will be a dictionary containing:
 *
 *                          {idp:        The user's identity provider,
 *                           identity:   The user's identity,
 *                           protocol:   "persona",
 *                           assertion:  IdP certificate + assertion for PC origin,
 *                           authModule: Pointer to an AuthModule}
 */
let selectIdentity = function selectIdentity(aPeerConnectionId, aWindowId, aCallback) {
  // Create a client object representing the PeerConnection to be
  // used in provisioning and authentication flows.  The PeerConnection
  // is treated as an RP whose origin is the ID of the PC.
  //
  // The action begins at the end with watch() and request().  The doLogin
  // callback is called when an identity has successfully been provisioned,
  // at which point we create an AuthModule and return it to the PC caller.
  let origin = 'webrtc://'+aPeerConnectionId.toString();
  let client = {
    id: aPeerConnectionId,
    windowId: aWindowId,
    loggedInEmail: null,
    origin: origin,
    doError: doError,
    doLogin: doLogin,
    doLogout: function(){},
    doReady: function(){}
  };

  // XXX assume that PeerConnection IDs are always unique
  // If not, login won't be triggered the second time an ID
  // is used - only ready.
  function doError(err) {
    return aCallback(err);
  }
  function doLogin(assertion) {
    // Find out what email was used to log in.  It's hard to imagine
    // how this could come back null (i.e., not logged in), except by
    // a very fast logout race condition.
    let email = IdentityService.getLoggedInEmail(client.origin);
    if (!email) {
      aCallback("User did not login successfully");
      return;
    }

    let cert = IdentityService.fetchIdentity(email).cert;

    // Great success!  OMG WEBRTC+BID BFF #FTW!
    let identity = {
      identity: email,
      cert: cert,
      assertion: assertion,
      idp: DEFAULT_IDP,
      protocol: DEFAULT_PROTOCOL,
      origin: origin
    };
    // XXX For Persona, creation of the AuthModule is actually synchronous,
    // but it maybe different for other protocols, so we use a callbacks
    // on nextTick.
    aCallback(null, new AuthModule(aPeerConnectionId, identity));
  }

  // Register listeners
  // XXX we don't want to be messing around with observers and listeners
  // XXX should be a way to just call this directly
  IdentityService.RP.watch(client);

  // Request an identity
  IdentityService.RP.request(client.id, {});
};
