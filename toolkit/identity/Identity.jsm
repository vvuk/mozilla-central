/* -*- Mode: js2; js2-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let Cu = Components.utils;
let Ci = Components.interfaces;
let Cc = Components.classes;
let Cr = Components.results;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/identity/Sandbox.jsm");

let EXPORTED_SYMBOLS = ["IdentityService"];
let FALLBACK_PROVIDER = "browserid.org";

const PREF_DEBUG = "toolkit.identity.debug";

XPCOMUtils.defineLazyServiceGetter(this,
                                   "IdentityCryptoService",
                                   "@mozilla.org/identity/crypto-service;1",
                                   "nsIIdentityCryptoService");

XPCOMUtils.defineLazyModuleGetter(this,
                                  "jwcrypto",
                                  "resource:///modules/identity/jwcrypto.jsm");

/**
 * log() - utility function to print a list of arbitrary things
 * Depends on IdentityService (bottom of this module).
 *
 * Enable with about:config pref toolkit.identity.debug
 */
function log(args) {
  if (!IdentityService._debugMode) {
    return;
  }

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
      try {
        strings.push(JSON.stringify(arg, null, 2));
      } catch(err) {
        strings.push("<<something>>");
      }
    }
  });
  let output = 'Identity: ' + strings.join(' ') + '\n';
  dump(output);

  // Additionally, make the output visible in the Error Console
  Services.console.logStringMessage(output);
};

function IDService() {
  Services.obs.addObserver(this, "quit-application-granted", false);
  // NB, prefs.addObserver and obs.addObserver have different interfaces
  Services.prefs.addObserver(PREF_DEBUG, this, false);

  this._debugMode = Services.prefs.getBoolPref(PREF_DEBUG);

  this.init();
}

