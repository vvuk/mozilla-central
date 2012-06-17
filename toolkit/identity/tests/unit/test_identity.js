/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

XPCOMUtils.defineLazyModuleGetter(this, "IDService",
                                  "resource:///modules/identity/Identity.jsm",
                                  "IdentityService");

XPCOMUtils.defineLazyModuleGetter(this, "jwcrypto",
                                  "resource:///modules/identity/jwcrypto.jsm");

function test_overall() {
  do_check_neq(IDService, null);
  run_next_test();
}

// set up the ID service with an identity with keypair and all
// when ready, invoke callback with the identity
function setup_test_identity(identity, cert, cb) {
  // set up the store so that we're supposed to be logged in
  let store = get_idstore();

  function keyGenerated(err, kpo) {
    store.addIdentity(identity, kpo, cert);
    cb();
  };

  jwcrypto.generateKeyPair("DS160", keyGenerated);
}

// create a mock "doc" object, which the Identity Service
// uses as a pointer back into the doc object
function mock_doc(aIdentity, aOrigin, aDoFunc) {
  let mockedDoc = {};
  mockedDoc.id = uuid();
  mockedDoc.loggedInEmail = aIdentity;
  mockedDoc.origin = aOrigin;
  mockedDoc['do'] = aDoFunc;
  mockedDoc.doReady = partial(aDoFunc, 'ready');
  mockedDoc.doLogin = partial(aDoFunc, 'login');
  mockedDoc.doLogout = partial(aDoFunc, 'logout');
  mockedDoc.doError = partial(aDoFunc, 'error');
  mockedDoc.doCancel = partial(aDoFunc, 'cancel');
  mockedDoc.doCoffee = partial(aDoFunc, 'coffee');

  return mockedDoc;
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

function test_add_identity() {
  IDService.init();

  IDService.addIdentity(TEST_USER);

  let identities = IDService.getIdentitiesForSite(TEST_URL);
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
	do_check_eq(params, null);
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
        do_check_eq(params, null);

        // we should have gotten the assertion already
        do_check_true(gotAssertion);

        do_test_finished();
        run_next_test();
      }));

    // register the callbacks
    IDService.watch(mockedDoc);

    // register the request UX observer
    makeObserver("identity-request", function (aSubject, aTopic, aData) {
      // do the select identity
      // we expect this to succeed right away because of test_identity
      // so we don't mock network requests or otherwise
      IDService.selectIdentity(aSubject.QueryInterface(Ci.nsIPropertyBag).getProperty('rpId'), id);
    });

    // do the request
    IDService.request(mockedDoc.id, {});
  });
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

function test_get_assertion_after_provision() {
  do_test_pending();
  let _callerId = null;

  setup_provisioning(
    TEST_USER,
    function(caller) {
      _callerId = caller.id;
      IDService.beginProvisioning(caller);
    },
    function(err) {
      // we should be cool!
      do_check_eq(err, null);

      check_provision_flow_done(_callerId);

      // check that the cert is there
      let identity = get_idstore().fetchIdentity(TEST_USER);
      do_check_neq(identity,null);
      do_check_eq(identity.cert, "fake-cert-42");

      do_test_finished();
      run_next_test();
    },
    {
      beginProvisioningCallback: function(email, duration_s) {
        IDService.genKeyPair(_callerId);
      },
      genKeyPairCallback: function(pk) {
        IDService.registerCertificate(_callerId, "fake-cert-42");
      }
    }
  );

}

