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

// delay the loading of the IDService for performance purposes
XPCOMUtils.defineLazyGetter(this, "jwcrypto", function (){
  let scope = {};
  Cu.import("resource:///modules/identity/bidbundle.jsm", scope);
  return scope.require("./lib/jwcrypto");
});

XPCOMUtils.defineLazyServiceGetter(this,
                                   "uuidGenerator",
                                   "@mozilla.org/uuid-generator;1",
                                   "nsIUUIDGenerator");

var INTERNAL_ORIGIN = "browserid://";

function partial(fn) {
  var args = Array.prototype.slice.call(arguments, 1);
  return function() {
    return fn.apply(this, args.concat(Array.prototype.slice.call(arguments)));
  };
}

function uuid()
{
  return uuidGenerator.generateUUID();
}

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

const TEST_URL = "https://myfavoritebacon.com";
const TEST_URL2 = "https://myfavoritebaconinacan.com";
const TEST_USER = "user@mozilla.com";
const TEST_PRIVKEY = "fake-privkey";
const TEST_CERT = "fake-cert";

const ALGORITHMS = { RS256: 1, DS160: 2, };

// mimicking callback funtionality for ease of testing
// this observer auto-removes itself after the observe function
// is called, so this is meant to observe only ONE event.
function makeObserver(aObserveTopic, aObserveFunc)
{
  let observer = {
    // nsISupports provides type management in C++
    // nsIObserver is to be an observer
    QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports, Ci.nsIObserver]),
    
    observe: function (aSubject, aTopic, aData)
    {
      if (aTopic == aObserveTopic) {
        aObserveFunc(aSubject, aTopic, aData);
        Services.obs.removeObserver(observer, aObserveTopic);
      }
    }
  };

  Services.obs.addObserver(observer, aObserveTopic, false);
}

function test_rsa()
{
  do_test_pending();
  function checkRSA(err, key) {
    var kpo;
    // now we can pluck the keyPair from the store
    kpo = IDService._getIdentityKeyPair(key.userID, key.url);
    do_check_neq(kpo, undefined);
    do_check_eq(kpo.algorithm, ALGORITHMS.RS256);

    do_check_neq(kpo.sign, null);
    do_check_eq(typeof kpo.sign, "function");
    do_check_neq(kpo.userID, null);
    do_check_neq(kpo.url, null);
    do_check_eq(kpo.url, INTERNAL_ORIGIN);
    do_check_neq(kpo.exponent, null);
    do_check_neq(kpo.modulus, null);
    
    // TODO: should sign be async?
    let sig = kpo.sign("This is a message to sign");
    
    do_check_neq(sig, null);
    do_check_eq(typeof sig, "string");
    do_check_true(sig.length > 1);
    
    do_test_finished();
    run_next_test();
  };
  
  IDService._generateKeyPair("RS256", INTERNAL_ORIGIN, TEST_USER, checkRSA);
}

function test_dsa()
{
  do_test_pending();
  function checkDSA(err, key) {
    var kpo;
    // now we can pluck the keyPair from the store
    kpo = IDService._getIdentityKeyPair(key.userID, key.url);
    do_check_neq(kpo, undefined);
    do_check_eq(kpo.algorithm, ALGORITHMS.DS160);

    do_check_neq(kpo.sign, null);
    do_check_eq(typeof kpo.sign, "function");
    do_check_neq(kpo.userID, null);
    do_check_neq(kpo.url, null);
    do_check_eq(kpo.url, INTERNAL_ORIGIN);
    do_check_neq(kpo.generator, null);
    do_check_neq(kpo.prime, null);
    do_check_neq(kpo.subPrime, null);
    do_check_neq(kpo.publicValue, null);
    
    let sig = kpo.sign("This is a message to sign");
    
    do_check_neq(sig, null);
    do_check_eq(typeof sig, "string");
    do_check_true(sig.length > 1);
    
    do_test_finished();
    run_next_test();
  };
  
  IDService._generateKeyPair("DS160", INTERNAL_ORIGIN, TEST_USER, checkDSA);
}

function test_overall()
{
  do_check_neq(IDService, null);
  run_next_test();
}

function get_idstore()
{
  return IDService._store;
}

