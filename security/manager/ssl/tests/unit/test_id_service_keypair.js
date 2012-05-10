/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

let Ci = Components.interfaces;
let Cc = Components.classes;
let Cu = Components.utils;
let Cr = Components.results;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyGetter(this, "IDServiceKeyPair", function (){
  return Cc["@mozilla.org/identityservice-keypair;1"].createInstance(Ci.nsIIdentityServiceKeyPair);
});

const ALG_RSA = 1;
const ALG_DSA = 2;

function log(aMsg){ dump("test_id: " + aMsg+ "\n"); };

function test_rsa()
{
  // RSA
  function rsaCallback() {}
  rsaCallback.prototype = {
    QueryInterface: function (iid)
    {
      log("RSA QI!!!");
      if (iid.equals(Ci.nsIIdentityServiceKeyGenCallback)) {
        return this;
      }
      throw Cr.NS_ERROR_NO_INTERFACE;
    },
    keyPairGenFinished: function (aKeyPair)
    {
      do_check_true(typeof aKeyPair.sign == "function");
      let sig = aKeyPair.sign("I am a string that mentions bacon. Yum.");
      do_check_true(sig.length > 1);
      do_check_true(aKeyPair.keyType == "1");
      do_check_true(aKeyPair.encodedPublicKey.length > 1);
      do_check_true(aKeyPair.encodedRSAPublicKeyModulus.length > 1);
      do_check_true(aKeyPair.encodedRSAPublicKeyExponent.length > 1);
      run_next_test();
    }
  };
  IDServiceKeyPair.generateKeyPair(ALG_RSA, new rsaCallback());
}

function test_dsa()
{
  function dsaCallback(aIDServiceKeyPair) {}
  dsaCallback.prototype = {
    QueryInterface: function QI(iid)
    {
      if (iid.equals(Ci.nsIIdentityServiceKeyGenCallback)) {
        return this;
      }
      throw Cr.NS_ERROR_NO_INTERFACE;
    },
    keyPairGenFinished: function dsa_keyPairGenFinished(aKeyPair)
    {
      do_check_true(typeof aKeyPair.sign == "function");
      let sig = aKeyPair.sign("I am a string that mentions bacon. Yum.");
      do_check_true(sig.length > 1);
      do_check_true(aKeyPair.keyType == "2");
      do_check_true(aKeyPair.encodedDSAGenerator.length > 1);
      do_check_true(aKeyPair.encodedDSAPrime.length > 1);
      do_check_true(aKeyPair.encodedDSASubPrime.length > 1);
      run_next_test();
      // do_test_finished();
    }
  };
  IDServiceKeyPair.generateKeyPair(ALG_DSA, new dsaCallback());
}

[test_rsa, test_dsa].forEach(add_test);

function run_test()
{
  run_next_test();
}
