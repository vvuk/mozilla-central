/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

XPCOMUtils.defineLazyModuleGetter(this, "IDService",
                                  "resource:///modules/identity/Identity.jsm",
                                  "IdentityService");

XPCOMUtils.defineLazyModuleGetter(this, "jwcrypto",
                                  "resource:///modules/identity/jwcrypto.jsm");

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
    doBeginAuthenticationCallback: function doBeginAuthenticationCallback(identity)    {
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

let TESTS = [];

TESTS.push(test_begin_authentication_flow);
TESTS.push(test_complete_authentication_flow);

TESTS.forEach(add_test);

function run_test() {
  run_next_test();
}
