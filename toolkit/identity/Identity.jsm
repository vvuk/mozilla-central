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

XPCOMUtils.defineLazyServiceGetter(this,
                                   "uuidGenerator",
                                   "@mozilla.org/uuid-generator;1",
                                   "nsIUUIDGenerator");

function uuid()
{
  return uuidGenerator.generateUUID();
}

function log(aMsg)
{
  dump("IDService: " + aMsg + "\n");
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
         aCaller.doReady();

      } else if (aCaller.loggedInEmail === null) {
        // Generate assertion for existing login
        let options = {requiredEmail: state.email, audience: origin};
        this.getAssertion(options, function(err, assertion) {
          aCaller.doLogin(assertion);
          aCaller.doReady();
        });

      } else {
        // Change login identity
        let options = {requiredEmail: aCaller.loggedInEmail, audience: origin};
        this.getAssertion(options, function(err, assertion) {
          aCaller.doLogin(assertion);
          aCaller.doReady();
        });
      }

    // If the user is not logged in, there are two cases:
    // 1. a logged in email was provided:                   'ready'; 'logout'
    // 2. no email provided, but there is an id we can use: 'login'; 'ready'
    // 3. not logged in, no email given, no id to use:      'ready';
    } else {
      if (!! aCaller.loggedInEmail) {
        // not logged in; logout
        aCaller.doReady();
        aCaller.doLogout();
        
      } else {
        // No loggedInEmail declared
        // Is there an identity we can already use for this site?
        let identity = this.getDefaultEmailForOrigin(origin);
        if (!! identity) {
          let options = {requiredEmail: identity, audience: origin};
          this.getAssertion(options, function(err, assertion) {
            aCaller.doLogin(assertion);
            aCaller.doReady();
          });
        } else {
          // not logged in; no identity; ready
          aCaller.doReady();        
        }
      }
    }
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
    log("selectIdentity: for request " + aCallerId + " and " + aIdentity);
    // set the state of login for the doc origin to be logged in as that identity
    
    // go generate assertion for this identity and deliver it to this doc
    // this._generateAssertion(doc.origin, aIdentity, cb)

    // if fail, we don't have the cert to do the assertion

    // figure out the IdP
    // this._discoverIdentityProvider(aIdentity, cb);
    // we get IDPParams in the callback.

    // using IdP info, we provision
    // this._provisionIdentity(aIdentity, idpParams, cb);

    this._getEndpoints(aIdentity, function(aEndpoints) {
      if (aEndpoints && aEndpoints.provisioning)
        this._beginProvisioningFlow(aIdentity, aEndpoints.provisioning);
      else
        throw new Error("Invalid or non-existent provisioning endpoint");
    }.bind(this));

    // if fail on callback, need to authentication
    // this.doAuthentication(aIdentity, idpParams, cb)

    // we try provisioning again
    // this._provisionIdentity(aIdentity, idpParams, cb);

    // if we fail, hard fail.
    
    // if succeed, then
    // this.generateAssertion(aCallerId, aIdentity, cb)

    // doc.doLogin(assertion);
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
    let cert = this._store.fetchIdentity(aIdentity)['cert'];

    // XXX generate the assertion using the stored key 
    // XXX here's a dummy assertion for now ...
    return aCallback(null, "T35T.CERT.FTW~T35T.A553RT10N.W00T"); 
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

    return aCallback(null, {"some": "params"});

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

    log("@@@@ flow.caller.id = " + flow.caller.id + "\n");
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
    
    if (flow.state !== "provisioning") {
      return flow.cb("Cannot genKeyPair before beginProvisioning");
    }

    // generate a keypair
    this._generateKeyPair("DS160", INTERNAL_ORIGIN, flow.identity, function(err, key) {
                            dump("@@@ got one \n");

      //flow.caller.kp = this._registry[kpID];
      return flow.caller.doGenKeyPairCallback(key);
    });

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
   *        (int)  the identifier of the provisioning caller tied to that sandbox
   *
   * @param aCert
   *        (String)  A JWT representing the signed certificate for the user
   *                  being provisioned, provided by the IdP.
   */
  registerCertificate: function registerCertificate(aProvId, aCert)
  {
    // look up provisioning caller, make sure it's valid.

    // store the keypair and certificate just provided in IDStore.

    // kill the sandbox

    // pull out the prov caller callback

    // kill the prov caller

    // invoke callback with success.
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
   * IMPORTANT: the aAuthId is *always* non-null, even if this is called from
   * a regular content page. We have to make sure, on every DOM call, that aAuthId
   * is an expected authentication-flow identifier. If not, we throw an error
   * or something.
   *
   * @param aAuthId
   *        (int)  the identifier of the authentication caller tied to that sandbox
   *
   */
  beginAuthentication: function beginAuthentication(aAuthId)
  {
    // look up AuthId caller, and the identity we're attempting to authenticate.

    // XXX we need pointer to the IFRAME/sandbox.
    // maybe this means we should create it, or maybe UX passes it to us
    // after it's created it, but we need the direct pointer.

    // tell sandbox to invoke the postBeginAuthentication callback with
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
    return identities.lastUsed || identities.result[0] || null;
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
dump("@@@ generateKeyPair url : " + url);
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

        dump("@@@ sweet.  key is " + keyID + "\n");
        dump("@ callback: " + JSON.stringify(aCallback) + "\n");
        
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
  _beginProvisioningFlow: function _beginProvisioning(aIdentity, aURL)
  {
    // TODO: cleanup sandbox (call free)
    new Sandbox(aURL, function(aSandbox) {
      dump("creating sandbox in _beginProvisioningFlow for " + aIdentity + " at " + aURL + "\n");
      let utils = aSandbox._frame.contentWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils); // TODO: move to helper in Sandbox.jsm
      let caller = {identity: aIdentity, securityLevel: this.securityLevel/*TODO: remove?*/, authenticationDone: false /* TODO */};
      IdentityService._provisionFlows[utils.outerWindowID] = caller;
      dump("_beginProvisioningFlow: " + utils.outerWindowID + "\n");
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
