/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

XPCOMUtils.defineLazyModuleGetter(this, "IDService",
                                  "resource:///modules/identity/Identity.jsm",
                                  "IdentityService");

function check_provision_flow_done(provId) {
  do_check_null(IDService._provisionFlows[provId]);
}

function test_begin_provisioning() {
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

function test_raise_provisioning_failure() {
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

function test_genkeypair_before_begin_provisioning() {
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

function test_genkeypair() {
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
function test_register_certificate_before_genkeypair() {
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

function test_register_certificate() {
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
      do_check_null(err);

      // XXX this will happen after the callback is called
      //
      //check_provision_flow_done(_callerId);

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
      do_check_null(err);

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

let TESTS = [];

TESTS.push(test_begin_provisioning);
TESTS.push(test_raise_provisioning_failure);
TESTS.push(test_genkeypair_before_begin_provisioning);
TESTS.push(test_genkeypair);
TESTS.push(test_register_certificate_before_genkeypair);
TESTS.push(test_register_certificate);
TESTS.push(test_get_assertion_after_provision);

function run_test() {
  run_next_test();
}
