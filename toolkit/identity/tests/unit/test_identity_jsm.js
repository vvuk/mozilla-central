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
function mock_doc(aOrigin, aDoFunc)
{
  var mockedDoc = {};
  mockedDoc.id = uuid();
  mockedDoc.origin = aOrigin;
  mockedDoc['do'] = aDoFunc;
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
  var mockedDoc = mock_doc(TEST_URL, function(action, params) {
    do_check_eq(action, 'fun');
    do_test_finished();
    run_next_test();
  });

  mockedDoc.do('fun');
}

function test_watch_loggedin_ready()
{
  do_test_pending();

  setup_test_identity(function(id) {
    let store = get_idstore();
    
    // set it up so we're supposed to be logged in to TEST_URL
    store.setLoginState(TEST_URL, true, id);
    
    IDService.watch(id, mock_doc(TEST_URL, function(action, params) {
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
    IDService.watch(null, mock_doc(TEST_URL, call_sequentially(
      function(action, params) {
        do_check_eq(action, 'login');
        do_check_neq(params, null);
        do_check_neq(params.assertion, null);
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
    
    IDService.watch("otherid@foo.com", mock_doc(TEST_URL, call_sequentially(
      function(action, params) {
        do_check_eq(action, 'login');
        do_check_neq(params, null);
        do_check_neq(params.assertion, null);
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

  IDService.watch(null, mock_doc(TEST_URL, function(action, params) {
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

  IDService.watch(TEST_USER, mock_doc(TEST_URL, call_sequentially(
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
  var mockedDoc = mock_doc(TEST_URL, function(action, params) {
    // this isn't going to be called for now.
  });
  
  IDService.watch(null, mockedDoc);

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
    var mockedDoc = mock_doc(TEST_URL, call_sequentially(
      // first the login call
      function(action, params) {
        do_check_eq(action, 'login');
        do_check_neq(params.assertion, null);

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
    IDService.watch(null, mockedDoc);

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
    var mockedDoc = mock_doc(TEST_URL, call_sequentially(
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
    
    IDService.watch(id, mockedDoc);
  });  
}

var TESTS = [test_overall, test_rsa, test_dsa, test_id_store, test_mock_doc];
TESTS = TESTS.concat([test_watch_loggedin_ready, test_watch_loggedin_login, test_watch_loggedin_logout]);
TESTS = TESTS.concat([test_watch_notloggedin_ready, test_watch_notloggedin_logout]);
TESTS.push(test_request);
TESTS.push(test_add_identity);
TESTS.push(test_select_identity);
TESTS.push(test_logout);

TESTS.forEach(add_test);

function run_test()
{
  run_next_test();
}
