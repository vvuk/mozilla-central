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

const IdentityCryptoService
  = Cc["@mozilla.org/identity/crypto-service;1"]
      .getService(Ci.nsIIdentityCryptoService);

var EXPORTED_SYMBOLS = ["jwcrypto"];

const ALGORITHMS = { RS256: "RS256", DS160: "DS160" };

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
  let output = "Identity jwcrypto.jsm: " + strings.join(' ') + "\n";
  dump(output);

  // Additionally, make the output visible in the Error Console
  Services.console.logStringMessage(output);

}

/*
 * An XPCOM data structure to invoke key generation
 * and call itself back
 */
function keygenerator() {}

keygenerator.prototype = {
  QueryInterface: function(aIID)
  {
    if (aIID.equals(Ci.nsIIdentityKeyGenCallback)) {
      return this;
    }
    throw Cr.NS_ERROR_NO_INTERFACE;
  },

  generateKeyPair: function(aAlgorithmName, aCallback)
  {
    this.callback = aCallback;
    IdentityCryptoService.generateKeyPair(aAlgorithmName, this);
  },

  generateKeyPairFinished: function(rv, aKeyPair)
  {
    if (!Components.isSuccessCode(rv)) {
      return this.callback("key generation failed");
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
      return this.callback("unknown key type");
    }

    let keyWrapper = {
      serializedPublicKey: JSON.stringify(publicKey),
      _kp: aKeyPair
    };

    return this.callback(null, keyWrapper);
  }
};

/*
 * An XPCOM data structure to invoke signing
 * and call itself back
 */
function signer() {
}

signer.prototype = {
  QueryInterface: function (aIID)
  {
    if (aIID.equals(Ci.nsIIdentityKeyGenCallback)) {
      return this;
    }
    throw Cr.NS_ERROR_NO_INTERFACE;
  },

  sign: function(aPayload, aKeypair, aCallback)
  {
    this.payload = aPayload;
    this.callback = aCallback;
    aKeypair._kp.sign(this.payload, this);
  },

  signFinished: function (rv, signature)
  {
    log("signFinished");
    if (!Components.isSuccessCode(rv)) {
      log("sign failed");
	    return this.callback("Sign Failed");
	  }

    this.callback(null, signature);
    log("signFinished: calling callback");
  }
};

function jwcryptoClass()
{
}

jwcryptoClass.prototype = {
  isCertValid: function(aCert, aCallback) {
    // XXX check expiration
    aCallback(true);
  },

  generateKeyPair: function(aAlgorithmName, aCallback) {
    log("generating");
    var the_keygenerator = new keygenerator();
    the_keygenerator.generateKeyPair(aAlgorithmName, aCallback);
  },

  generateAssertion: function(aCert, aKeyPair, aAudience, aCallback) {
    // for now, we hack the algorithm name
    var header = {"alg": "DS128"};
    var headerBytes = IdentityCryptoService.base64UrlEncode(
                          JSON.stringify(header));

    var payload = {
      // expires in 2 minutes
      exp: Date.now() + (2 * 60 * 1000),
      aud: aAudience
    };
    var payloadBytes = IdentityCryptoService.base64UrlEncode(
                          JSON.stringify(payload));

    log("payload bytes", payload, payloadBytes);
    var theSigner = new signer();
    theSigner.sign(headerBytes + "." + payloadBytes, aKeyPair, function(err, signature) {
      if (err)
        return aCallback(err);

      var signedAssertion = headerBytes + "." + payloadBytes + "." + signature;
      return aCallback(null, aCert + "~" + signedAssertion);
    });
  }

};

var jwcrypto = new jwcryptoClass();
jwcrypto.ALGORITHMS = ALGORITHMS;