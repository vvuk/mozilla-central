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
var INTERNAL_ORIGIN = "browserid://";

const ALGORITHMS = { RS256: 1, DS160: 2 };

XPCOMUtils.defineLazyGetter(this, "IDKeyPair", function () {
  return Cc["@mozilla.org/identityservice-keypair;1"].
    createInstance(Ci.nsIIdentityServiceKeyPair);
});

XPCOMUtils.defineLazyGetter(this, "jwcrypto", function (){
  let scope = {};
  Cu.import("resource:///modules/identity/jwcrypto.jsm", scope);
  return scope.jwcrypto;
});

XPCOMUtils.defineLazyServiceGetter(this,
                                   "uuidGenerator",
                                   "@mozilla.org/uuid-generator;1",
                                   "nsIUUIDGenerator");

function uuid()
{
  return uuidGenerator.generateUUID();
}

/**
 * log() - utility function to print a list of arbitrary things
 */
function log()
{
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
      strings.push(JSON.stringify(arg, null, 2));
    }
  });                
  dump("@@ Identity.jsm: " + strings.join(' ') + "\n");
}

// the data store for IDService
// written as a separate thing so it can easily be mocked
function IDServiceStore()
{
  this.reset();
}

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
    this._loginStates[aOrigin] = {isLoggedIn: aState, email: aEmail};
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


function IDService()
{
  Services.obs.addObserver(this, "quit-application-granted", false);
  Services.obs.addObserver(this, "identity-login", false);
  this.reset();
}

