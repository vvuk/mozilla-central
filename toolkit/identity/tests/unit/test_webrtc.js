/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let webrtc = {};
Cu.import("resource://gre/modules/identity/WebRTC.jsm", webrtc);
Cu.import("resource://gre/modules/identity/Identity.jsm");

let saved_state = {};
let TEST_FINGERPRINT = uuid();

/**
 * setup_picked_identity - utility function for these tests to
 * simulate the user interaction that chooses and maybe authenticates
 * an identity when webrtc.selectIdentity is called for a given
 * origin.
 *
 * If an identity cannot be picked, aCallback is called with an error.
 *
 * If an identity is successfully selected, aCallback is called with
 * null (no error) and an AuthModule instance.
 */
function setup_picked_identity(aCallback) {
  // Setup a browserid identity
  setup_test_identity(TEST_USER, TEST_CERT, function() {

    let observer = {
      QueryInterface: XPCOMUtils.generateQI([Ci.snISupports, Ci.nsIObserver]),
      observe: function (aSubject, aTopic, aData) {
        if (aTopic === "identity-request") {
          // Now pick the browserid identity and get an authModule back for it
          IdentityService.selectIdentity(aSubject.wrappedJSObject.rpId, TEST_USER);
        }
      }
    };
    // Register an observer for the identity-request signal that will be
    // emitted when WebRTC's selectIdentity function issues a request().
    Services.obs.addObserver(observer, "identity-request", false);

    webrtc.selectIdentity(TEST_FINGERPRINT, aCallback);
  });
}

add_test(function test_overall() {
  do_check_eq((typeof webrtc.selectIdentity), 'function');
  run_next_test();
});


add_test(function test_instantiate_auth_module() {
  setup_picked_identity(function(err, authModule) {
    do_check_eq(err, null);
    do_check_eq((typeof authModule), 'object');
    run_next_test();
  });
});

add_test(function test_get_assertion() {
  setup_picked_identity(function(err, authModule) {
    do_check_eq(err, null);
    do_check_neq(authModule, null);

    let test_origin = "https://example.com";
    let test_message = "I like pie!";

    authModule.sign(test_origin, test_message, function(err, assertion) {
      do_check_eq(err, null);
      do_check_neq(assertion, null);

      saved_state.assertion = assertion;

      run_next_test();
    });
  });
});

/*
add_test(function test_check_assertion_success() {
  webrtc.createAuthModule({
    idp: "browserid.org",
    protocol: "persona"
  }, function(err, result) {
    do_check_eq(err, null);
    do_check_neq(result, null);

    result.verify({
      assertion:saved_state.assertion
    }, function(err, result) {
      do_check_eq(err, null);
      do_check_neq(result, null);
      do_check_eq(result.identity, "ben@adida.net");
      do_check_eq(result.message,"testmessage");

      run_next_test();
    });
  });
});
*/
function run_test() {
  run_next_test();
}
