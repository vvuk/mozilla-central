"use strict";

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

let Ci = Components.interfaces;
let Cc = Components.classes;
let Cu = Components.utils;
let Cr = Components.results;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

const idService = Cc["@mozilla.org/IdentityModule/service;1"]
                    .getService(Components.interfaces.nsIIdentityService);


const ALG_DSA = "DS160";
const ALG_RSA = "RS256";

function log(aMsg){ dump("ID Tests: " + aMsg+ "\n"); };

var CallbackPrototype = {
  QueryInterface: function(iid) {
    if (iid.equals(Components.interfaces.nsIIdentityKeyGenCallback) ||
        iid.equals(Components.interfaces.nsIIdentitySignCallback) ||
        iid.equals(Components.interfaces.nsISupports))
      return this;

    throw Components.results.NS_ERROR_NO_INTERFACE;
  },
};

function test_dsa() {
  var dsaCallback = {
    generateKeyPairFinished: function dsa_GenerateKeyPairFinished(rv, keyPair)
    {
      log("DSA generateKeyPairFinished");
      do_check_true(Components.isSuccessCode(rv));
      do_check_eq(typeof keyPair.sign, "function");
      log("DSA generateKeyPairFinished " + rv);
      //do_check_true(keyPair.keyType == AlG_DSA);
      do_check_eq(keyPair.hexDSAGenerator.length, 1024 / 8 * 2);
      do_check_eq(keyPair.hexDSAPrime.length, 1024 / 8 * 2);
      do_check_eq(keyPair.hexDSASubprime.length, 160 / 8 * 2);
      do_check_eq(keyPair.hexDSAPublicValue.length, 1024 / 8 * 2);
      // XXX: test that RSA parameters throw the correct error

      this.keyPair = keyPair;

      log("about to sign with DSA key");
      keyPair.sign("foo", this);
    },

    signFinished: function dsa_SignFinished(rv, signature)
    {
      log("DSA signFinished");
      log(signature);
      do_check_true(Components.isSuccessCode(rv));
      do_check_true(signature.length > 1);
      // TODO: verify the signature with the public key
      run_next_test();
    }
  };

  dsaCallback.prototype = CallbackPrototype;
  idService.generateKeyPair(ALG_DSA, dsaCallback);
}

function test_rsa() {
  // RSA Signature
  var rsaCallback = {
    generateKeyPairFinished: function rsa_GenerateKeyPairFinished(rv, keyPair)
    {
      log("RSA generateKeyPairFinished");
      do_check_true(Components.isSuccessCode(rv));
      do_check_true(typeof keyPair.sign == "function");
      do_check_true(keyPair.keyType == ALG_RSA);
      do_check_true(keyPair.hexRSAPublicKeyModulus.length > 1);
      do_check_true(keyPair.hexRSAPublicKeyExponent.length > 1);

      this.keyPair = keyPair;

      log("about to sign with RSA key");
      keyPair.sign("foo", this);
    },
    
    signFinished: function rsa_GenerateKeyPairFinished(rv, signature)
    {
      log("RSA signFinished");
      log(signature);
      do_check_true(Components.isSuccessCode(rv));
      do_check_true(signature.length > 1);
      run_next_test();
    }
  };
  rsaCallback.prototype = CallbackPrototype;

  idService.generateKeyPair(ALG_RSA, rsaCallback);
}

add_test(test_dsa);
add_test(test_rsa);

function run_test()
{
  run_next_test();
}
