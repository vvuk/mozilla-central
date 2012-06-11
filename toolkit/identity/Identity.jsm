/* -*- Mode: js2; js2-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let Cu = Components.utils;
let Ci = Components.interfaces;
let Cc = Components.classes;
let Cr = Components.results;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/identity/Sandbox.jsm");
Cu.import("resource://gre/modules/DOMIdentity.jsm");

var EXPORTED_SYMBOLS = ["IdentityService"];
var FALLBACK_PROVIDER = "browserid.org";

const PREF_DEBUG = "toolkit.identity.debug";

XPCOMUtils.defineLazyServiceGetter(this,
                                   "IdentityCryptoService",
                                   "@mozilla.org/identity/crypto-service;1",
                                   "nsIIdentityCryptoService");

XPCOMUtils.defineLazyModuleGetter(this,
                                  "jwcrypto",
                                  "resource:///modules/identity/jwcrypto.jsm");

/**
 * log() - utility function to print a list of arbitrary things
 * Depends on IdentityService (bottom of this module).
 *
 * Enable with about:config pref toolkit.identity.debug
 */
function log(args) {
  if (! IdentityService._debugMode) return;

  let strings = [];
  let args = Array.prototype.slice.call(arguments);
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
  dump("@@ Identity.jsm: " + strings.join(' ') + "\n");
};

// the data store for IDService
// written as a separate thing so it can easily be mocked
function IDServiceStore() {
  this.reset();
}

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
    return this._loginStates[aOrigin] = {isLoggedIn: aState, email: aEmail};
  },
  getLoginState: function getLoginState(aOrigin) {
    return this._loginStates[aOrigin];
  },
  clearLoginState: function clearLoginState(aOrigin) {
    delete this._loginStates[aOrigin];
  },

  reset: function reset() {
    this._identities = {};
    this._loginStates = {};
  }
};


function IDService() {
  Services.obs.addObserver(this, "quit-application-granted", false);
  Services.obs.addObserver(this, "identity-login", false);
  // NB, prefs.addObserver and obs.addObserver have different interfaces
  Services.prefs.addObserver(PREF_DEBUG, this, false);

  this._debugMode = Services.prefs.getBoolPref(PREF_DEBUG);

  this.reset();
}

