/* -*- Mode: js2; js2-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";


const Cu = Components.utils;
const Ci = Components.interfaces;
const Cc = Components.classes;
const Cr = Components.results;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/identity/LogUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this,
                                  "IDLog",
                                  "resource://gre/modules/identity/IdentityStore.jsm");

XPCOMUtils.defineLazyServiceGetter(this,
                                   "IdentityCryptoService",
                                   "@mozilla.org/identity/crypto-service;1",
                                   "nsIIdentityCryptoService");

const EXPORTED_SYMBOLS = ["jwcrypto"];

const ALGORITHMS = { RS256: "RS256", DS160: "DS160" };

function log(...aMessageArgs) {
  Logger.log.apply(Logger, ["jwcrypto"].concat(aMessageArgs));
}

function generateKeyPair(aAlgorithmName, aCallback) {
  log("Generate key pair; alg =", aAlgorithmName);

  IdentityCryptoService.generateKeyPair(aAlgorithmName, function(rv, aKeyPair) {
    if (!Components.isSuccessCode(rv)) {
      return aCallback("key generation failed");
    }

    var publicKey;

    switch (aKeyPair.keyType) {
     case ALGORITHMS.RS256:
      publicKey = {
        algorithm: "RS",
        exponent:  aKeyPair.hexRSAPublicKeyExponent,
        modulus:   aKeyPair.hexRSAPublicKeyModulus
      };
      break;

     case ALGORITHMS.DS160:
      publicKey = {
        algorithm: "DS",
        y: aKeyPair.hexDSAPublicValue,
        p: aKeyPair.hexDSAPrime,
        q: aKeyPair.hexDSASubPrime,
        g: aKeyPair.hexDSAGenerator
      };
      break;

    default:
      return aCallback("unknown key type");
    }

    let keyWrapper = {
      serializedPublicKey: JSON.stringify(publicKey),
      _kp: aKeyPair
    };

    return aCallback(null, keyWrapper);
  });
}

function sign(aPayload, aKeypair, aCallback) {
  aKeypair._kp.sign(aPayload, function(rv, signature) {
    if (!Components.isSuccessCode(rv)) {
      log("ERROR: signer.sign failed");
      return aCallback("Sign failed");
    }
    log("signer.sign: success");
    return aCallback(null, signature);
  });
}

function jwcryptoClass()
{
}

jwcryptoClass.prototype = {
  isCertValid: function(aCert, aCallback) {
    // XXX check expiration, bug 769850
    aCallback(true);
  },

  _extractAssertionComponents: function _extractAssertionComponents(aSignedObject) {
    if (typeof (aSignedObject) !== 'string') {
      throw("_extractAssertionComponents: String argument required");
    }
    var parts = aSignedObject.split('.');
    if (parts.length !== 3) {
      throw("_extractAssertionComponents: Invalid signed object");
    }

    // we verify based on the actual string
    // FIXME: we should validate that the header contains only proper fields
    return {signed: parts[0] + '.' + parts[1],
            header: JSON.parse(this.base64Decode(parts[0])),
            payload: JSON.parse(this.base64Decode(parts[1])),
            signature: parts[2]};
  },

  base64Encode: function(aToEncode) {
    return IdentityCryptoService.base64UrlEncode(aToEncode);
  },

  base64Decode: function(aToDecode) {
    return IdentityCryptoService.base64UrlDecode(aToDecode);
  },

  generateKeyPair: function(aAlgorithmName, aCallback) {
    log("generating");
    generateKeyPair(aAlgorithmName, aCallback);
  },

  generateAssertionWithExtraParams: function(aCert, aKeyPair, aAudience, aExtraParams, aCallback) {
    // for now, we hack the algorithm name
    // XXX bug 769851
    var header = {"alg": "DS128"};
    var headerBytes = this.base64Encode(JSON.stringify(header));

    var payload = {
      // expires in 2 minutes
      // XXX clock skew needs exploration bug 769852
      exp: Date.now() + (2 * 60 * 1000),
      aud: aAudience
    };

    // copy in the extra params
    Object.keys(aExtraParams).forEach(function(k) {
      payload[k] = aExtraParams[k];
    });

    var payloadBytes = this.base64Encode(JSON.stringify(payload));

    sign(headerBytes + "." + payloadBytes, aKeyPair, function(err, signature) {
      if (err) {
        return aCallback(err);
      }

      var signedAssertion = headerBytes + "." + payloadBytes + "." + signature;
      return aCallback(null, aCert + "~" + signedAssertion);
    });
  },

  generateAssertion: function(aCert, aKeyPair, aAudience, aCallback) {
    this.generateAssertionWithExtraParams(aCert, aKeyPair, aAudience, {}, aCallback);
  },

  verifyAssertion: function verifyAssertion(aSignedObject, aPublicKey, aCallback) {
    try {
      let components = this._extractAssertionComponents(aSignedObject);
// XXX: DO NOT SHIP WITHOUT ACTUALLY VERIFYING SIGNATURE. Bug 783105.
//      aPublicKey.verify(components.signed, components.signature, function(err, result) {
//        if (err) {
//        return aCallback(err);
//        }
        return aCallback(null, components.payload);
//      });
    } catch (err) {
      aCallback(err);
    }
  }
};

var jwcrypto = new jwcryptoClass();
jwcrypto.ALGORITHMS = ALGORITHMS;
