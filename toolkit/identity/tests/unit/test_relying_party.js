/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

XPCOMUtils.defineLazyModuleGetter(this, "IDService",
                                  "resource:///modules/identity/Identity.jsm",
                                  "IdentityService");


function test_watch_loggedin_ready() {
  do_test_pending();

  IDService.init();

  let id = TEST_USER;
  setup_test_identity(id, TEST_CERT, function() {
    let store = get_idstore();

    // set it up so we're supposed to be logged in to TEST_URL
    store.setLoginState(TEST_URL, true, id);
    IDService.watch(mock_doc(id, TEST_URL, function(action, params) {
      do_check_eq(action, 'ready');
      do_check_eq(params, undefined);

      do_test_finished();
      run_next_test();
    }));
  });
}

function test_watch_loggedin_login() {
  do_test_pending();

  IDService.init();

  let id = TEST_USER;
  setup_test_identity(id, TEST_CERT, function() {
    let store = get_idstore();

    // set it up so we're supposed to be logged in to TEST_URL
    store.setLoginState(TEST_URL, true, id);

    // check for first a login() call, then a ready() call
    IDService.watch(mock_doc(null, TEST_URL, call_sequentially(
      function(action, params) {
        do_check_eq(action, 'login');
        do_check_neq(params, null);
      },
      function(action, params) {
        do_check_eq(action, 'ready');
        do_check_eq(params, null);

        do_test_finished();
        run_next_test();
      }
    )));
  });
}

function test_watch_loggedin_logout() {
  do_test_pending();

  IDService.init();

  let id = TEST_USER;
  let other_id = "otherid@foo.com";
  setup_test_identity(other_id, TEST_CERT, function() {
    setup_test_identity(id, TEST_CERT, function() {
      let store = get_idstore();

      // set it up so we're supposed to be logged in to TEST_URL
      // with id, not other_id
      store.setLoginState(TEST_URL, true, id);

      // this should cause a login with an assertion for id,
      // not for other_id
      IDService.watch(mock_doc(other_id, TEST_URL, call_sequentially(
        function(action, params) {
          do_check_eq(action, 'login');
          do_check_neq(params, null);
        },
        function(action, params) {
          do_check_eq(action, 'ready');
          do_check_eq(params, null);

          do_test_finished();
          run_next_test();
        }
      )));
    });
  });
}

function test_watch_notloggedin_ready() {
  do_test_pending();

  IDService.init();

  IDService.watch(mock_doc(null, TEST_URL, function(action, params) {
    do_check_eq(action, 'ready');
    do_check_eq(params, undefined);

    do_test_finished();
    run_next_test();
  }));
}

function test_watch_notloggedin_logout() {
  do_test_pending();

  IDService.init();

  IDService.watch(mock_doc(TEST_USER, TEST_URL, call_sequentially(
    function(action, params) {
      do_check_eq(action, 'logout');
      do_check_eq(params, undefined);

      let store = get_idstore();
      do_check_null(store.getLoginState(TEST_URL));
    },
    function(action, params) {
      do_check_eq(action, 'ready');
      do_check_eq(params, undefined);
      do_test_finished();
      run_next_test();
    }
  )));
}

function test_request() {
  do_test_pending();

  // set up a watch, to be consistent
  let mockedDoc = mock_doc(null, TEST_URL, function(action, params) {
    // this isn't going to be called for now
    // XXX but it is called - is that bad?
  });

  IDService.watch(mockedDoc);

  // be ready for the UX identity-request notification
  makeObserver("identity-request", function (aSubject, aTopic, aData) {
    do_check_neq(aSubject, null);

    let subj = aSubject.QueryInterface(Ci.nsIPropertyBag);
    do_check_eq(subj.getProperty('requiredEmail'), TEST_USER);
    do_check_eq(subj.getProperty('rpId'), mockedDoc.id);

    do_test_finished();
    run_next_test();
  });

  IDService.request(mockedDoc.id, {requiredEmail: TEST_USER});
}

function test_logout() {
  do_test_pending();

  IDService.init();

  let id = TEST_USER;
  setup_test_identity(id, TEST_CERT, function() {
    let store = get_idstore();

    // set it up so we're supposed to be logged in to TEST_URL
    store.setLoginState(TEST_URL, true, id);

    let doLogout;
    let mockedDoc = mock_doc(id, TEST_URL, call_sequentially(
      function(action, params) {
        do_check_eq(action, 'ready');
        do_check_eq(params, undefined);

        do_timeout(100, doLogout);
      },
      function(action, params) {
        do_check_eq(action, 'logout');
        do_check_eq(params, undefined);

        do_timeout(100, doLogout);
      },
      function(action, params) {
        do_check_eq(action, 'ready');
        do_check_eq(params, undefined);

        do_test_finished();
        run_next_test();
      }));

    doLogout = function() {
      IDService.logout(mockedDoc.id);
      do_check_false(store.getLoginState(TEST_URL).isLoggedIn);
      do_check_eq(store.getLoginState(TEST_URL).email, TEST_USER);
    };

    IDService.watch(mockedDoc);
  });
}

let TESTS = [];

TESTS = TESTS.concat([test_watch_loggedin_ready, test_watch_loggedin_login, test_watch_loggedin_logout]);
TESTS = TESTS.concat([test_watch_notloggedin_ready, test_watch_notloggedin_logout]);
TESTS.push(test_request);

TESTS.push(test_logout);

TESTS.forEach(add_test);

function run_test() {
  run_next_test();
}
