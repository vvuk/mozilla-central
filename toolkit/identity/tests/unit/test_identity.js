/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

XPCOMUtils.defineLazyModuleGetter(this, "IDService",
                                  "resource:///modules/identity/Identity.jsm",
                                  "IdentityService");

function test_overall() {
  do_check_neq(IDService, null);
  run_next_test();
}

function test_mock_doc() {
  do_test_pending();
  let mockedDoc = mock_doc(null, TEST_URL, function(action, params) {
    do_check_eq(action, 'coffee');
    do_test_finished();
    run_next_test();
  });

  mockedDoc.doCoffee();
}

function test_add_identity() {
  IDService.init();

  IDService.addIdentity(TEST_USER);

  let identities = IDService.RP.getIdentitiesForSite(TEST_URL);
  do_check_eq(identities.result.length, 1);
  do_check_eq(identities.result[0], TEST_USER);

  run_next_test();
}

function test_select_identity() {
  do_test_pending();

  IDService.init();

  let id = "ishtar@mockmyid.com";
  setup_test_identity(id, TEST_CERT, function() {
    let gotAssertion = false;
    let mockedDoc = mock_doc(null, TEST_URL, call_sequentially(
      function(action, params) {
        // ready emitted from first watch() call
	do_check_eq(action, 'ready');
	do_check_null(params);
      },
      // first the login call
      function(action, params) {
        do_check_eq(action, 'login');
        do_check_neq(params, null);

        // XXX - check that the assertion is for the right email

        gotAssertion = true;
      },
      // then the ready call
      function(action, params) {
        do_check_eq(action, 'ready');
        do_check_null(params);

        // we should have gotten the assertion already
        do_check_true(gotAssertion);

        do_test_finished();
        run_next_test();
      }));

    // register the callbacks
    IDService.RP.watch(mockedDoc);

    // register the request UX observer
    makeObserver("identity-request", function (aSubject, aTopic, aData) {
      // do the select identity
      // we expect this to succeed right away because of test_identity
      // so we don't mock network requests or otherwise
      IDService.selectIdentity(aSubject.wrappedJSObject.rpId, id);
    });

    // do the request
    IDService.RP.request(mockedDoc.id, {});
  });
}

let TESTS = [test_overall, test_mock_doc];

TESTS.push(test_add_identity);
TESTS.push(test_select_identity);

TESTS.forEach(add_test);

function run_test() {
  run_next_test();
}