function test_id_store()
{
  // XXX - this is ugly, peaking in like this into IDService
  // probably should instantiate our own.
  var store = get_idstore();

  // try adding an identity
  store.addIdentity(TEST_USER, TEST_PRIVKEY, TEST_CERT);
  do_check_neq(store.getIdentities()[TEST_USER], null);
  do_check_eq(store.getIdentities()[TEST_USER].cert, TEST_CERT);

  // does fetch identity work?
  do_check_neq(store.fetchIdentity(TEST_USER), null);
  do_check_eq(store.fetchIdentity(TEST_USER).cert, TEST_CERT);

  // clear the cert should keep the identity but not the cert
  store.clearCert(TEST_USER);
  do_check_neq(store.getIdentities()[TEST_USER], null);
  do_check_eq(store.getIdentities()[TEST_USER].cert, null);
  
  // remove it should remove everything
  store.removeIdentity(TEST_USER);
  do_check_eq(store.getIdentities()[TEST_USER], undefined);

  // act like we're logged in to TEST_URL
  store.setLoginState(TEST_URL, true, TEST_USER);
  do_check_neq(store.getLoginState(TEST_URL), null);
  do_check_true(store.getLoginState(TEST_URL).isLoggedIn);
  do_check_eq(store.getLoginState(TEST_URL).email, TEST_USER);

  // log out
  store.setLoginState(TEST_URL, false, TEST_USER);
  do_check_neq(store.getLoginState(TEST_URL), null);
  do_check_false(store.getLoginState(TEST_URL).isLoggedIn);

  // email is still set
  do_check_eq(store.getLoginState(TEST_URL).email, TEST_USER);

  // not logged into other site
  do_check_eq(store.getLoginState(TEST_URL2), null);

  // clear login state
  store.clearLoginState(TEST_URL);
  do_check_eq(store.getLoginState(TEST_URL), null);
  do_check_eq(store.getLoginState(TEST_URL2), null);
  
  run_next_test();
}

// set up the ID service with an identity with keypair and all
// when ready, invoke callback with the identity
function setup_test_identity(identity, cert, cb)
{
  // set up the store so that we're supposed to be logged in
  let store = get_idstore();
  
  function keyGenerated(err, key) {
    log("keyGenerated");
    let kpo = IDService._getIdentityKeyPair(key.userID, key.url);

    store.addIdentity(identity, kpo, cert);
    cb();
  };

  log("setup_test_identity");
  IDService._generateKeyPair("DS160", INTERNAL_ORIGIN, identity, keyGenerated);
}

