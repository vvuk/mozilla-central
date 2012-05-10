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

var EXPORTED_SYMBOLS = ["IdentityService",];

XPCOMUtils.defineLazyGetter(this, "IDKeyPair", function () {
  return Cc["@mozilla.org/identityservice-keypair;1"].createInstance(Ci.nsIIdentityServiceKeyPair);
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

const ALGORITHMS = { RS256: 1, DS160: 2, };

function IDService()
{
  Services.obs.addObserver(this, "quit-application-granted", false);
}

IDService.prototype = {
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
  getAssertion: function IS_getAssertion(aCallback, aOptions)
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
  getAssertionWithLogin: function IS_getAssertionWithLogin(aCallback, aOptions, aFrame)
  {

  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports, Ci.nsIObserver]),

  observe: function IS_observe(aSubject, aTopic, aData)
  {
    if (aTopic == "quit-application-granted") {
      Services.obs.removeObserver(this, "quit-application-granted", false);
      this.shutdown();
    }
  },

  _registry: { },

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
  generateKeyPair: function IS_generateKeyPair(aAlgorithm, aOrigin, aUserID)
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

  shutdown: function IS_shutdown()
  {
    this._registry = null;
  },

  /**
   * Returns a keypair object from the Identitiy in-memory storage
   *
   * @param string aUserID
   *        Most likely an email address
   * @param string aUrl
   *        a "prepath" url: https://www.mozilla.org:1234/
   * @returns object
   *
   * The returned obejct will have different properties based on which algorithm was used
   * to generate the keypair. Check the 'algorithm' property before accessing
   * additional properties.
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
  getIdentityServiceKeyPair: function IS_getIdentityServiceKeypair(aUserID, aUrl)
  {
    let uri = Services.io.newURI(aUrl, null, null);
    let key = aUserID + "__" + uri.prePath;
    let keyObj =  this._registry[key];

    if (!keyObj) {
      throw new Error("getIdentityServiceKeyPair: Invalid Key");
    }
    return keyObj;
  },
};

function supString(aString)
{
  let str = Cc["@mozilla.org/supports-string;1"].
    createInstance(Ci.nsISupportsString);
  str.data = aString;
  return str;
}

var IdentityService = new IDService();
