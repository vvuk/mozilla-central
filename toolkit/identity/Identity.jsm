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

var EXPORTED_SYMBOLS = ["IdentityService",];

const ALGORITHMS = { RS256: 1, DS160: 2, };

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
    // XXX - should remove key from store?
    delete this._identities[aEmail];
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
  this._store = new IDServiceStore();
}

IDService.prototype = {
  // DOM Methods.

  /**
   * Register a listener for a given windowID as a result of a call to
   * navigator.id.watch().
   *
   * @param aOptions
   *        (Object)  An object containing the same properties as handed
   *                  to the watch call made in DOM. See nsIDOMIdentity.idl
   *                  for more information on each property.
   *
   * @param aWindowID
   *        (int)     A unique number representing the window from which this
   *                  call was made.
   */
  watch: function watch(aOptions, aWindowID)
  {
    // TODO: do type checking on aOptions properties.
    // TODO: only persist a whitelist of properties
  },

  /**
   * Initiate a login with user interaction as a result of a call to
   * navigator.id.request().
   *
   * @param aWindowID
   *        int       A unique number representing a window which is requesting
   *                  the assertion.
   *
   * If an assertion is obtained successfully, aOptions.onlogin will be called,
   * as registered with a preceding call to watch for the same window ID. It is
   * an error to invoke request() without first calling watch().
   */
  request: function request(aWindowID)
  {
    Services.obs.notifyObservers(/*aWindowID*/ null, "identity-request", null);
  },

  /**
   * Invoked when a user wishes to logout of a site (for instance, when clicking
   * on an in-content logout button).
   *
   * @param aWindowID
   *        int       A unique number representing a window which is requesting
   *                  the assertion.
   *
   * Will cause the onlogout callback passed to navigator.id.watch() to be invoked.
   */
  logout: function logout(aWindowID)
  {
    // TODO
  },

  /**
   * Notify the Identity module that content has finished loading its
   * provisioning context and is ready to being the provisioning process.
   * 
   * @param aCallback
   *        (Function)  A callback that will be called with (email, time), where
   *                    email is the address for which a certificate is
   *                    requested, and the time is the *maximum* time allowed
   *                    for the validity of the ceritificate.
   *
   * @param aWindowID
   *        int         A unique number representing the window in which the
   *                    provisioning page for the IdP has been loaded.
   */
  beginProvisioning: function beginProvisioning(aCallback, aWindowID)
  {

  },

  /**
   * TODO
   */
  provisioningFailure: function provisioningFailure(aReason, aWindowID)
  {
    log("provisioningFailure: " + aReason);
    /* TODO:
     * if (CONTEXT.authenticationDone)
     *   hard fail
     * else
     *   try to authenticate (setting CONTEXT.authenticationDone to true)
     */
    let identity = this._pendingProvisioningData[aWindowID].identity;
    this._getEndpoints(identity, function(aEndpoints) {
      if (aEndpoints && aEndpoints.authentication)
        this._beginAuthenticationFlow(identity, aEndpoints.authentication);
      else
        throw new Error("Invalid or non-existent authentication endpoint");
    }.bind(this));

  },

  /**
   * Generates a keypair for the current user being provisioned and returns
   * the public key via the callback.
   *
   * @param aCallback
   *        (Function)  A callback that will be called with the public key
   *                    of the generated keypair.
   *
   * @param aWindowID
   *        int         A unique number representing the window in which this
   *                    call was made.
   *
   * It is an error to call genKeypair without receiving the callback for
   * the beginProvisioning() call first.
   */
  genKeypair: function genKeypair(aCallback, aWindowID)
  {

  },

  /**
   * Sets the certificate for the user for which a certificate was requested
   * via a preceding call to beginProvisioning (and genKeypair).
   *
   * @param aCert
   *        (String)  A JWT representing the signed certificate for the user
   *                  being provisioned, provided by the IdP.
   *
   * @param aWindowID
   *        int         A unique number representing the window in which this
   *                    call was made.
   */
  registerCertificate: function registerCertificate(aCert, aWindowID)
  {

  },

  /**
   * Notify the Identity module that content has finished loading its
   * authentication context and is ready to being the authentication process.
   *
   * @param aCallback
   *        (Function)  A callback that will be called with (identity), where
   *                    identity is the email address for which authentication is
   *                    requested.
   *
   * @param aWindowID
   *        int         A unique number representing the window in which the
   *                    authentication page for the IdP has been loaded.
   */
  beginAuthentication: function beginAuthentication(aCallback, aWindowID)
  {
    // TODO
  },

  completeAuthentication: function completeAuthentication(aWindowID)
  {
    // TODO: invoke the Provisioning Flow
  },

  cancelAuthentication: function cancelAuthentication(aWindowID)
  {
    // TODO: proceed to Provisioning Hard-Fail.
  },

  // Public utility methods.

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
   *                            chosen. If both this property and "sameEmailAs"
   *                            are set, an exception will be thrown.
   *
   *          "sameEmailAs"   : If set, instructs the function to issue an
   *                            assertion for the same email that was provided
   *                            to the domain specified by this value. If this
   *                            information could not be obtained, the call
   *                            will fail. If both this property and
   *                            "requiredEmail" are set, an exception will be
   *                            thrown.
   *
   *          "audience"      : The audience for which the assertion is to be
   *                            issued. If this property is not set an exception
   *                            will be thrown.
   *
   *        Any properties not listed above will be ignored.
   */
  getAssertion: function getAssertion(aCallback, aOptions)
  {

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

  shutdown: function shutdown()
  {
    this._registry = null;
    this._endpoints = null;
  },

  /**
   * Return the list of identities a user may want to use to login to aOrigin.
   */
  getIdentitiesForSite: function getIdentitiesForSite(aOrigin) {
/* TODO
    let rv = {result: []};
    for (let id in this._store.getIdentities()) {
      
    }
*/
    return {
      "result": [
        "foo@eyedee.me",
        "joe@mockmyid.com",
        "matt@browserid.linuxsecured.net",
      ],
      lastUsed: "foo@eyedee.me", // or null if a new origin
    };
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
  _pendingProvisioningData: { },
  _pendingAuthenticationData: { },

  setPendingAuthenticationData: function(aWindowID, aContext) {
    this._pendingAuthenticationData[aWindowID] = aContext;
  },

  /**
   * Generates an nsIIdentityServiceKeyPair object that can sign data. It also
   * provides all of the public key properties needed by a JW* formatted object
   *
   * @param string aAlgorithm
   *        Either RS256: "1" or DS160: "2", see constant ALGORITHMS above
   * @param string aOrigin
   *        a 'prepath' url, ex: https://www.mozilla.org:1234/
   * @param string aUserID
   *        Most likely, this is an email address
   * @returns void
   *          An internal callback object will notifyObservers of topic
   *          "id-service-key-gen-finished" when the keypair is ready.
   *          Access to the keypair is via the getIdentityServiceKeyPair() method
   **/
  _generateKeyPair: function _generateKeyPair(aAlgorithm, aOrigin, aUserID)
  {
    if (!ALGORITHMS[aAlgorithm]) {
      throw new Error("IdentityService: Unsupported algorithm");
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
        let alg = ALGORITHMS[aAlgorithm];
        let url = Services.io.newURI(aOrigin, null, null).prePath;
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

        self._registry[key.userID + "__" + key.url] = keyWrapper;
        Services.obs.notifyObservers(null,
                                     "id-service-key-gen-finished",
                                     JSON.stringify({ url: url, userID: aUserID }));
      },
    };

    IDKeyPair.generateKeyPair(ALGORITHMS[aAlgorithm], new keyGenCallback());
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
    let uri = Services.io.newURI(aUrl, null, null);
    let key = aUserID + "__" + uri.prePath;
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
  _getEndpoints: function _getEndpoints(email, aCallback)
  {
    log("_getEndpoints\n");
    // TODO: validate email
    let emailDomain = email.substring(email.indexOf("@") + 1);
    log("_getEndpoints: " + emailDomain + "\n");
    // TODO: lookup in cache
    let onSuccess = function(aDomain, aWellKnown) {
      // aDomain is the domain that the well-known file was on (not necessarily the email domain for cases 2 & 3)
      this._endpoints[emailDomain] = {};
      // TODO: convert to full URI if not already
      // TODO: require HTTPS?
      this._endpoints[emailDomain].authentication = "https://" + aDomain + aWellKnown.authentication;
      this._endpoints[emailDomain].provisioning = "https://" + aDomain + aWellKnown.provisioning;
      aCallback(this._endpoints[emailDomain]);
    }.bind(this);
    this._fetchWellKnownFile(emailDomain, onSuccess, function onFailure() {
      // TODO: use proxy IDP service
      //this._fetchWellKnownFile(..., onSuccess);
      // TODO: fallback to persona.org
      aCallback(null);
    }.bind(this));
  },

  _fetchWellKnownFile: function _fetchWellKnownFile(domain, aSuccess, aFailure) {
    var XMLHttpRequest = Cc["@mozilla.org/appshell/appShellService;1"]
                           .getService(Ci.nsIAppShellService)
                           .hiddenDOMWindow.XMLHttpRequest;
    var req  = new XMLHttpRequest();
    // TODO: require HTTPS?
    req.open("GET", "https://" + domain + "/.well-known/browserid", true);
    req.responseType = "json";
    req.mozBackgroundRequest = true;
    req.onreadystatechange = function(oEvent) {
      if (req.readyState === 4) {
        if (req.status === 200) {
          // TODO validate format
          aSuccess(domain, req.response);
          log(JSON.stringify(this._endpoints[domain], null, 4) + "\n");
        } else {
          log("Error: " + req.statusText + "\n");
          aFailure(req.statusText);
        }
      }
    }.bind(this);
    req.send(null);
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
      let context = {identity: aIdentity, securityLevel: this.securityLevel/*TODO: remove?*/, authenticationDone: false /* TODO */};
      IdentityService._pendingProvisioningData[utils.outerWindowID] = context;
      dump("_beginProvisioningFlow: " + utils.outerWindowID + "\n");
    }.bind(this));
  },

  /**
   * Load the authentication UI to start the authentication process.
   */
  _beginAuthenticationFlow: function _beginAuthentication(aIdentity, aURL)
  {
    var propBag = Cc["@mozilla.org/hash-property-bag;1"].
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
      case "identity-login": // User chose a new or exiting identity after a request() call
        let identity = aData; // String
        if (!identity) // TODO: validate email format
          throw new Error("Invalid identity chosen");
        // aData is the email address chosen (TODO: or null if cancelled?)
        // TODO: either do the authentication or provisioning flow depending on the email
        // TODO: is this correct to assume that if the identity is in the store that we don't need to re-provision?
        let storedID = this._store.fetchIdentity(identity);
        if (!storedID) {
          // begin provisioning
          this._getEndpoints(identity, function(aEndpoints) {
            if (aEndpoints && aEndpoints.provisioning)
              this._beginProvisioningFlow(identity, aEndpoints.provisioning);
            else
              throw new Error("Invalid or non-existent provisioning endpoint");
          }.bind(this));
        }
        break;
    }
  },
};

var IdentityService = new IDService();
