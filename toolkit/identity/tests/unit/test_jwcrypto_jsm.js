/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyGetter(this, "IDService", function (){
  let scope = {};
  Cu.import("resource:///modules/identity/Identity.jsm", scope);
  return scope.IdentityService;
});

XPCOMUtils.defineLazyGetter(this, "jwcrypto", function (){
  let scope = {};
  Cu.import("resource:///modules/identity/jwcrypto.jsm", scope);
  return scope.jwcrypto;
});

const INTERNAL_ORIGIN = "browserid://";
const TEST_USER = "user@mozilla.com";
const RP_ORIGIN = "http://123done.org";

function log(aMsg)
{
  dump("jwcrypto-Testing: " + aMsg + "\n");
}

function test_get_assertion()
{
  do_test_pending();

  IDService._generateKeyPair(
    "RS256", INTERNAL_ORIGIN, TEST_USER,
    function(err, key) {
      dump("got a keypair\n");
      var kp = IDService._getIdentityServiceKeyPair(key.userID, key.url);
      jwcrypto.generateAssertion("fake-cert", kp, RP_ORIGIN, function(err, assertion) {
        do_check_eq(err, null);

        log(assertion);

        do_test_finished();
        run_next_test();
      });
    });
}

var TESTS = [test_get_assertion];

TESTS.forEach(add_test);

function run_test()
{
  run_next_test();
}
