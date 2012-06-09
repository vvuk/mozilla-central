"use strict"
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
  dump("@@ test_identity_jsm: " + strings.join(' ') + "\n");
}

function test_generate()
{
  do_test_pending();
  jwcrypto.generateKeyPair("DS160", function(err, kp) {
    do_check_eq(err, null);
    do_check_neq(kp, null);
    
    do_test_finished();
    run_next_test();
  });
}

function test_get_assertion()
{
  do_test_pending();

  jwcrypto.generateKeyPair(
    "DS160",
    function(err, kp) {
      jwcrypto.generateAssertion("fake-cert", kp, RP_ORIGIN, function(err, assertion) {
        do_check_eq(err, null);

        // more checks on assertion
        log("assertion", assertion);

        do_test_finished();
        run_next_test();
      });
    });
}

var TESTS = [test_generate, test_get_assertion];

TESTS.forEach(add_test);

function run_test()
{
  run_next_test();
}