function test_begin_authentication_flow() {
  do_test_pending();
  let _provId = null;

  // set up a watch, to be consistent
  let mockedDoc = mock_doc(null, TEST_URL, function(action, params) {});
  IDService.watch(mockedDoc);

  // The identity-auth notification is sent up to the UX from the
  // _doAuthentication function.  Be ready to receive it and call
  // beginAuthentication
  makeObserver("identity-auth", function (aSubject, aTopic, aData) {
    do_check_neq(aSubject, null);

    let subj = aSubject.QueryInterface(Ci.nsIPropertyBag);
    do_check_eq(subj.getProperty('provId'), _provId);

    do_test_finished();
    run_next_test();
  });

  setup_provisioning(
    TEST_USER,
    function(caller) {
      _provId = caller.id;
      IDService.beginProvisioning(caller);
    }, function() {},
    {
      beginProvisioningCallback: function(email, duration_s) {
	// let's say this user needs to authenticate
	IDService._doAuthentication(_provId, {idpParams:TEST_IDPPARAMS});
      }
    });
}

function test_complete_authentication_flow() {
  do_test_pending();
  let _provId = null;
  let _authId = null;
  let id = TEST_USER;

  let callbacksFired = false;
  let topicObserved = false;

  // The result of authentication should be a successful login
  IDService.init();

  setup_test_identity(id, TEST_CERT, function() {
    // set it up so we're supposed to be logged in to TEST_URL
    get_idstore().setLoginState(TEST_URL, true, id);

    // When we authenticate, our ready callback will be fired.
    // At the same time, a separate topic will be sent up to the
    // the observer in the UI.  The test is complete when both
    // events have occurred.
    let mockedDoc = mock_doc(id, TEST_URL, call_sequentially(
      function(action, params) {
        do_check_eq(action, 'ready');
        do_check_eq(params, undefined);

	// if notification already received by observer, test is done
	callbacksFired = true;
	if (topicObserved) {
	  do_test_finished();
          run_next_test();
	}
      }
    ));

    makeObserver("identity-login-state-changed", function (aSubject, aTopic, aData) {
      do_check_neq(aSubject, null);

      let subj = aSubject.QueryInterface(Ci.nsIPropertyBag);
      do_check_eq(subj.getProperty('rpId'), mockedDoc.id);
      do_check_eq(aData, id);

      // if callbacks in caller doc already fired, test is done.
      topicObserved = true;
      if (callbacksFired) {
	do_test_finished();
	run_next_test();
      }
     });

    IDService.watch(mockedDoc);
  });

  // A mock calling contxt
  let authCaller = {
    doBeginAuthenticationCallback: function doBeginAuthenticationCallback(identity) {
      do_check_eq(identity, TEST_USER);

      IDService.completeAuthentication(_authId);
    },

    doError: function(err) {
      log("OW! My doError callback hurts!", err);
    },
  };

  // Create a provisioning flow for our auth flow to attach to
  setup_provisioning(
    TEST_USER,
    function(provFlow) {
      _provId = provFlow.id;

      IDService.beginProvisioning(provFlow);
    }, function() {},
    {
      beginProvisioningCallback: function(email, duration_s) {
	// let's say this user needs to authenticate
	IDService._doAuthentication(_provId, {idpParams:TEST_IDPPARAMS});

	// test_begin_authentication_flow verifies that the right
	// message is sent to the UI.  So that works.  Moving on,
	// the UI calls setAuthenticationFlow ...
	_authId = uuid();
	IDService.setAuthenticationFlow(_authId, _provId);

	// ... then the UI calls beginAuthentication ...
	authCaller.id = _authId;
	IDService._provisionFlows[_provId].caller = authCaller;
	IDService.beginAuthentication(authCaller);
      }
    });
}

let TESTS = [test_overall, test_mock_doc];

TESTS = TESTS.concat([test_watch_loggedin_ready, test_watch_loggedin_login, test_watch_loggedin_logout]);
TESTS = TESTS.concat([test_watch_notloggedin_ready, test_watch_notloggedin_logout]);
TESTS.push(test_request);
TESTS.push(test_add_identity);
TESTS.push(test_select_identity);

TESTS.push(test_logout);

// authentication tests
TESTS.push(test_begin_authentication_flow);
TESTS.push(test_complete_authentication_flow);

TESTS.forEach(add_test);

function run_test() {
  run_next_test();
}