IDService.prototype = {
  // DOM Methods.

  /**
   * Reset the state of the IDService object.
   */
  reset: function reset() {
    // Forget all documents that call in.  (These are sometimes
    // referred to as callers.)
    this._rpFlows = {};

    // Forget all identities
    this._store = new IDServiceStore();

    // Clear the provisioning flows.  Provision flows contain an
    // identity, idpParams (how to reach the IdP to provision and
    // authenticate), a callback (a completion callback for when things
    // are done), and a provisioningFrame (which is the provisioning
    // sandbox).  Additionally, two callbacks will be attached:
    // beginProvisioningCallback and genKeyPairCallback.
    this._provisionFlows = {};

    // Clear the authentication flows.  Authentication flows attach
    // to provision flows.  In the process of provisioning an id, it
    // may be necessary to authenticate with an IdP.  The authentication
    // flow maintains the state of that authentication process.
    this._authenticationFlows = {};

    this._registry = {};
  },


  /**
   * Register a listener for a given windowID as a result of a call to
   * navigator.id.watch().
   *
   * @param aCaller
   *        (Object)  an object that represents the caller document, and
   *                  is expected to have properties:
   *                  - id (unique, e.g. uuid)
   *                  - loggedInEmail (string or null)
   *                  - origin (string)
   *
   *                  and a bunch of callbacks
   *                  - doReady()
   *                  - doLogin()
   *                  - doLogout()
   *                  - doError()
   *                  - doCancel()
   *
   */
  watch: function watch(aRpCaller) {
    this._rpFlows[aRpCaller.id] = aRpCaller;
    let origin = aRpCaller.origin;
    let state = this._store.getLoginState(origin) || {};

    // If the user is already logged in, then there are three cases
    // to deal with:
    //
    //   1. the email is valid and unchanged:  'ready'
    //   2. the email is null:                 'login'; 'ready'
    //   3. the email has changed:             'login'; 'ready'
    if (state.isLoggedIn) {
      if (state.email && aRpCaller.loggedInEmail === state.email) {
        this._notifyLoginStateChanged(aRpCaller.id, state.email);
        return aRpCaller.doReady();

      } else if (aRpCaller.loggedInEmail === null) {
        // Generate assertion for existing login
        let options = {requiredEmail: state.email, origin: origin};
        return this._doLogin(aRpCaller, options);

      } else {
        // A loggedInEmail different from state.email has been specified.
        // Change login identity.
        let options = {requiredEmail: aRpCaller.loggedInEmail, origin: origin};
        return this._doLogin(aRpCaller, options);
      }

    // If the user is not logged in, there are two cases:
    //
    //   1. a logged in email was provided: 'ready'; 'logout'
    //   2. not logged in, no email given:  'ready';

    } else {
      if (aRpCaller.loggedInEmail) {
        return this._doLogout(aRpCaller, {origin: origin});

      } else {
        return aRpCaller.doReady();
      }
    }
  },

  /**
   * A utility for watch() to set state and notify the dom
   * on login
   *
   * Note that this calls _getAssertion
   */
  _doLogin: function _doLogin(aRpCaller, aOptions, aAssertion) {
    let loginWithAssertion = function loginWithAssertion(assertion) {
      this._store.setLoginState(aOptions.origin, true, aOptions.loggedInEmail);
      this._notifyLoginStateChanged(aRpCaller.id, aOptions.loggedInEmail);
      aRpCaller.doLogin(assertion);
      aRpCaller.doReady();
    }.bind(this);

    if (aAssertion) {
      loginWithAssertion(aAssertion);
    } else {
      this._getAssertion(aOptions, function(err, assertion) {
        if (err) {
          Cu.reportError("Error on login attempt: " + err);
          this._doLogout(aRpCaller);
        } else {
          loginWithAssertion(assertion);
        }
      });
    }
  },

  /**
   * A utility for watch() to set state and notify the dom
   * on logout.
   */
  _doLogout: function _doLogout(aRpCaller, aOptions) {
    let state = this._store.getLoginState(aOptions.origin) || {};

    // XXX add tests for state change
    state.isLoggedIn = false;
    this._notifyLoginStateChanged(aRpCaller.id, null);

    aRpCaller.doLogout();
    aRpCaller.doReady();
  },

  /**
   * For use with login or logout, emit 'identity-login-state-changed'
   *
   * The notification will send the rp caller id in the properties,
   * and the email of the user in the message.
   *
   * @params aRpCallerId
   *         (integer) The id of the RP caller
   *
   * @params aIdentity
   *         (string) The email of the user whose login state has changed
   */
  _notifyLoginStateChanged: function _notifyLoginStateChanged(aRpCallerId, aIdentity) {
    let options = Cc["@mozilla.org/hash-property-bag;1"]
                    .createInstance(Ci.nsIWritablePropertyBag);
    options.setProperty("rpId", aRpCallerId);
    Services.obs.notifyObservers(options, "identity-login-state-changed", aIdentity);
  },

  /**
   * Initiate a login with user interaction as a result of a call to
   * navigator.id.request().
   *
   * @param aRPId
   *        (integer)  the id of the doc object obtained in .watch()
   *
   * @param aOptions
   *        (Object)  options including requiredEmail, privacyURL, tosURL
   */
  request: function request(aRPId, aOptions) {
    // Notify UX to display identity picker.
    let options = Cc["@mozilla.org/hash-property-bag;1"]
                    .createInstance(Ci.nsIWritablePropertyBag);

    // Pass the doc id to UX so it can pass it back to us later.
    options.setProperty("rpId", aRPId);

    // Also pass the options tos and privacy policy, and requiredEmail.
    for (let optionName of ["requiredEmail"]) {
      options.setProperty(optionName, aOptions[optionName]);
    }

    // Append URLs after resolving
    let rp = this._rpFlows[aRPId];
    let baseURI = Services.io.newURI(rp.origin, null, null);
    for (let optionName of ["privacyURL", "tosURL"]) {
      options.setProperty(optionName, baseURI.resolve(aOptions[optionName]));
    }

    Services.obs.notifyObservers(options, "identity-request", null);
  },

  /**
   * The UX wants to add a new identity
   * often followed by selectIdentity()
   *
   * @param aIdentity
   *        (string) the email chosen for login
   */
  addIdentity: function addIdentity(aIdentity) {
    if (this._store.fetchIdentity(aIdentity) === null) {
      this._store.addIdentity(aIdentity, null, null);
    }
  },

  /**
   * The UX comes back and calls selectIdentity once the user has picked
   * an identity.
   *
   * @param aRPId
   *        (integer) the id of the doc object obtained in .watch() and
   *                  passed to the UX component.
   *
   * @param aIdentity
   *        (string) the email chosen for login
   */
  selectIdentity: function selectIdentity(aRPId, aIdentity) {
    var self = this;
    // Get the RP that was stored when watch() was invoked.
    let rp = this._rpFlows[aRPId];
    if (!rp) {
      Cu.reportError("selectIdentity called with invalid rp id: " + aRPId);
      return null;
    }

    // It's possible that we are in the process of provisioning an
    // identity.
    let provId = rp.provId || null;

    // XXX consolidate rp, flows, etc. ?
    let rpLoginOptions = {
      loggedInEmail: aIdentity,
      origin: rp.origin
    };

    // Once we have a cert, and once the user is authenticated with the
    // IdP, we can generate an assertion and deliver it to the doc.
    self._generateAssertion(rp.origin, aIdentity, function(err, assertion) {
      if (!err && assertion) {
        self._doLogin(rp, rpLoginOptions, assertion);
        return;

      } else {
        // Need to provision an identity first.  Begin by discovering
        // the user's IdP.
        self._discoverIdentityProvider(aIdentity, function(err, idpParams) {
          if (err) {
            rp.doError(err);
            return;
          }

          // The idpParams tell us where to go to provision and authenticate
          // the identity.
          self._provisionIdentity(aIdentity, idpParams, provId, function(err, aProvId) {

            // Provision identity may have created a new provision flow
            // for us.  To make it easier to relate provision flows with
            // RP callers, we cross index the two here.
            rp.provId = aProvId;
            self._provisionFlows[aProvId].rpId = aRPId;

            // At this point, we already have a cert.  If the user is also
            // already authenticated with the IdP, then we can try again
            // to generate an assertion and login.
            if (!err) {
              // XXX quick hack - cleanup is done by registerCertificate
              // XXX order of callbacks and signals is a little tricky
              //self._cleanUpProvisionFlow(aProvId);
              self._generateAssertion(rp.origin, aIdentity, function(err, assertion) {
                if (! err) {
                  self._doLogin(rp, rpLoginOptions, assertion);
                  return;
                } else {
                  rp.doError(err);
                  return;
                }
              });

            // We are not authenticated.  If we have already tried to
            // authenticate and failed, then this is a "hard fail" and
            // we give up.  Otherwise we try to authenticate with the
            // IdP.
            } else {
              if (self._provisionFlows[aProvId].didAuthentication) {
                self._cleanUpProvisionFlow(aProvId);
                rp.doError("Authentication fail.");
                return;

              } else {
                // Try to authenticate with the IdP.  Note that we do
                // not clean up the provision flow here.  We will continue
                // to use it.
                self._doAuthentication(aProvId, idpParams);
                return;
              }
            }
          });
        });
      }
    });
  },

  /**
   * Generate an assertion, including provisioning via IdP if necessary,
   * but no user interaction, so if provisioning fails, aCallback is invoked
   * with an error.
   *
   * @param aAudience
   *        (string) web origin
   *
   * @param aIdentity
   *        (string) the email we're logging in with
   *
   * @param aCallback
   *        (function) callback to invoke on completion
   *                   with first-positional parameter the error.
   */
  _generateAssertion: function _generateAssertion(aAudience, aIdentity, aCallback) {
    let id = this._store.fetchIdentity(aIdentity);
    if (! (id && id.cert)) {
      return aCallback("Cannot generate assertion without a cert");
    }

    let kp = id.keyPair;

    if (!kp) {
      return aCallback("no kp");
    }

    jwcrypto.generateAssertion(id.cert, kp, aAudience, aCallback);
  },

  /**
   * Provision an Identity
   *
   * @param aIdentity
   *        (string) the email we're logging in with
   *
   * @param aIDPParams
   *        (object) parameters of the IdP
   *
   * @param aCallback
   *        (function) callback to invoke on completion
   *                   with first-positional parameter the error.
   */
  _provisionIdentity: function _provisionIdentity(aIdentity, aIDPParams, aProvId, aCallback) {
    let url = 'https://' + aIDPParams.domain + aIDPParams.idpParams.provisioning;

    // If aProvId is not null, then we already have a flow
    // with a sandbox.  Otherwise, get a sandbox and create a
    // new provision flow.

    if (aProvId !== null) {
      // Re-use an existing sandbox
      this._provisionFlows[aProvId].provisioningSandbox.load();

    } else {
      this._createProvisioningSandbox(url, function(aSandbox) {
        // create a provisioning flow, using the sandbox id, and
        // stash callback associated with this provisioning workflow.

        let provId = aSandbox.id;
        this._provisionFlows[provId] = {
          identity: aIdentity,
          idpParams: aIDPParams,
          securityLevel: this.securityLevel,
          provisioningSandbox: aSandbox,
          callback: function doCallback(aErr) {
            aCallback(aErr, provId);
          },
        };

        // XXX MAYBE
        // set a timeout to clear out this provisioning workflow if it doesn't
        // complete in X time.

      }.bind(this));
    }
  },

  /**
   * Discover the IdP for an identity
   *
   * @param aIdentity
   *        (string) the email we're logging in with
   *
   * @param aCallback
   *        (function) callback to invoke on completion
   *                   with first-positional parameter the error.
   */
  _discoverIdentityProvider: function _discoverIdentityProvider(aIdentity, aCallback) {
    let domain = aIdentity.split('@')[1];
    // XXX not until we have this mocked in the tests
    this._fetchWellKnownFile(domain, function(err, idpParams) {
      // idpParams includes the pk, authorization url, and
      // provisioning url.

      // XXX TODO follow any authority delegations
      // if no well-known at any point in the delegation
      // fall back to browserid.org as IdP

      // XXX TODO use email-specific delegation if IdP supports it
      // XXX TODO will need to update spec for that
      return aCallback(err, idpParams);
    });
  },

  /**
   * Invoked when a user wishes to logout of a site (for instance, when clicking
   * on an in-content logout button).
   *
   * @param aRpCallerId
   *        (integer)  the id of the doc object obtained in .watch()
   *
   */
  logout: function logout(aRpCallerId) {
    let rp = this._rpFlows[aRpCallerId];
    if (rp && rp.origin) {
      let audience = rp.origin;
      this._doLogout(rp, {audience: audience});
    }
    // We don't delete this._rpFlows[aRpCallerId], because
    // the user might log back in again.
  },

  /**
   * the provisioning iframe sandbox has called navigator.id.beginProvisioning()
   *
   * @param aCaller
   *        (object)  the iframe sandbox caller with all callbacks and
   *                  other information.  Callbacks include:
   *                  - doBeginProvisioningCallback(id, duration_s)
   *                  - doGenKeyPairCallback(pk)
   */
  beginProvisioning: function beginProvisioning(aCaller) {
    // Expect a flow for this caller already to be underway.
    let provFlow = this._provisionFlows[aCaller.id];
    if (!provFlow) {
      return aCaller.doError("No provision flow for caller with your id:", aCaller.id);
    }

    // keep the caller object around
    provFlow.caller = aCaller;

    let identity = provFlow.identity;
    let frame = provFlow.provisioningFrame;

    // Determine recommended length of cert.
    let duration = this.certDuration;

    // Make a record that we have begun provisioning.  This is required
    // for genKeyPair.
    provFlow.didBeginProvisioning = true;

    // Let the sandbox know to invoke the callback to beginProvisioning with
    // the identity and cert length.
    return aCaller.doBeginProvisioningCallback(identity, duration);
  },

  /**
   * the provisioning iframe sandbox has called
   * navigator.id.raiseProvisioningFailure()
   *
   * @param aProvId
   *        (int)  the identifier of the provisioning flow tied to that sandbox
   * @param aReason
   */
  raiseProvisioningFailure: function raiseProvisioningFailure(aProvId, aReason) {
    Cu.reportError("Provisioning failure: " + aReason);
    // look up the provisioning caller and its callback
    let provFlow = this._provisionFlows[aProvId];

    // Sandbox is deleted in _cleanUpProvisionFlow in case we re-use it.

    // This may be either a "soft" or "hard" fail.  If it's a
    // soft fail, we'll flow through setAuthenticationFlow, where
    // the provision flow data will be copied into a new auth
    // flow.  If it's a hard fail, then the callback will be
    // responsible for cleaning up the now defunct provision flow.

    // invoke the callback with an error.
    return provFlow.callback(aReason);
  },

  /**
   * When navigator.id.genKeyPair is called from provisioning iframe sandbox.
   * Generates a keypair for the current user being provisioned.
   *
   * @param aProvId
   *        (int)  the identifier of the provisioning caller tied to that sandbox
   *
   * It is an error to call genKeypair without receiving the callback for
   * the beginProvisioning() call first.
   */
  genKeyPair: function genKeyPair(aProvId) {
    // Look up the provisioning caller and make sure it's valid.
    let provFlow = this._provisionFlows[aProvId];

    if (!provFlow) {
      log("Cannot genKeyPair on non-existing flow.");
      return null;
    }

    if (!provFlow.didBeginProvisioning) {
      return provFlow.callback("Cannot genKeyPair before beginProvisioning");
    }

    // Ok generate a keypair
    jwcrypto.generateKeyPair(jwcrypto.ALGORITHMS.DS160, function(err, kp) {
      if (err) {
        log("error generating keypair:" + err);
        return provFlow.callback(err);
      }

      provFlow.kp = kp;

      // Serialize the publicKey of the keypair and send it back to the
      // sandbox.
      provFlow.caller.doGenKeyPairCallback(provFlow.kp.serializedPublicKey);
    }.bind(this));
  },

  /**
   * When navigator.id.registerCertificate is called from provisioning iframe
   * sandbox.
   *
   * Sets the certificate for the user for which a certificate was requested
   * via a preceding call to beginProvisioning (and genKeypair).
   *
   * @param aProvId
   *        (integer) the identifier of the provisioning caller tied to that
   *                  sandbox
   *
   * @param aCert
   *        (String)  A JWT representing the signed certificate for the user
   *                  being provisioned, provided by the IdP.
   */
  registerCertificate: function registerCertificate(aProvId, aCert) {
    // look up provisioning caller, make sure it's valid.
    let provFlow = this._provisionFlows[aProvId];
    if (!provFlow && provFlow.caller) {
      Cu.reportError("Cannot register cert; No provision flow or caller");
      return null;
    }
    if (!provFlow.kp)  {
      Cu.reportError("Cannot register cert; No keypair");
      return provFlow.callback("Cannot register a cert without generating a keypair first");
    }

    // store the keypair and certificate just provided in IDStore.
    this._store.addIdentity(provFlow.identity, provFlow.kp, aCert);

    // Great success!
    provFlow.callback(null);

    // Clean up the flow.
    this._cleanUpProvisionFlow(aProvId);
  },

  /**
   * Begin the authentication process with an IdP
   *
   * @param aProvId
   *        (int) the identifier of the provisioning flow which failed
   *
   * @param aCallback
   *        (function) to invoke upon completion, with
   *                   first-positional-param error.
   */
  _doAuthentication: function _doAuthentication(aProvId, aIDPParams) {
    // create an authentication caller and its identifier AuthId
    // stash aIdentity, idpparams, and callback in it.

    // extract authentication URL from idpParams

    // ? create a visible frame with sandbox and notify UX
    // or notify UX so it can create the visible frame, not sure which one.
    // TODO: make the two lines below into a helper to be used for auth and authentication
    let authPath = aIDPParams.idpParams.authentication;
    let authURI = Services.io.newURI("https://" + aIDPParams.domain, null, null).resolve(authPath);

    // beginAuthenticationFlow causes the "identity-auth" topic to be
    // observed.  Since it's sending a notification to the DOM, there's
    // no callback.  We wait for the DOM to trigger the next phase of
    // provisioning.
    this._beginAuthenticationFlow(aProvId, authURI);

    // either we bind the AuthID to the sandbox ourselves, or UX does that,
    // in which case we need to tell UX the AuthId.
    // Currently, the UX creates the UI and gets the AuthId from the window
    // and sets is with setAuthenticationFlow
  },

  /**
   * The authentication frame has called navigator.id.beginAuthentication
   *
   * IMPORTANT: the aCaller is *always* non-null, even if this is called from
   * a regular content page. We have to make sure, on every DOM call, that
   * aCaller is an expected authentication-flow identifier. If not, we throw
   * an error or something.
   *
   * @param aCaller
   *        (object)  the authentication caller
   *
   */
  beginAuthentication: function beginAuthentication(aCaller) {
    // Begin the authentication flow after having concluded a provisioning
    // flow.  The aCaller that the DOM gives us will have the same ID as
    // the provisioning flow we just concluded.  (see setAuthenticationFlow)
    let authFlow = this._authenticationFlows[aCaller.id];
    if (!authFlow) {
      return aCaller.doError("beginAuthentication: no flow for caller id", aCaller.id);
    }

    // stash the caller in the flow
    // XXX do we need to do this?
    authFlow.caller = aCaller;

    let identity = this._provisionFlows[authFlow.provId].identity;

    // tell the UI to start the authentication process
    return authFlow.caller.doBeginAuthenticationCallback(identity);
  },

  /**
   * The auth frame has called navigator.id.completeAuthentication
   *
   * @param aAuthId
   *        (int)  the identifier of the authentication caller tied to that sandbox
   *
   */
  completeAuthentication: function completeAuthentication(aAuthId) {
    // look up the AuthId caller, and get its callback.
    let authFlow = this._authenticationFlows[aAuthId];
    if (!authFlow) {
      Cu.reportError("completeAuthentication: No auth flow with id " + aAuthId);
      return;
    }
    let provId = authFlow.provId;

    // delete caller
    delete authFlow['caller'];
    delete this._authenticationFlows[aAuthId];

    let provFlow = this._provisionFlows[provId];
    provFlow.didAuthentication = true;
    Services.obs.notifyObservers(null, "identity-auth-complete", aAuthId);

    // We have authenticated in order to provision an identity.
    // So try again.
    this.selectIdentity(provFlow.rpId, provFlow.identity);
  },

  /**
   * The auth frame has called navigator.id.cancelAuthentication
   *
   * @param aAuthId
   *        (int)  the identifier of the authentication caller
   *
   */
  cancelAuthentication: function cancelAuthentication(aAuthId) {
    // look up the AuthId caller, and get its callback.
    let authFlow = this._authenticationFlows[aAuthId];
    if (!authFlow) {
      Cu.reportError("cancelAuthentication: No auth flow with id " + aAuthId);
    }
    let provId = authFlow.provId;

    // delete caller
    delete authFlow['caller'];
    delete this._authenticationFlows[aAuthId];

    let provFlow = this._provisionFlows[provId];
    provFlow.didAuthentication = true;
    Services.obs.notifyObservers(null, "identity-auth-complete", aAuthId);

    // invoke callback with ERROR.
    return provFlow.callback("authentication cancelled by IDP");
  },

  // methods for chrome and add-ons

  /**
   * Obtain a BrowserID assertion with the specified characteristics.
   *
   * @param aCallback
   *        (Function) Callback to be called with (err, assertion) where 'err'
   *        can be an Error or NULL, and 'assertion' can be NULL or a valid
   *        BrowserID assertion. If no callback is provided, an exception is
   *        thrown.
   *
   * @param aOptions
   *        (Object) An object that may contain the following properties:
   *
   *          "requiredEmail" : An email for which the assertion is to be
   *                            issued. If one could not be obtained, the call
   *                            will fail. If this property is not specified,
   *                            the default email as set by the user will be
   *                            chosen.
   *
   *          "audience"      : The audience for which the assertion is to be
   *                            issued. If this property is not set an exception
   *                            will be thrown.
   *
   *        Any properties not listed above will be ignored.
   */
  _getAssertion: function _getAssertion(aOptions, aCallback) {
    let audience = aOptions.audience;
    let email = aOptions.requiredEmail || this.getDefaultEmailForOrigin(audience);

    // We might not have any identity info for this email
    if (!this._store.fetchIdentity(email)) {
      this.addIdentity(email, null, null);
    }

    let cert = this._store.fetchIdentity(email)['cert'];
    if (cert) {
      this._generateAssertion(audience, email, function(err, assertion) {
        return aCallback(err, assertion);
      });

    } else {
      // We need to get a certificate.  Discover the identity's
      // IdP and provision
      this._discoverIdentityProvider(email, function(err, idpParams) {
        if (err) return aCallback(err);

        // Now begin provisioning from the IdP
        this._generateAssertion(audience, email, function(err, assertion) {
          return aCallback(err, assertion);
        }.bind(this));
      }.bind(this));
    }
  },

  /**
   * Obtain a BrowserID assertion by asking the user to login and select an
   * email address.
   *
   * @param aCallback
   *        (Function) Callback to be called with (err, assertion) where 'err'
   *        can be an Error or NULL, and 'assertion' can be NULL or a valid
   *        BrowserID assertion. If no callback is provided, an exception is
   *        thrown.
   *
   * @param aOptions
   *        (Object) An object that may contain the following properties:
   *
   *          "audience"      : The audience for which the assertion is to be
   *                            issued. If this property is not set an exception
   *                            will be thrown.
   *
   * @param aFrame
   *        (iframe) A XUL iframe element where the login dialog will be
   *        rendered.
   */
  getAssertionWithLogin: function getAssertionWithLogin(aCallback, aOptions, aFrame) {
    // XXX - do we need this call?
  },

  //
  //
  //

  shutdown: function shutdown() {
    this.reset();
  },

  getDefaultEmailForOrigin: function getDefaultEmailForOrigin(aOrigin) {
    let identities = this.getIdentitiesForSite(aOrigin);
    return identities.lastUsed || null;
  },

  /**
   * Return the list of identities a user may want to use to login to aOrigin.
   */
  getIdentitiesForSite: function getIdentitiesForSite(aOrigin) {
    let rv = { result: [] };
    for (let id in this._store.getIdentities()) {
      rv.result.push(id);
    }
    let loginState = this._store.getLoginState(aOrigin);
    if (loginState && loginState.email)
      rv.lastUsed = loginState.email;
    return rv;
  },

  /**
   * Called by the UI to set the ID and caller for the authentication flow after it gets its ID
   */
  setAuthenticationFlow: function(aAuthId, aProvId) {
    // this is the transition point between the two flows,
    // provision and authenticate.  We tell the auth flow which
    // provisioning flow it is started from.

    this._authenticationFlows[aAuthId] = { provId: aProvId };
    this._provisionFlows[aProvId].authId = aAuthId;
  },

  get securityLevel() {
    return 1;
  },

  get certDuration() {
    switch(this.securityLevel) {
      default:
        return 3600;
    }
  },

  // TODO: need helper to logout of all sites for SITB?

  /**
   * Fetch the well-known file from the domain.
   *
   * @param aDomain
   *
   * @param aScheme
   *        (string) (optional) Protocol to use.  Default is https.
   *                 This is necessary because we are unable to test
   *                 https.
   *
   * @param aCallback
   *
   */
  _fetchWellKnownFile: function _fetchWellKnownFile(aDomain, aScheme, aCallback) {
    if (arguments.length <= 2) {
      aCallback = aScheme;
      aScheme = "https";
    }
    let url = aScheme + '://' + aDomain + "/.well-known/browserid";

    /*
    let XMLHttpRequest = Cc["@mozilla.org/appshell/appShellService;1"]
                           .getService(Ci.nsIAppShellService)
                           .hiddenDOMWindow.XMLHttpRequest;*/

    // let req  = new XMLHttpRequest();

    // this appears to be a more successful way to get at xmlhttprequest (which supposedly will close with a window
    let req = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"]
                .getService(Ci.nsIXMLHttpRequest);

    // XXX how can we detect whether we are off-line?
    // TODO: decide on how to handle redirects
    req.open("GET", url, true);
    req.responseType = "json";
    req.mozBackgroundRequest = true;
    req.onload = function _fetchWellKnownFile_onload() {
      if (req.status < 200 || req.status >= 400)
        return aCallback(req.status);
      try {
        let idpParams = req.response;

        // Verify that the IdP returned a valid configuration
        if (! (idpParams.provisioning &&
            idpParams.authentication &&
            idpParams['public-key'])) {
          log("Invalid well-known file from: " + aDomain);
          return aCallback("Invalid well-known file from: " + aDomain);
        }

        let callbackObj = {
          domain: aDomain,
          idpParams: idpParams,
        };
        // Yay.  Valid IdP configuration for the domain.
        return aCallback(null, callbackObj);

      } catch (err) {
        Cu.reportError("Bad configuration from " + aDomain + " : " +err);
        return aCallback(err.toString());
      }
    };
    req.onerror = function _fetchWellKnownFile_onerror() {
      let err = "Failed to fetch well-known file";
      if (req.status) err += " " + req.status + ":";
      if (req.statusText) err += " " + req.statusText;
      return aCallback(err);
    };
    req.send(null);
  },

  /**
   * Load the provisioning URL in a hidden frame to start the provisioning
   * process.
   * TODO: CHANGE this call to be just _createSandbox, and do the population
   * of the flow object in _provisionIdentity instead, so that method has full
   * context.
   */
  _createProvisioningSandbox: function _createProvisioningSandbox(aURL, aCallback) {
    new Sandbox(aURL, aCallback);
  },

  /**
   * Load the authentication UI to start the authentication process.
   */
  _beginAuthenticationFlow: function _beginAuthenticationFlow(aProvId, aURL) {
    let propBag = Cc["@mozilla.org/hash-property-bag;1"]
                    .createInstance(Ci.nsIWritablePropertyBag);
    propBag.setProperty("provId", aProvId);

    Services.obs.notifyObservers(propBag, "identity-auth", aURL);
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports, Ci.nsIObserver]),

  observe: function observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "quit-application-granted":
        Services.obs.removeObserver(this, "quit-application-granted", false);
        Services.obs.removeObserver(this, "identity-login", false);
        this.shutdown();
        break;

      case "nsPref:changed":
        this._debugMode = Services.prefs.getBoolPref(PREF_DEBUG);
        break;
    }
  }.bind(this),

  /**
   * Clean up a provision flow and the authentication flow and sandbox
   * that may be attached to it.
   */
  _cleanUpProvisionFlow: function _cleanUpProvisionFlow(aProvId) {
    let prov = this._provisionFlows[aProvId];
    let rp = this._rpFlows[prov.rpId];

    // Clean up the sandbox, if there is one.
    if (prov.provisioningSandbox) {
      let sandbox = this._provisionFlows[aProvId]['provisioningSandbox'];
      if (sandbox.free) {
        sandbox.free();
      }
      delete this._provisionFlows[aProvId]['provisioningSandbox'];
    }

    // Clean up a related authentication flow, if there is one.
    if (this._authenticationFlows[prov.authId]) {
      delete this._authenticationFlows[prov.authId];
    }

    // Finally delete the provision flow and any reference to it
    // from the rpFlows
    delete this._provisionFlows[aProvId];
    if (rp) {
      delete rp['provId'];
    }
  }
};

var IdentityService = new IDService();