IDService.prototype = {
  // DOM Methods.

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
  watch: function watch(aCaller)
  {
    this._rpFlows[aCaller.id] = aCaller;
    let origin = aCaller.origin;
    let state = this._store.getLoginState(origin) || {};

    // If the user is already logged in, then there are three cases
    // to deal with
    // 1. the email is valid and unchanged:  'ready'
    // 2. the email is null:                 'login'; 'ready'
    // 3. the email has changed:             'login'; 'ready'
    if (state.isLoggedIn) {
      // Logged in; ready
      if (!!state.email && aCaller.loggedInEmail === state.email) {
        return aCaller.doReady();

      } else if (aCaller.loggedInEmail === null) {
        // Generate assertion for existing login
        let options = {requiredEmail: state.email, audience: origin};
        return this._doLogin(aCaller, options);

      } else {
        // A loggedInEmail different from state.email has been specified
        // Change login identity
        let options = {requiredEmail: aCaller.loggedInEmail, audience: origin};
        return this._doLogin(aCaller, options);
      }

    // If the user is not logged in, there are two cases:
    // 1. a logged in email was provided: 'ready'; 'logout'
    // 2. not logged in, no email given:  'ready';
    } else {
      if (!! aCaller.loggedInEmail) {
        // not logged in; logout
        return this._doLogout(aCaller, {audience: origin});

      } else {
        return aCaller.doReady();
      }
    }
  },

  /**
   * A utility for watch() to set state and notify the dom
   * on login
   */
  _doLogin: function _doLogin(aCaller, aOptions) 
  {
    let state = this._store.getLoginState(aOptions.audience) || {};

    this.getAssertion(aOptions, function(err, assertion) {
      if (err) {
        // XXX i think this is right?
        return this._doLogout(aCaller);
      } 

      // XXX add tests for state change
      state.isLoggedIn = true;
      state.email = aOptions.loggedInEmail;
      aCaller.doLogin(assertion);
      return aCaller.doReady();
    }.bind(this));
  },

  /**
   * A utility for watch() to set state and notify the dom
   * on logout.
   */
  _doLogout: function _doLogout(aCaller, aOptions) {
    let state = this._store.getLoginState(aOptions.audience) || {};

    // XXX add tests for state change
    state.isLoggedIn = false;    
    aCaller.doReady();
    return aCaller.doLogout();
  },

  /**
   * Initiate a login with user interaction as a result of a call to
   * navigator.id.request().
   *
   * @param aCallerId
   *        (integer)  the id of the doc object obtained in .watch()
   *
   * @param aOptions
   *        (Object)  options including requiredEmail, privacyURL, tosURL
   */
  request: function request(aCallerId, aOptions)
  {
    // notify UX to display identity picker
    // pass the doc id to UX so it can pass it back to us later.
    // also pass the options tos and privacy policy, and requiredEmail

    let options = Cc["@mozilla.org/hash-property-bag;1"].
                  createInstance(Ci.nsIWritablePropertyBag);
    options.setProperty("requestID", aCallerId);
    for (let optionName of ["requiredEmail", "privacyURL", "tosURL"]) {
      options.setProperty(optionName, aOptions[optionName]);
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
  addIdentity: function addIdentity(aIdentity)
  {
    if (this._store.fetchIdentity(aIdentity) === null) {
      this._store.addIdentity(aIdentity, null, null);
    }
  },

  /**
   * The UX comes back and calls selectIdentity once the user has picked an identity
   *
   * @param aCallerId
   *        (integer) the id of the doc object obtained in .watch() and
   *                  passed to the UX component.
   *
   * @param aIdentity
   *        (string) the email chosen for login
   */
  selectIdentity: function selectIdentity(aCallerId, aIdentity)
  {
    var self = this;

    let caller = this._rpFlows[aCallerId];
    if (! caller) {
      log("No caller with id", aCallerId);
      return null;
    }

    // set the state of logi
    let state = this._store.getLoginState(caller.origin) || {};
    state.isLoggedIn = true;
    state.email = aIdentity;

    // go generate assertion for this identity and deliver it to this doc
    // XXX duplicates getAssertion
    self._generateAssertion(caller.origin, aIdentity, function(err, assertion) {
      // if fail, we don't have the cert to do the assertion
      if (err) {
        log("need to get cert");
        
        // figure out the IdP
        self._discoverIdentityProvider(aIdentity, function(err, idpParams) { 
           log("discovered idp", idpParams);
                                         
          // using IdP info, we provision 
          log("now provision")                                         ;
          self._provisionIdentity(aIdentity, idpParams, function(err, identity) {
            log("provisioned identity", identity);

            // if fail on callback, need to authentication
            if (err) {
              self.doAuthentication(aIdentity, idpParams, function(err, authd) {
                // we try provisioning again
                self._provisionIdentity(aIdentity, idpParams, function (err, identity) {
                  // if we fail, hard fail
                  if (err) {
                    return caller.doError("Aw, snap.");
                  } else {
                    // yay
                    self.generateAssertion(aCallerId, aIdentity, function(err, assertion) {
                      return caller.doLogin(assertion);
                    });
                  }
                });
              });

            // successfully provisioned using idp info
            } else {
              return caller.doLogin(assertion);
            }
          });
        });


      } else {
        log("no error on getAssertion", assertion);
        caller.doLogin(assertion);
        return caller.doReady();
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
  _generateAssertion: function _generateAssertion(aAudience, aIdentity, aCallback)
  {
    let id = this._store.fetchIdentity(aIdentity);
    if (! (id && id.cert)) {
      return aCallback("Cannot generate assertion without a cert");
    }
    
    let kp = this._getIdentityServiceKeyPair(aIdentity, INTERNAL_ORIGIN);
    log("have kp");
    if (kp) {
      return jwcrypto.generateAssertion(id.cert, kp, aAudience, aCallback);
      
    // XXX Maybe have to generate a key pair first ?
    // e.g., when changing identities
    } else {
      this._generateKeyPair("DS160", INTERNAL_ORIGIN, aIdentity, function(err, key) {
        kp = this._getIdentityServiceKeyPair(aIdentity, INTERNAL_ORIGIN);
        return jwcrypto.generateAssertion(id.cert, kp, aAudience, aCallback);
      });
    } 
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
  _provisionIdentity: function _provisionIdentity(aIdentity, aIDPParams, aCallback)
  {
    log('provision identity', aIdentity, aIDPParams);
    let url = 'https://' + aIDPParams.domain + aIDPParams.idpParams.provisioning;
    this._beginProvisioningFlow(aIdentity, url, function(err, hotdamn) {
      log(err, hotdamn);                             
    });
    // create a provisioning flow identifier provId and store the caller
    // stash callback associated with this provisioning workflow.
    
    // launch the provisioning workflow given the IdP provisoning URL
    // using the sandbox, store the sandbox in our provisioning data
    // XXX - _beginProvisioningFlow ??
    // pass the provId to the sandbox so its DOM calls can be identified

    // MAYBE
    // set a timeout to clear out this provisioning workflow if it doesn't
    // complete in X time.
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
  _discoverIdentityProvider: function _discoverIdentityProvider(aIdentity, aCallback)
  {
    let domain = aIdentity.split('@')[1];
    // XXX not until we have this mocked in the tests
    this._fetchWellKnownFile(domain, function(err, idpParams) {
      // XXX TODO follow any authority delegations
      // if no well-known at any point in the delegation
      // fall back to browserid.org as IdP

      // XXX TODO use email-specific delegation if IdP supports it
      // XXX TODO will need to update spec for that

      // idpParams includes pk, authorization, provisioning.
      return aCallback(null, idpParams);
    });
  },
  
  /**
   * Invoked when a user wishes to logout of a site (for instance, when clicking
   * on an in-content logout button).
   *
   * @param aCallerId
   *        (integer)  the id of the doc object obtained in .watch()
   *
   */
  logout: function logout(aCallerId)
  {
    this._rpFlows[aCallerId].doLogout();
    delete this._rpFlows[aCallerId];
  },

  /**
   * the provisioning iframe sandbox has called navigator.id.beginProvisioning()
   *
   * @param aCaller
   *        (object)  the caller with all callbacks and other information
   *                  callbacks include:
   *                  - doBeginProvisioningCallback(id, duration_s)
   *                  - doGenKeyPairCallback(pk)
   */
  beginProvisioning: function beginProvisioning(aCaller)
  {
    // look up the provisioning caller and the identity we're trying to provision
    let flow = this._provisionFlows[aCaller.id];
    if (!flow) {
      return aCaller.doError("no such provisioning flow");
    }

    // keep the caller object around
    flow.caller = aCaller;
    
    let identity = flow.identity;
    let frame = flow.provisioningFrame;

    // as part of that caller. determine recommended length of cert.
    let duration = this.certDuration;

    // XXX is this where we indicate that the flow is "valid" for keygen?
    flow.state = "provisioning";

    // let the sandbox know to invoke the callback to beginProvisioning with
    // the identity and cert length.
    return flow.caller.doBeginProvisioningCallback(identity, duration);
  },

  /**
   * the provisioning iframe sandbox has called navigator.id.raiseProvisioningFailure()
   * 
   * @param aProvId
   *        (int)  the identifier of the provisioning flow tied to that sandbox
   */
  raiseProvisioningFailure: function raiseProvisioningFailure(aProvId, aReason)
  {
    log("provisioningFailure: " + aReason);
    
    // look up the provisioning caller and its callback
    let flow = this._provisionFlows[aProvId];
    let cb = flow.cb;

    // Clean up the sandbox here?
    if (flow.sandbox) {
      flow.sandbox.free();
    }

    // delete the provisioning context
    delete this._provisionFlows[aProvId];

    // invoke the callback with an error.
    return cb(aReason);

    // we probably do the below code at a higher level,
    // e.g. in selectIdentity()
    /*
    let identity = this._provisionFlows[aProvId].identity;
    this._getEndpoints(identity, function(aEndpoints) {
      if (aEndpoints && aEndpoints.authentication)
        this._beginAuthenticationFlow(identity, aEndpoints.authentication);
      else
        throw new Error("Invalid or non-existent authentication endpoint");
    }.bind(this));
     */
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
  genKeyPair: function genKeyPair(aProvId)
  {
    // look up the provisioning caller, make sure it's valid.
    let flow = this._provisionFlows[aProvId];
    if (!flow) {
      log("Cannot genKeyPair on non-existing flow.  Flow could have ended.");
      return null;
    }

    if (flow.state !== "provisioning") {
      return flow.cb("Cannot genKeyPair before beginProvisioning");
    }

    // generate a keypair
    this._generateKeyPair("DS160", INTERNAL_ORIGIN, flow.identity, function(err, key) {
      flow.kp = this._getIdentityServiceKeyPair(key.userID, key.url);
      return flow.caller.doGenKeyPairCallback(key);
    }.bind(this));

    // we have a handle on the sandbox, we need to invoke the genKeyPair callback
    // on it with the serialized public key of the keypair.

    // the API into the sandbox is likely in DOMIdentity.jsm,
    // but we need some guidance here.
  },

  /**
   * When navigator.id.registerCertificate is called from provisioning iframe sandbox.
   * Sets the certificate for the user for which a certificate was requested
   * via a preceding call to beginProvisioning (and genKeypair).
   *
   * @param aProvId
   *        (uuid) the identifier of the provisioning caller tied to that sandbox
   *
   * @param aCert
   *        (String)  A JWT representing the signed certificate for the user
   *                  being provisioned, provided by the IdP.
   */
  registerCertificate: function registerCertificate(aProvId, aCert)
  {
    // look up provisioning caller, make sure it's valid.
    let flow = this._provisionFlows[aProvId];
    if (! flow && flow.caller) {
      return null;
    }
    if (! flow.kp)  {
      return flow.cb("Cannot register a cert without generating a keypair first");
    }

    // store the keypair and certificate just provided in IDStore.
    this._store.addIdentity(flow.identity, flow.kp, aCert);

    // kill the sandbox
    delete flow.caller;

    // pull out the prov caller callback
    let callback = flow.cb;

    // kill the prov caller
    delete this._provisionFlows[aProvId];

    // invoke callback with success.
    return callback(null);
  },

  /**
   * Begin the authentication process with an IdP
   *
   * @param aIdentity
   *        (string) the email address we're identifying as to the IdP
   *
   * @param aCallback
   *        (function) to invoke upon completion, with
   *                   first-positional-param error.
   */
  doAuthentication: function doAuthentication(aIdentity, idpParams, aCallback)
  {
    // create an authentication caller and its identifier AuthId
    // stash aIdentity, idpparams, and callback in it.

    // extract authentication URL from idpParams
    
    // ? create a visible frame with sandbox and notify UX
    // or notify UX so it can create the visible frame, not sure which one.

    // either we bind the AuthID to the sandbox ourselves, or UX does that,
    // in which case we need to tell UX the AuthId.
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
   *        (object)  the authentication caller tied to that sandbox
   *
   */
  beginAuthentication: function beginAuthentication(aCaller)
  {
    // look up AuthId caller, and the identity we're attempting to authenticate.

    // get the flow that goes with this caller
    // sandbox is already attached as .sandbox

    // XXX we need pointer to the IFRAME/sandbox.
    // maybe this means we should create it, or maybe UX passes it to us
    // after it's created it, but we need the direct pointer.

    // tell sandbox to invoke the doBeginAuthentication callback with
    // the identity we want.
  },

  /**
   * The auth frame has called navigator.id.completeAuthentication
   *
   * @param aAuthId
   *        (int)  the identifier of the authentication caller tied to that sandbox
   *
   */
  completeAuthentication: function completeAuthentication(aAuthId)
  {
    // look up the AuthId caller, and get its callback.

    // delete caller

    // invoke callback with success.
    // * what callback? Spec says to invoke the Provisioning Flow -- MN
  },

  /**
   * The auth frame has called navigator.id.cancelAuthentication
   *
   * @param aAuthId
   *        (int)  the identifier of the authentication caller tied to that sandbox
   *
   */
  cancelAuthentication: function cancelAuthentication(aWindowID)
  {
    // look up the AuthId caller, and get its callback.

    // delete caller

    // invoke callback with ERROR.

    // What callback? Specs says to proceed to Provisioning Hard-Fail. -- MN
  },

  // methods for chrome and add-ons

  /**
   * Twiddle the login state at an origin
   * a bit more hackish
   */
  getLoginState: function getLoginState(aOrigin, aCallback)
  {
    
  },

  /**
   * @param aState
   *        (object) with fields isLoggedIn and identity
   */
  setLoginState: function setLoginState(aOrigin, aState, aCallback)
  {
    
  },

  /**
   * watches for state changes to a particular origin
   * and invokes callback with a status object
   *
   * @param aOrigin
   * 
   * @param aCallback
   */
  internalWatch: function internalWatch(aOrigin, aCallback)
  {
    
  },
  
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
  getAssertion: function getAssertion(aOptions, aCallback)
  {
    let audience = aOptions.audience;
    let email = aOptions.requiredEmail || this.getDefaultEmailForOrigin(audience);
    // We might not have any identity info for this email
    // XXX is this right? 
    // if not, fix generateAssertion, which assumes we can fetchIdentity
    if (! this._store.fetchIdentity(email)) {
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
        // XXX TODO
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
  getAssertionWithLogin: function getAssertionWithLogin(
    aCallback, aOptions, aFrame)
  {
    // XXX - do we need this call?
  },

  //
  //
  //
  
  shutdown: function shutdown()
  {
    this._registry = null;
    this._endpoints = null;
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
  setAuthenticationFlow: function(aAuthID, aCaller) {
    this._authenticationFlows[aAuthID] = aCaller;
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

  // Private.
  _registry: { },
  _endpoints: { },

  /**
   * Generates an nsIIdentityServiceKeyPair object that can sign data. It also
   * provides all of the public key properties needed by a JW* formatted object
   *
   * @param string aAlgorithmName
   *        Either RS256 or DS160 (keys to constant ALGORITHMS above)
   * @param string aOrigin
   *        a 'prepath' url, ex: https://www.mozilla.org:1234/
   * @param string aUserID
   *        Most likely, this is an email address
   * @returns void
   *          An internal callback object will notifyObservers of topic
   *          "id-service-key-gen-finished" when the keypair is ready.
   *          Access to the keypair is via the getIdentityServiceKeyPair() method
   **/
  _generateKeyPair: function _generateKeyPair(aAlgorithmName, aOrigin, aUserID, aCallback)
  {
    let alg = ALGORITHMS[aAlgorithmName];
    if (! alg) {
      throw new Error("IdentityService: Unsupported algorithm: " + aAlgorithmName);
    }

    var self = this;

    function keyGenCallback() { }

    keyGenCallback.prototype = {

      QueryInterface: function (aIID)
      {
        if (aIID.equals(Ci.nsIIdentityServiceKeyGenCallback)) {
          return this;
        }
        throw Cr.NS_ERROR_NO_INTERFACE;
      },

      keyPairGenFinished: function (aKeyPair)
      {
        let url = aOrigin; // Services.io.newURI(aOrigin, null, null).prePath;
        let id = uuid();
        var keyWrapper;
        let pubK = aKeyPair.encodedPublicKey; // DER encoded, then base64 urlencoded
        let key = { userID: aUserID, url: url };

        switch (alg) {
        case ALGORITHMS.RS256:
          keyWrapper = {
            algorithm: alg,
            userID: aUserID,
            sign:        aKeyPair.sign,
            url:         url,
            publicKey:   aKeyPair.encodedPublicKey,
            exponent:    aKeyPair.encodedRSAPublicKeyExponent,
            modulus:     aKeyPair.encodedRSAPublicKeyModulus,
          };

          break;

        case ALGORITHMS.DS160:
          keyWrapper = {
            algorithm: alg,
            userID: aUserID,
            sign:       aKeyPair.sign,
            url:        url,
            publicKey:  pubK,
            generator:  aKeyPair.encodedDSAGenerator,
            prime:      aKeyPair.encodedDSAPrime,
            subPrime:   aKeyPair.encodedDSASubPrime,
          };

          break;
        default:
          throw new Error("Unsupported algorithm");
        }

        let keyID = key.userID + "__" + key.url;
        self._registry[keyID] = keyWrapper;

        return  aCallback(null, {url:url, userID: aUserID});
      },
    };

    IDKeyPair.generateKeyPair(ALGORITHMS[aAlgorithmName], new keyGenCallback());
  },

  /**
   * Returns a keypair object from the Identity in-memory storage
   *
   * @param string aUserID
   *        Most likely an email address
   * @param string aUrl
   *        a "prepath" url: https://www.mozilla.org:1234/
   * @returns object
   *
   * The returned obejct will have different properties based on which algorithm
   * was used to generate the keypair. Check the 'algorithm' property before
   * accessing additional properties.
   *
   * RSA keypair properties:
   *   algorithm
   *   userID
   *   sign()
   *   url
   *   publicKey
   *   exponent
   *   modulus
   *
   * DSA keypair properties:
   *   algorithm
   *   userID
   *   sign()
   *   url
   *   publicKey
   *   generator
   *   prime
   *   subPrime
   **/
  _getIdentityServiceKeyPair: function _getIdentityServiceKeypair(aUserID, aUrl)
  {
    let key = aUserID + "__" + aUrl;
    let keyObj =  this._registry[key];
    if (!keyObj) {
      throw new Error("getIdentityServiceKeyPair: Invalid Key");
    }
    return keyObj;
  },

  /**
   * Determine the IdP endpoints for provisioning an authorization for a
   * given email address. The order of resolution is as follows:
   *
   * 1) Attempt to fetch /.well-known/browserid for the domain of the provided
   * email address. If a delegation was found, follow to the delegated domain
   * and repeat. If a valid IdP descriptin is found, parse and return values. 
   *
   * 2) Attempt to verify that the domain is supported by the ProxyIdP service
   * by Persona/BrowserID. If the domain is supported, treat persona.org as the
   * primary IdP and return the endpoint values accordingly.
   *
   * 3) Fallback to using persona.org as a secondary verifier. Return endpoints
   * for secondary authorization and provisioning provided by BrowserID/Persona.
   */
  // XXX this looks not great with side-effect instead of just functional.
  //  -- it's for caching purposes since we shouldn't expect the caller to known how to cache the endpoint
  _getEndpoints: function _getEndpoints(email, aCallback)
  {
    log("_getEndpoints\n");
    // TODO: validate email
    let emailDomain = email.substring(email.indexOf("@") + 1);
    log("_getEndpoints: " + emailDomain + "\n");
    // TODO: lookup in cache
    let wellKnownCallback = function(aError, aResult) {
      log("wellKnownCallback: " + !!aError);
      if (!!aError) {
        aCallback(null);
      } else {
        // aDomain is the domain that the well-known file was on (not necessarily the email domain for cases 2 & 3)
        this._endpoints[emailDomain] = {};
        // TODO: convert to full URI if not already
        // TODO: require HTTPS?
        this._endpoints[emailDomain].authentication = "https://" + aResult.domain + aResult.idpParams.authentication;
        this._endpoints[emailDomain].provisioning = "https://" + aResult.domain + aResult.idpParams.provisioning;
        aCallback(this._endpoints[emailDomain]);
      }
    }.bind(this);
    this._fetchWellKnownFile(emailDomain, wellKnownCallback);
  },

  _fetchWellKnownFile: function _fetchWellKnownFile(aDomain, aCallback) {
    // Mock now - just returns mockmyid.com well-known

    log("fetch well known", aDomain);
    let idpParams = {
    "public-key": {
        "algorithm": "RS",
        "n": "15498874758090276039465094105837231567265546373975960480941122651107772824121527483107402353899846252489837024870191707394743196399582959425513904762996756672089693541009892030848825079649783086005554442490232900875792851786203948088457942416978976455297428077460890650409549242124655536986141363719589882160081480785048965686285142002320767066674879737238012064156675899512503143225481933864507793118457805792064445502834162315532113963746801770187685650408560424682654937744713813773896962263709692724630650952159596951348264005004375017610441835956073275708740239518011400991972811669493356682993446554779893834303",
        "e": "65537"},
    "authentication": "/browserid/sign_in.html",
    "provisioning": "/browserid/provision.html"
};
    return aCallback(null, {domain:"mockmyid.com", idpParams:idpParams});

    // XXX we'll do this LATER!
    let XMLHttpRequest = Cc["@mozilla.org/appshell/appShellService;1"]
                           .getService(Ci.nsIAppShellService)
                           .hiddenDOMWindow.XMLHttpRequest;
    let req  = new XMLHttpRequest();
    // TODO: require HTTPS?
    req.open("GET", "https://" + aDomain + "/.well-known/browserid", true);
    req.responseType = "json";
    req.mozBackgroundRequest = true;
    req.onreadystatechange = function(oEvent) {
      if (req.readyState === 4) {
        log("HTTP status: " + req.status);
        if (req.status === 200) {
          try {
            let idpParams = req.response;

            // Verify that the IdP returned a valid configuration
            if (! idpParams.provisioning && 
                  idpParams.authentication && 
                  idpParams['public-key']) {
              throw new Error("Invalid well-known file from: " + aDomain);
            }

            let callbackObj = {
              domain: aDomain,
              idpParams: idpParams,
            };
            // Yay.  Valid IdP configuration for the domain.
            return aCallback(null, callbackObj);

          } catch (err) {
            // Bad configuration from this domain.
            return aCallback(err);
          }
        } else {
          // We get nothing from this domain.
          return aCallback(req.statusText);
        }
      }
    }.bind(this);
    req.send(null);
    log("fetching https://" + aDomain + "/.well-known/browserid");
  },

  /**
   * Load the provisioning URL in a hidden frame to start the provisioning
   * process.
   */
  _beginProvisioningFlow: function _beginProvisioning(aIdentity, aURL, aCallback)
  {
log("begin prov flow", aIdentity, aURL);

    // TODO: cleanup sandbox (call free)
    new Sandbox(aURL, function(aSandbox) {
log(aURL, aSandbox);
      // TODO: move to helper in Sandbox.jsm
      let utils = aSandbox._frame.contentWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils); 
      let callerId = utils.outerWindowID;
      let caller = {
        id: callerId,
        identity: aIdentity, 
        securityLevel: this.securityLevel, 
        sandbox: aSandbox};
      this._provisionFlows[callerId] = caller;
      return aCallback(null, caller.id);
    }.bind(this));
  },

  /**
   * Load the authentication UI to start the authentication process.
   */
  _beginAuthenticationFlow: function _beginAuthentication(aIdentity, aURL)
  {
    let propBag = Cc["@mozilla.org/hash-property-bag;1"].
                  createInstance(Ci.nsIWritablePropertyBag);
    propBag.setProperty("identity", aIdentity);

    Services.obs.notifyObservers(propBag, "identity-auth", aURL);
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports, Ci.nsIObserver]),

  observe: function observe(aSubject, aTopic, aData)
  {
    switch (aTopic) {
      case "quit-application-granted":
        Services.obs.removeObserver(this, "quit-application-granted", false);
        Services.obs.removeObserver(this, "identity-login", false);
        this.shutdown();
        break;
    }
  },

  reset: function reset()
  {
    // Forget all documents
    this._rpFlows = {};

    // Forget all identities
    this._store = new IDServiceStore();
    
    // tracking ongoing flows

    // a provisioning flow contains
    // identity, idpParams, cb, provisioningFrame
    // idpParams includes the normal BrowserID IdP Parameters
    // cb is just a completion callback for when things are done
    // provisioningFrame is the provisioning frame pointer
    // with fields beginProvisioningCallback and genKeyPairCallback.
    this._provisionFlows = {};

    // an authentication flow contains...
    this._authenticationFlows = {};


  }

};

var IdentityService = new IDService();