IDService.prototype = {
  // DOM Methods.

  /**
   * Reset the state of the IDService object.
   */
  init: function init() {
    XPCOMUtils.defineLazyModuleGetter(this,
                                      "_store",
                                      "resource://gre/modules/identity/IdentityStore.jsm",
                                      "IdentityStore");

    XPCOMUtils.defineLazyModuleGetter(this,
                                      "RP",
                                      "resource://gre/modules/identity/RelyingParty.jsm",
                                      "RelyingParty");

    // Forget all identities
    this._store.init();

    // Clear RP state
    this.RP.init();

    // Clear the provisioning flows.  Provision flows contain an
    // identity, idpParams (how to reach the IdP to provision and
    // authenticate), a callback (a completion callback for when things
    // are done), and a provisioningFrame (which is the provisioning
    // sandbox).  Additionally, two callbacks will be attached:
    // beginProvisioningCallback and genKeyPairCallback.
    this._provisionFlows = {};

    // Clear the authentication flows.  Authentication flows attach
    // to provision flows.  In the process of provisioning an id, it
    // may be necessary to authenticate with an IdP.  The authentication
    // flow maintains the state of that authentication process.
    this._authenticationFlows = {};

    this._registry = {};
  },

  /**
   * The UX wants to add a new identity
   * often followed by selectIdentity()
   *
   * @param aIdentity
   *        (string) the email chosen for login
   */
  addIdentity: function addIdentity(aIdentity) {
    if (this._store.fetchIdentity(aIdentity) === null) {
      this._store.addIdentity(aIdentity, null, null);
    }
  },

  /**
   * The UX comes back and calls selectIdentity once the user has picked
   * an identity.
   *
   * @param aRPId
   *        (integer) the id of the doc object obtained in .watch() and
   *                  passed to the UX component.
   *
   * @param aIdentity
   *        (string) the email chosen for login
   */
  selectIdentity: function selectIdentity(aRPId, aIdentity) {
    log("selectIdentity: RP id:", aRPId, "identity:", aIdentity);

    let self = this;
    // Get the RP that was stored when watch() was invoked.
    let rp = this.RP._rpFlows[aRPId];
    if (!rp) {
      let errStr = "Cannot select identity for invalid RP with id: " + aRPId;
      log("ERROR: selectIdentity:", errStr);
      Cu.reportError(errStr);
      return null;
    }

    // It's possible that we are in the process of provisioning an
    // identity.
    let provId = rp.provId || null;

    // XXX consolidate rp, flows, etc. ?
    let rpLoginOptions = {
      loggedInEmail: aIdentity,
      origin: rp.origin
    };
    log("selectIdentity: provId:", provId, "origin:", rp.origin);

    // Once we have a cert, and once the user is authenticated with the
    // IdP, we can generate an assertion and deliver it to the doc.
    self.RP._generateAssertion(rp.origin, aIdentity, function(err, assertion) {
      if (!err && assertion) {
        self.RP._doLogin(rp, rpLoginOptions, assertion);
        return;

      } else {
        // Need to provision an identity first.  Begin by discovering
        // the user's IdP.
        self._discoverIdentityProvider(aIdentity, function(err, idpParams) {
          if (err) {
            rp.doError(err);
            return;
          }

          // The idpParams tell us where to go to provision and authenticate
          // the identity.
          self._provisionIdentity(aIdentity, idpParams, provId, function(err, aProvId) {

            // Provision identity may have created a new provision flow
            // for us.  To make it easier to relate provision flows with
            // RP callers, we cross index the two here.
            rp.provId = aProvId;
            self._provisionFlows[aProvId].rpId = aRPId;

            // At this point, we already have a cert.  If the user is also
            // already authenticated with the IdP, then we can try again
            // to generate an assertion and login.
            if (!err) {
              // XXX quick hack - cleanup is done by registerCertificate
              // XXX order of callbacks and signals is a little tricky
              //self._cleanUpProvisionFlow(aProvId);
              self.RP._generateAssertion(rp.origin, aIdentity, function(err, assertion) {
                if (!err) {
                  self.RP._doLogin(rp, rpLoginOptions, assertion);
                  return;
                } else {
                  rp.doError(err);
                  return;
                }
              });

            // We are not authenticated.  If we have already tried to
            // authenticate and failed, then this is a "hard fail" and
            // we give up.  Otherwise we try to authenticate with the
            // IdP.
            } else {
              if (self._provisionFlows[aProvId].didAuthentication) {
                self._cleanUpProvisionFlow(aProvId);
                log("ERROR: selectIdentity: authentication hard fail");
                rp.doError("Authentication fail.");
                return;

              } else {
                // Try to authenticate with the IdP.  Note that we do
                // not clean up the provision flow here.  We will continue
                // to use it.
                self._doAuthentication(aProvId, idpParams);
                return;
              }
            }
          });
        });
      }
    });
  },

  /**
   * Provision an Identity
   *
   * @param aIdentity
   *        (string) the email we're logging in with
   *
   * @param aIDPParams
   *        (object) parameters of the IdP
   *
   * @param aCallback
   *        (function) callback to invoke on completion
   *                   with first-positional parameter the error.
   */
  _provisionIdentity: function _provisionIdentity(aIdentity, aIDPParams, aProvId, aCallback) {
    let url = 'https://' + aIDPParams.domain + aIDPParams.idpParams.provisioning;
    log("_provisionIdentity: identity:", aIdentity, "url:", url);

    // If aProvId is not null, then we already have a flow
    // with a sandbox.  Otherwise, get a sandbox and create a
    // new provision flow.

    if (aProvId !== null) {
      // Re-use an existing sandbox
      log("_provisionIdentity: re-using sandbox in provisioning flow with id:", aProvId);
      this._provisionFlows[aProvId].provisioningSandbox.load();

    } else {
      this._createProvisioningSandbox(url, function(aSandbox) {
        // create a provisioning flow, using the sandbox id, and
        // stash callback associated with this provisioning workflow.

        let provId = aSandbox.id;
        this._provisionFlows[provId] = {
          identity: aIdentity,
          idpParams: aIDPParams,
          securityLevel: this.securityLevel,
          provisioningSandbox: aSandbox,
          callback: function doCallback(aErr) {
            aCallback(aErr, provId);
          },
        };

        log("_provisionIdentity: Created sandbox and provisioning flow with id:", aProvId);

        // XXX MAYBE
        // set a timeout to clear out this provisioning workflow if it doesn't
        // complete in X time.

      }.bind(this));
    }
  },

  /**
   * Discover the IdP for an identity
   *
   * @param aIdentity
   *        (string) the email we're logging in with
   *
   * @param aCallback
   *        (function) callback to invoke on completion
   *                   with first-positional parameter the error.
   */
  _discoverIdentityProvider: function _discoverIdentityProvider(aIdentity, aCallback) {
    let domain = aIdentity.split('@')[1];
    log("_discoverIdentityProvider: identity:", aIdentity, "domain:", domain);

    this._fetchWellKnownFile(domain, function(err, idpParams) {
      // idpParams includes the pk, authorization url, and
      // provisioning url.

      // XXX TODO follow any authority delegations
      // if no well-known at any point in the delegation
      // fall back to browserid.org as IdP

      // XXX TODO use email-specific delegation if IdP supports it
      // XXX TODO will need to update spec for that
      return aCallback(err, idpParams);
    });
  },

  /**
   * the provisioning iframe sandbox has called navigator.id.beginProvisioning()
   *
   * @param aCaller
   *        (object)  the iframe sandbox caller with all callbacks and
   *                  other information.  Callbacks include:
   *                  - doBeginProvisioningCallback(id, duration_s)
   *                  - doGenKeyPairCallback(pk)
   */
  beginProvisioning: function beginProvisioning(aCaller) {
    log("beginProvisioning:", aCaller.id);

    // Expect a flow for this caller already to be underway.
    let provFlow = this._provisionFlows[aCaller.id];
    if (!provFlow) {
      let errStr = "No provisioning flow found with id:" + aCaller.id;
      log("ERROR: beginProvisioning:", errStr);
      return aCaller.doError(errStr);
    }

    // keep the caller object around
    provFlow.caller = aCaller;

    let identity = provFlow.identity;
    let frame = provFlow.provisioningFrame;

    // Determine recommended length of cert.
    let duration = this.certDuration;

    // Make a record that we have begun provisioning.  This is required
    // for genKeyPair.
    provFlow.didBeginProvisioning = true;

    // Let the sandbox know to invoke the callback to beginProvisioning with
    // the identity and cert length.
    return aCaller.doBeginProvisioningCallback(identity, duration);
  },

  /**
   * the provisioning iframe sandbox has called
   * navigator.id.raiseProvisioningFailure()
   *
   * @param aProvId
   *        (int)  the identifier of the provisioning flow tied to that sandbox
   * @param aReason
   */
  raiseProvisioningFailure: function raiseProvisioningFailure(aProvId, aReason) {
    log("ERROR: Provisioning failure:", aReason);
    Cu.reportError("Provisioning failure: " + aReason);

    // look up the provisioning caller and its callback
    let provFlow = this._provisionFlows[aProvId];
    if (!provFlow) {
      let errStr = "No provisioning flow found with id:" + aProvId;
      log("ERROR: raiseProvisioningFailure:", errStr);
      Cu.reportError(errStr);
      return;
    }

    // Sandbox is deleted in _cleanUpProvisionFlow in case we re-use it.

    // This may be either a "soft" or "hard" fail.  If it's a
    // soft fail, we'll flow through setAuthenticationFlow, where
    // the provision flow data will be copied into a new auth
    // flow.  If it's a hard fail, then the callback will be
    // responsible for cleaning up the now defunct provision flow.

    // invoke the callback with an error.
    provFlow.callback(aReason);
  },

  /**
   * When navigator.id.genKeyPair is called from provisioning iframe sandbox.
   * Generates a keypair for the current user being provisioned.
   *
   * @param aProvId
   *        (int)  the identifier of the provisioning caller tied to that sandbox
   *
   * It is an error to call genKeypair without receiving the callback for
   * the beginProvisioning() call first.
   */
  genKeyPair: function genKeyPair(aProvId) {
    // Look up the provisioning caller and make sure it's valid.
    let provFlow = this._provisionFlows[aProvId];

    if (!provFlow) {
      log("ERROR: genKeyPair: no provisioning flow found with id:", aProvId);
      return null;
    }

    if (!provFlow.didBeginProvisioning) {
      let errStr = "ERROR: genKeyPair called before beginProvisioning";
      log(errStr);
      return provFlow.callback(errStr);
    }

    // Ok generate a keypair
    jwcrypto.generateKeyPair(jwcrypto.ALGORITHMS.DS160, function gkpCb(err, kp) {
      log("in gkp callback");
      if (err) {
        log("ERROR: genKeyPair:" + err);
        return provFlow.callback(err);
      }

      provFlow.kp = kp;

      // Serialize the publicKey of the keypair and send it back to the
      // sandbox.
      log("genKeyPair: generated keypair for provisioning flow with id:", aProvId);
      provFlow.caller.doGenKeyPairCallback(provFlow.kp.serializedPublicKey);
    }.bind(this));
  },

  /**
   * When navigator.id.registerCertificate is called from provisioning iframe
   * sandbox.
   *
   * Sets the certificate for the user for which a certificate was requested
   * via a preceding call to beginProvisioning (and genKeypair).
   *
   * @param aProvId
   *        (integer) the identifier of the provisioning caller tied to that
   *                  sandbox
   *
   * @param aCert
   *        (String)  A JWT representing the signed certificate for the user
   *                  being provisioned, provided by the IdP.
   */
  registerCertificate: function registerCertificate(aProvId, aCert) {
    log("registerCertificate:", aProvId, aCert);

    // look up provisioning caller, make sure it's valid.
    let provFlow = this._provisionFlows[aProvId];
    if (!provFlow && provFlow.caller) {
      let errStr = "Cannot register cert; No provision flow or caller";
      log("ERROR: registerCertificate:", errStr);
      Cu.reportError(errStr);

      // there is nobody to call back to
      return null;
    }
    if (!provFlow.kp)  {
      let errStr = "Cannot register a certificate without a keypair";
      log("ERROR: registerCertificate:", errStr);
      Cu.reportError(errStr);
      return provFlow.callback(errStr);
    }

    // store the keypair and certificate just provided in IDStore.
    this._store.addIdentity(provFlow.identity, provFlow.kp, aCert);

    // Great success!
    provFlow.callback(null);

    // Clean up the flow.
    this._cleanUpProvisionFlow(aProvId);
  },

  /**
   * Begin the authentication process with an IdP
   *
   * @param aProvId
   *        (int) the identifier of the provisioning flow which failed
   *
   * @param aCallback
   *        (function) to invoke upon completion, with
   *                   first-positional-param error.
   */
  _doAuthentication: function _doAuthentication(aProvId, aIDPParams) {
    // create an authentication caller and its identifier AuthId
    // stash aIdentity, idpparams, and callback in it.

    // extract authentication URL from idpParams

    // ? create a visible frame with sandbox and notify UX
    // or notify UX so it can create the visible frame, not sure which one.
    // TODO: make the two lines below into a helper to be used for auth and authentication
    let authPath = aIDPParams.idpParams.authentication;
    let authURI = Services.io.newURI("https://" + aIDPParams.domain, null, null).resolve(authPath);

    log("_doAuthentication: provId:", aProvId, "authURI:", authURI);
    // beginAuthenticationFlow causes the "identity-auth" topic to be
    // observed.  Since it's sending a notification to the DOM, there's
    // no callback.  We wait for the DOM to trigger the next phase of
    // provisioning.
    this._beginAuthenticationFlow(aProvId, authURI);

    // either we bind the AuthID to the sandbox ourselves, or UX does that,
    // in which case we need to tell UX the AuthId.
    // Currently, the UX creates the UI and gets the AuthId from the window
    // and sets is with setAuthenticationFlow
  },

  /**
   * The authentication frame has called navigator.id.beginAuthentication
   *
   * IMPORTANT: the aCaller is *always* non-null, even if this is called from
   * a regular content page. We have to make sure, on every DOM call, that
   * aCaller is an expected authentication-flow identifier. If not, we throw
   * an error or something.
   *
   * @param aCaller
   *        (object)  the authentication caller
   *
   */
  beginAuthentication: function beginAuthentication(aCaller) {
    log("beginAuthentication: caller id:", aCaller.id);

    // Begin the authentication flow after having concluded a provisioning
    // flow.  The aCaller that the DOM gives us will have the same ID as
    // the provisioning flow we just concluded.  (see setAuthenticationFlow)
    let authFlow = this._authenticationFlows[aCaller.id];
    if (!authFlow) {
      return aCaller.doError("beginAuthentication: no flow for caller id", aCaller.id);
    }

    // stash the caller in the flow
    // XXX do we need to do this?
    authFlow.caller = aCaller;

    let identity = this._provisionFlows[authFlow.provId].identity;

    // tell the UI to start the authentication process
    log("beginAuthentication: authFlow:", aCaller.id, "identity:", identity);
    return authFlow.caller.doBeginAuthenticationCallback(identity);
  },

  /**
   * The auth frame has called navigator.id.completeAuthentication
   *
   * @param aAuthId
   *        (int)  the identifier of the authentication caller tied to that sandbox
   *
   */
  completeAuthentication: function completeAuthentication(aAuthId) {
    log("completeAuthentication:", aAuthId);

    // look up the AuthId caller, and get its callback.
    let authFlow = this._authenticationFlows[aAuthId];
    if (!authFlow) {
      Cu.reportError("completeAuthentication: No auth flow with id " + aAuthId);
      return;
    }
    let provId = authFlow.provId;

    // delete caller
    delete authFlow['caller'];
    delete this._authenticationFlows[aAuthId];

    let provFlow = this._provisionFlows[provId];
    provFlow.didAuthentication = true;
    Services.obs.notifyObservers(null, "identity-auth-complete", aAuthId);

    // We have authenticated in order to provision an identity.
    // So try again.
    this.selectIdentity(provFlow.rpId, provFlow.identity);
  },

  /**
   * The auth frame has called navigator.id.cancelAuthentication
   *
   * @param aAuthId
   *        (int)  the identifier of the authentication caller
   *
   */
  cancelAuthentication: function cancelAuthentication(aAuthId) {
    log("cancelAuthentication:", aAuthId);

    // look up the AuthId caller, and get its callback.
    let authFlow = this._authenticationFlows[aAuthId];
    if (!authFlow) {
      Cu.reportError("cancelAuthentication: No auth flow with id " + aAuthId);
    }
    let provId = authFlow.provId;

    // delete caller
    delete authFlow['caller'];
    delete this._authenticationFlows[aAuthId];

    let provFlow = this._provisionFlows[provId];
    provFlow.didAuthentication = true;
    Services.obs.notifyObservers(null, "identity-auth-complete", aAuthId);

    // invoke callback with ERROR.
    let errStr = "Authentication canceled by IDP";
    log("ERROR: cancelAuthentication:", errStr);
    return provFlow.callback(errStr);
  },

  // methods for chrome and add-ons

  shutdown: function shutdown() {
    this.init();
  },

  /**
   * Called by the UI to set the ID and caller for the authentication flow after it gets its ID
   */
  setAuthenticationFlow: function(aAuthId, aProvId) {
    // this is the transition point between the two flows,
    // provision and authenticate.  We tell the auth flow which
    // provisioning flow it is started from.
    log("setAuthenticationFlow: authId:", aAuthId, "provId:", aProvId);
    this._authenticationFlows[aAuthId] = { provId: aProvId };
    this._provisionFlows[aProvId].authId = aAuthId;
  },

  get securityLevel() {
    return 1;
  },

  get certDuration() {
    switch(this.securityLevel) {
      default:
        return 3600;
    }
  },

  // TODO: need helper to logout of all sites for SITB?

  /**
   * Fetch the well-known file from the domain.
   *
   * @param aDomain
   *
   * @param aScheme
   *        (string) (optional) Protocol to use.  Default is https.
   *                 This is necessary because we are unable to test
   *                 https.
   *
   * @param aCallback
   *
   */
  _fetchWellKnownFile: function _fetchWellKnownFile(aDomain, aScheme, aCallback) {
    if (arguments.length <= 2) {
      aCallback = aScheme;
      aScheme = "https";
    }
    let url = aScheme + '://' + aDomain + "/.well-known/browserid";
    log("_fetchWellKnownFile:", url);

    // this appears to be a more successful way to get at xmlhttprequest (which supposedly will close with a window
    let req = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"]
                .getService(Ci.nsIXMLHttpRequest);

    // XXX how can we detect whether we are off-line?
    // TODO: decide on how to handle redirects
    req.open("GET", url, true);
    req.responseType = "json";
    req.mozBackgroundRequest = true;
    req.onload = function _fetchWellKnownFile_onload() {
      if (req.status < 200 || req.status >= 400)
        return aCallback(req.status);
      try {
        let idpParams = req.response;

        // Verify that the IdP returned a valid configuration
        if (! (idpParams.provisioning &&
            idpParams.authentication &&
            idpParams['public-key'])) {
          let errStr= "Invalid well-known file from: " + aDomain;
          log("_fetchWellKnownFile:", errStr);
          return aCallback(errStr);
        }

        let callbackObj = {
          domain: aDomain,
          idpParams: idpParams,
        };
        // Yay.  Valid IdP configuration for the domain.
        return aCallback(null, callbackObj);

      } catch (err) {
        let errStr = "Bad configuration from " + aDomain + ": " + err;
        Cu.reportError(errStr);
        log("ERROR: _fetchWellKnownFile:", errStr);
        return aCallback(err.toString());
      }
    };
    req.onerror = function _fetchWellKnownFile_onerror() {
      let err = "Failed to fetch well-known file";
      if (req.status) {
        err += " " + req.status + ":";
      }
      if (req.statusText) {
        err += " " + req.statusText;
      }
      log("ERROR: _fetchWellKnownFile:", err);
      return aCallback(err);
    };
    req.send(null);
  },

  /**
   * Load the provisioning URL in a hidden frame to start the provisioning
   * process.
   * TODO: CHANGE this call to be just _createSandbox, and do the population
   * of the flow object in _provisionIdentity instead, so that method has full
   * context.
   */
  _createProvisioningSandbox: function _createProvisioningSandbox(aURL, aCallback) {
    log("_createProvisioningSandbox:", aURL);
    new Sandbox(aURL, aCallback);
  },

  /**
   * Load the authentication UI to start the authentication process.
   */
  _beginAuthenticationFlow: function _beginAuthenticationFlow(aProvId, aURL) {
    log("_beginAuthenticationFlow:", aProvId, aURL);
    let propBag = Cc["@mozilla.org/hash-property-bag;1"]
                    .createInstance(Ci.nsIWritablePropertyBag);
    propBag.setProperty("provId", aProvId);

    Services.obs.notifyObservers(propBag, "identity-auth", aURL);
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports, Ci.nsIObserver]),

  observe: function observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "quit-application-granted":
        Services.obs.removeObserver(this, "quit-application-granted", false);
        this.shutdown();
        break;

      case "nsPref:changed":
        this._debugMode = Services.prefs.getBoolPref(PREF_DEBUG);
        break;
    }
  },

  /**
   * Clean up a provision flow and the authentication flow and sandbox
   * that may be attached to it.
   */
  _cleanUpProvisionFlow: function _cleanUpProvisionFlow(aProvId) {
    log('_cleanUpProvisionFlow:', aProvId);
    let prov = this._provisionFlows[aProvId];
    let rp = this.RP._rpFlows[prov.rpId];

    // Clean up the sandbox, if there is one.
    if (prov.provisioningSandbox) {
      let sandbox = this._provisionFlows[aProvId]['provisioningSandbox'];
      if (sandbox.free) {
        log('_cleanUpProvisionFlow: freeing sandbox');
        sandbox.free();
      }
      delete this._provisionFlows[aProvId]['provisioningSandbox'];
    }

    // Clean up a related authentication flow, if there is one.
    if (this._authenticationFlows[prov.authId]) {
      delete this._authenticationFlows[prov.authId];
    }

    // Finally delete the provision flow and any reference to it
    // from the rpFlows
    delete this._provisionFlows[aProvId];
    if (rp) {
      delete rp['provId'];
    }
  }
};

let IdentityService = new IDService();

