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

XPCOMUtils.defineLazyServiceGetter(this,
                                   "uuidGenerator",
                                   "@mozilla.org/uuid-generator;1",
                                   "nsIUUIDGenerator");

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

function log(aMsg)
{
  dump("IDService-Testing: " + aMsg + "\n");
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
      do_check_neq(kpo, undefined);

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

function test_rsa()
{
  do_test_pending();
  makeObserver("id-service-key-gen-finished", function checkRSA(aSubject, aTopic, aData) {
    var kpo;
    // now we can pluck the keyPair from the store
    let key = JSON.parse(aData);
    kpo = IDService._getIdentityServiceKeyPair(key.userID, key.url);
    do_check_neq(kpo, undefined);
    do_check_eq(kpo.algorithm, ALGORITHMS.RS256);

    do_check_neq(kpo.sign, null);
    do_check_eq(typeof kpo.sign, "function");
    do_check_neq(kpo.userID, null);
    do_check_neq(kpo.url, null);
    do_check_eq(kpo.url, TEST_URL);
    do_check_neq(kpo.publicKey, null);
    do_check_neq(kpo.exponent, null);
    do_check_neq(kpo.modulus, null);
    
    // TODO: should sign be async?
    let sig = kpo.sign("This is a message to sign");
    
    do_check_neq(sig, null);
    do_check_eq(typeof sig, "string");
    do_check_true(sig.length > 1);
    
    do_test_finished();
    run_next_test();
  });
  
  IDService._generateKeyPair("RS256", TEST_URL, TEST_USER);
}

function test_dsa()
{
  do_test_pending();
  makeObserver("id-service-key-gen-finished", function checkDSA(aSubject, aTopic, aData) {
    var kpo;
    // now we can pluck the keyPair from the store
    let key = JSON.parse(aData);
    kpo = IDService._getIdentityServiceKeyPair(key.userID, key.url);
    do_check_neq(kpo, undefined);
    do_check_eq(kpo.algorithm, ALGORITHMS.DS160);

    do_check_neq(kpo.sign, null);
    do_check_eq(typeof kpo.sign, "function");
    do_check_neq(kpo.userID, null);
    do_check_neq(kpo.url, null);
    do_check_eq(kpo.url, TEST_URL2);
    do_check_neq(kpo.publicKey, null);
    do_check_neq(kpo.generator, null);
    do_check_neq(kpo.prime, null);
    do_check_neq(kpo.subPrime, null);
    
    let sig = kpo.sign("This is a message to sign");
    
    do_check_neq(sig, null);
    do_check_eq(typeof sig, "string");
    do_check_true(sig.length > 1);
    
    do_test_finished();
    run_next_test();
  });
  
  IDService._generateKeyPair("DS160", TEST_URL2, TEST_USER);
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
function setup_test_identity(cb)
{
  // set up the store so that we're supposed to be logged in
  IDService.reset();
  let store = get_idstore();
  
  makeObserver("id-service-key-gen-finished", function (aSubject, aTopic, aData) {
    let key = JSON.parse(aData);
    let kpo = IDService._getIdentityServiceKeyPair(key.userID, key.url);

    store.addIdentity(TEST_USER, kpo, "fake-cert");

    cb(TEST_USER);
  });

  IDService._generateKeyPair("DS160", TEST_URL, TEST_USER);
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

  setup_test_identity(function(id) {
    let store = get_idstore();
    
    // set it up so we're supposed to be logged in to TEST_URL
    store.setLoginState(TEST_URL, true, id);
    dump("mock dock with id " + id + "\n");
    IDService.watch(mock_doc(id, TEST_URL, function(action, params) {
				 dump("@@@ in ready callback\n");
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

  setup_test_identity(function(id) {
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

  setup_test_identity(function(id) {
    let store = get_idstore();
    
    // set it up so we're supposed to be logged in to TEST_URL
    store.setLoginState(TEST_URL, true, id);
    
    IDService.watch(mock_doc("otherid@foo.com", TEST_URL, call_sequentially(
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
    // this isn't going to be called for now.
  });
  
  IDService.watch(mockedDoc);

  // be ready for the UX identity-request notification
  makeObserver("identity-request", function (aSubject, aTopic, aData) {
    do_check_neq(aSubject, null);
    var subj = aSubject.QueryInterface(Ci.nsIPropertyBag);
    do_check_eq(subj.getProperty('requiredEmail'), TEST_USER);
    do_check_eq(subj.getProperty('requestID'), mockedDoc.id);
    
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
  
  setup_test_identity(function(id) {
    var gotAssertion = false;
    var mockedDoc = mock_doc(null, TEST_URL, call_sequentially(
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
      IDService.selectIdentity(aSubject.QueryInterface(Ci.nsIPropertyBag).getProperty('requestID'), id);
    });

    // do the request
    IDService.request(mockedDoc.id, {});
  });
}


function test_logout()
{
  do_test_pending();

  setup_test_identity(function(id) {
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
    cb: function(err) {
      if (doneProvisioningCallback)
        doneProvisioningCallback(err);
    },
    provisioningFrame: {}
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
	  dump("@@@ begin prov - gen key pair\n");
        IDService.genKeyPair(_callerId);
      },
      genKeyPairCallback: function(pk) {
	  dump("@@@ in gen key pair callback \n");
	  dump("kp = "+ JSON.stringify(pk));
        do_check_neq(pk, null);

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

  setup_provisioning(
    TEST_USER,
    function(provId) {
      // do the right thing for beginProvisioning
      IDService.beginProvisioning(provId);
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
        IDService.registerCertificate(provId, "fake-cert");
      }      
    }
  );  
}

function test_register_certificate()
{
  do_test_pending();

  setup_provisioning(
    TEST_USER,
    function(provId) {
      IDService.beginProvisioning(provId);
    },
    function(err) {
      // we should be cool!
      do_check_eq(err, null);

      check_provision_flow_done(provId);

      // check that the cert is there
      var identity = get_idstore().fetchIdentity(TEST_USER);
      do_check_neq(identity,null);
      do_check_eq(identity.cert, "fake-cert-42");

      do_test_finished();
      run_next_test();
    },
    {
      beginProvisioningCallback: function(email, duration_s) {
        IDService.genKeyPair(provId);
      },
      genKeyPairCallback: function(pk) {
        IDService.registerCertificate(provId, "fake-cert-42");
      }      
    }
  );  
  
}

var TESTS = [test_overall, test_rsa, test_dsa, test_id_store, test_mock_doc];
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
//TESTS.push(test_genkeypair);
//TESTS.push(test_register_certificate_before_genkeypair);
//TESTS.push(test_register_certificate);

TESTS.forEach(add_test);

function run_test()
{
  run_next_test();
}
