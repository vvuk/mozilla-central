/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

// delay the loading of the IDService for performance purposes
XPCOMUtils.defineLazyGetter(this, "IDService", function (){
  let scope = {};
  Cu.import("resource:///modules/identity/Identity.jsm", scope);
  return scope.IdentityService;
});

const TEST_URL = "https://myfavoritebacon.com";
const TEST_URL2 = "https://myfavoritebaconinacan.com";
const TEST_USER = "user@mozilla.com";
const TEST_PRIVKEY = "fake-privkey";
const TEST_CERT = "fake-cert";

const ALGORITHMS = { RS256: 1, DS160: 2, };

let idObserver = {
  // nsISupports provides type management in C++
  // nsIObserver is to be an observer
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports, Ci.nsIObserver]),

  observe: function (aSubject, aTopic, aData)
  {
    var kpo;
    if (aTopic == "id-service-key-gen-finished") {
      // now we can pluck the keyPair from the store
      let key = JSON.parse(aData);
      kpo = IDService._getIdentityServiceKeyPair(key.userID, key.url);
      do_check_true(kpo != undefined);

      if (kpo.algorithm == ALGORITHMS.RS256) {
        checkRsa(kpo);
      }
      else if (kpo.algorithm == ALGORITHMS.DS160) {
        checkDsa(kpo);
      }
    }
  },
};

// we use observers likely for e10s process separation,
// but maybe a cb interface would work well here, tbd.
Services.obs.addObserver(idObserver, "id-service-key-gen-finished", false);

function checkRsa(kpo)
{
  do_check_true(kpo.sign != null);
  do_check_true(typeof kpo.sign == "function");
  do_check_true(kpo.userID != null);
  do_check_true(kpo.url != null);
  do_check_true(kpo.url == TEST_URL);
  do_check_true(kpo.publicKey != null);
  do_check_true(kpo.exponent != null);
  do_check_true(kpo.modulus != null);

  // TODO: should sign be async?
  let sig = kpo.sign("This is a message to sign");

  do_check_true(sig != null && typeof sig == "string" && sig.length > 1);

  do_test_finished();
  run_next_test();
}

function checkDsa(kpo)
{
  do_check_true(kpo.sign != null);
  do_check_true(typeof kpo.sign == "function");
  do_check_true(kpo.userID != null);
  do_check_true(kpo.url != null);
  do_check_true(kpo.url == TEST_URL2);
  do_check_true(kpo.publicKey != null);
  do_check_true(kpo.generator != null);
  do_check_true(kpo.prime != null);
  do_check_true(kpo.subPrime != null);

  let sig = kpo.sign("This is a message to sign");

  do_check_true(sig != null && typeof sig == "string" && sig.length > 1);

  // Services.obs.removeObserver(idObserver, "id-service-key-gen-finished");

  do_test_finished();
  run_next_test();
}

function test_rsa()
{
  do_test_pending();
  IDService._generateKeyPair("RS256", TEST_URL, TEST_USER);
}

function test_dsa()
{
  do_test_pending();
  IDService._generateKeyPair("DS160", TEST_URL2, TEST_USER);
}

function test_overall()
{
  do_check_true(IDService != null);
  run_next_test();
}

function test_id_store()
{
  // XXX - this is ugly, peaking in like this into IDService
  // probably should instantiate our own.
  var store = IDService._store;

  // try adding an identity
  store.addIdentity(TEST_USER, TEST_PRIVKEY, TEST_CERT);
  do_check_true(store.getIdentities()[TEST_USER] != null);
  do_check_true(store.getIdentities()[TEST_USER].cert == TEST_CERT);

  // clear the cert should keep the identity but not the cert
  store.clearCert(TEST_USER);
  do_check_true(store.getIdentities()[TEST_USER] != null);
  do_check_true(store.getIdentities()[TEST_USER].cert == null);
  
  // remove it should remove everything
  store.removeIdentity(TEST_USER);
  do_check_true(store.getIdentities()[TEST_USER] == undefined);

  // act like we're logged in to TEST_URL
  store.setLoginState(TEST_URL, true, TEST_USER);
  do_check_true(store.getLoginState(TEST_URL) != null);
  do_check_true(store.getLoginState(TEST_URL).isLoggedIn);
  do_check_true(store.getLoginState(TEST_URL).email == TEST_USER);

  // log out
  store.setLoginState(TEST_URL, false, TEST_USER);
  do_check_true(store.getLoginState(TEST_URL) != null);
  do_check_false(store.getLoginState(TEST_URL).isLoggedIn);

  // email is still set
  do_check_true(store.getLoginState(TEST_URL).email == TEST_USER);

  // not logged into other site
  do_check_true(store.getLoginState(TEST_URL2) == null);

  // clear login state
  store.clearLoginState(TEST_URL);
  do_check_true(store.getLoginState(TEST_URL) == null);
  do_check_true(store.getLoginState(TEST_URL2) == null);
  
  run_next_test();
}

function test_watch()
{
  
}

function test_request()
{
  
}

const TESTS = [test_overall, test_rsa, test_dsa, test_id_store];
TESTS.forEach(add_test);

function run_test()
{
  run_next_test();
}