// create a mock "doc" object, which the Identity Service
// uses as a pointer back into the doc object
function mock_doc(aIdentity, aOrigin, aDoFunc)
{
  var mockedDoc = {};
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

// takes a list of functions and returns a function that
// when called the first time, calls the first func,
// then the next time the second, etc.
function call_sequentially()
{
  var numCalls = 0;
  var funcs = arguments;

  return function() {
    funcs[numCalls].apply(funcs[numCalls],arguments);
    numCalls += 1;
  };
}

function test_mock_doc()
{
  do_test_pending();
  var mockedDoc = mock_doc(null, TEST_URL, function(action, params) {
    do_check_eq(action, 'coffee');
    do_test_finished();
    run_next_test();
  });

  mockedDoc.doCoffee();
}

function test_watch_loggedin_ready()
{
  do_test_pending();

  IDService.reset();
  
  var id = TEST_USER;
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

function test_watch_loggedin_login()
{
  do_test_pending();

  IDService.reset();
  
  var id = TEST_USER;
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

function test_watch_loggedin_logout()
{
  do_test_pending();

  IDService.reset();
  
  var id = TEST_USER;
  var other_id = "otherid@foo.com";
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

function test_watch_notloggedin_ready()
{
  do_test_pending();

  IDService.reset();

  IDService.watch(mock_doc(null, TEST_URL, function(action, params) {
    do_check_eq(action, 'ready');
    do_check_eq(params, undefined);
    
    do_test_finished();
    run_next_test();
  }));
}

function test_watch_notloggedin_logout()
{
  do_test_pending();

  IDService.reset();

  IDService.watch(mock_doc(TEST_USER, TEST_URL, call_sequentially(
    function(action, params) {
      do_check_eq(action, 'ready');
      do_check_eq(params, undefined);
    },
    function(action, params) {
      do_check_eq(action, 'logout');
      do_check_eq(params, undefined);
      do_test_finished();
      run_next_test();
    }
  )));
}


function test_request()
{
  do_test_pending();
  
  // set up a watch, to be consistent
  var mockedDoc = mock_doc(null, TEST_URL, function(action, params) {
    // this isn't going to be called for now
    // XXX but it is called - is that bad?
  });
  
  IDService.watch(mockedDoc);

  // be ready for the UX identity-request notification
  makeObserver("identity-request", function (aSubject, aTopic, aData) {
    do_check_neq(aSubject, null);

    var subj = aSubject.QueryInterface(Ci.nsIPropertyBag);
    do_check_eq(subj.getProperty('requiredEmail'), TEST_USER);
    do_check_eq(subj.getProperty('rpId'), mockedDoc.id);
    
    do_test_finished();
    run_next_test();
  });

  IDService.request(mockedDoc.id, {requiredEmail: TEST_USER});
}

function test_add_identity()
{
  IDService.reset();

  IDService.addIdentity(TEST_USER);

  var identities = IDService.getIdentitiesForSite(TEST_URL);
  do_check_eq(identities.result.length, 1);
  do_check_eq(identities.result[0], TEST_USER);

  run_next_test();
}

function test_select_identity()
{
  do_test_pending();

  IDService.reset();

  var id = "ishtar@mockmyid.com";
  setup_test_identity(id, TEST_CERT, function() {
    var gotAssertion = false;
    var mockedDoc = mock_doc(null, TEST_URL, call_sequentially(
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


function test_logout()
{
  do_test_pending();

  IDService.reset();
  
  var id = TEST_USER;
  setup_test_identity(id, TEST_CERT, function() {
    let store = get_idstore();
    
    // set it up so we're supposed to be logged in to TEST_URL
    store.setLoginState(TEST_URL, true, id);

    var doLogout;
    var mockedDoc = mock_doc(id, TEST_URL, call_sequentially(
      function(action, params) {
        do_check_eq(action, 'ready');
        do_check_eq(params, undefined);

        do_timeout(100, doLogout);
      },
      function(action, params) {
        do_check_eq(action, 'ready');
        do_check_eq(params, undefined);

        do_timeout(100, doLogout);
      },
      function(action, params) {
        do_check_eq(action, 'logout');
        do_check_eq(params, undefined);
        
        do_test_finished();
        run_next_test();
      }));

    doLogout = function() {
      IDService.logout(mockedDoc.id);
    };
    
    IDService.watch(mockedDoc);
  });  
}

/*
 * Setup a provisioning workflow with appropriate callbacks
 *
 * identity is the email we're provisioning.
 * 
 * afterSetupCallback is required.
 *
 * doneProvisioningCallback is optional, if the caller
 * wants to be notified when the whole provisioning workflow is done
 *
 * frameCallbacks is optional, contains the callbacks that the sandbox
 * frame would provide in response to DOM calls.
 */
function setup_provisioning(identity, afterSetupCallback, doneProvisioningCallback, callerCallbacks)
{
  IDService.reset();
  
  var provId = uuid();
  IDService._provisionFlows[provId] = {
    identity : identity,
    idpParams: {},
    callback: function(err) {
      if (doneProvisioningCallback)
        doneProvisioningCallback(err);
    },
    sandbox: {
	// Emulate the free() method on the iframe sandbox
	free: function() {}
    }
  };

  var caller = {};
  caller.id = provId;
  caller.doBeginProvisioningCallback = function(id, duration_s) {
    if (callerCallbacks && callerCallbacks.beginProvisioningCallback)
      callerCallbacks.beginProvisioningCallback(id, duration_s);
  };
  caller.doGenKeyPairCallback = function(pk) {
    if (callerCallbacks && callerCallbacks.genKeyPairCallback)
      callerCallbacks.genKeyPairCallback(pk);
  };
  
  log("afterSetupCallback(caller); " + caller);
  afterSetupCallback(caller);
}

function check_provision_flow_done(provId)
{
  do_check_eq(IDService._provisionFlows[provId], null);
}

function test_begin_provisioning()
{
  do_test_pending();
  
  setup_provisioning(
    TEST_USER,
    function(caller) {
      // call .beginProvisioning()
      IDService.beginProvisioning(caller);
    }, function() {},
    {
      beginProvisioningCallback: function(email, duration_s) {
        do_check_eq(email, TEST_USER);
        do_check_true(duration_s > 0);
        do_check_true(duration_s <= (24 * 3600));

        do_test_finished();
        run_next_test();
      }
    });
}

function test_raise_provisioning_failure()
{
  do_test_pending();
  let _callerId = null;

  setup_provisioning(
    TEST_USER,
    function(caller) {
      // call .beginProvisioning()
      _callerId = caller.id;
      IDService.beginProvisioning(caller);
    }, function(err) {
      // this should be invoked with a populated error
      do_check_neq(err, null);
      do_check_true(err.indexOf("can't authenticate this email") > -1);
      
      do_test_finished();
      run_next_test();
    },
    {
      beginProvisioningCallback: function(email, duration_s) {
        // raise the failure as if we can't provision this email
        IDService.raiseProvisioningFailure(_callerId, "can't authenticate this email");
      }
    });
}

function test_genkeypair_before_begin_provisioning()
{
  do_test_pending();

  setup_provisioning(
    TEST_USER,
    function(caller) {
      // call genKeyPair without beginProvisioning
      IDService.genKeyPair(caller.id);
    },
    // expect this to be called with an error
    function(err) {
      do_check_neq(err, null);

      do_test_finished();
      run_next_test();
    },
    {
      // this should not be called at all!
      genKeyPairCallback: function(pk) {
        // a test that will surely fail because we shouldn't be here.
        do_check_true(false);

        do_test_finished();
        run_next_test();
      }
    }
  );
}

function test_genkeypair()
{
  do_test_pending();
  let _callerId = null;

  setup_provisioning(
    TEST_USER,
    function(caller) {
      log("caller " + _callerId);
      _callerId = caller.id;
      IDService.beginProvisioning(caller);
    },
    function(err) {
      // should not be called!
      do_check_true(false);

      do_test_finished();
      run_next_test();
    },
    {
      beginProvisioningCallback: function(email, time_s) {
        log("whatever " + _callerId);
        IDService.genKeyPair(_callerId);
      },
      genKeyPairCallback: function(kp) {
        do_check_neq(kp, null);

        // yay!
        do_test_finished();
        run_next_test();
      }
    }
  );  
}

// we've already ensured that genkeypair can't be called
// before beginProvisioning, so this test should be enough
// to ensure full sequential call of the 3 APIs.
function test_register_certificate_before_genkeypair()
{
  do_test_pending();
  let _callerID = null;

  setup_provisioning(
    TEST_USER,
    function(caller) {
      // do the right thing for beginProvisioning
      _callerID = caller.id;
      IDService.beginProvisioning(caller);
    },
    // expect this to be called with an error
    function(err) {
      do_check_neq(err, null);

      do_test_finished();
      run_next_test();
    },
    {
      beginProvisioningCallback: function(email, duration_s) {
        // now we try to register cert but no keygen has been done
        IDService.registerCertificate(_callerID, "fake-cert");
      }      
    }
  );  
}

function test_register_certificate()
{
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

      // XXX this will happen after the callback is called
      // 
      //check_provision_flow_done(_callerId);

      // check that the cert is there
      var identity = get_idstore().fetchIdentity(TEST_USER);
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

function test_get_assertion_after_provision()
{
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
      var identity = get_idstore().fetchIdentity(TEST_USER);
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


function test_jwcrypto()
{
  do_test_pending();

  jwcrypto.generateKeypair({algorithm: "DS", keysize: 128}, function(err, kp) {
    do_check_eq(err, null);
    

    do_test_finished();
    run_next_test();
  });
}

var TESTS = [test_overall, test_id_store, test_mock_doc];

// no test_rsa, test_dsa for now
//TESTS = TESTS.concat([test_rsa, test_dsa]);

TESTS.push(test_jwcrypto);


TESTS = TESTS.concat([test_watch_loggedin_ready, test_watch_loggedin_login, test_watch_loggedin_logout]);
TESTS = TESTS.concat([test_watch_notloggedin_ready, test_watch_notloggedin_logout]);
TESTS.push(test_request);
TESTS.push(test_add_identity);
TESTS.push(test_select_identity);

TESTS.push(test_logout);

// provisioning tests
TESTS.push(test_begin_provisioning);
TESTS.push(test_raise_provisioning_failure);
TESTS.push(test_genkeypair_before_begin_provisioning);
TESTS.push(test_genkeypair);
TESTS.push(test_register_certificate_before_genkeypair);
TESTS.push(test_register_certificate);

TESTS.forEach(add_test);

function run_test()
{
  run_next_test();
}
