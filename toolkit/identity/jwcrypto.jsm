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
function log() {
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

function keygenerator() {}

keygenerator.prototype = {
  generateKeyPair: function(aAlgorithmName, aCallback) {
    log("gen key pair");
    IdentityCryptoService.generateKeyPair(aAlgorithmName, function(rv, keypair) {
      return this._generateKeyPairFinished(rv, keypair, aCallback);
    }.bind(this));
  },

  _generateKeyPairFinished: function(rv, aKeyPair, aCallback) {
    log("kp finished");
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

    return aCallback(null, keyWrapper);
  }
};

function signer() {
}

signer.prototype = {
  sign: function(aPayload, aKeypair, aCallback) {
    this.payload = aPayload;
    this.callback = aCallback;
    aKeypair._kp.sign(this.payload, function(rv, signature) {
      if (!Components.isSuccessCode(rv)) {
        log("ERROR: signer.sign failed");
        return aCallback("Sign failed");
      }
      log("signer.sign: success");
      return aCallback(null, signature);
    }.bind(this));
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