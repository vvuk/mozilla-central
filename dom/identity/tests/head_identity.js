/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const Ci = Components.interfaces;
const Cu = SpecialPowers.wrap(Components).utils;

SpecialPowers.setBoolPref("toolkit.identity.debug", true);

const Services = Cu.import("resource://gre/modules/Services.jsm").Services;
const DOMIdentity = Cu.import("resource://gre/modules/DOMIdentity.jsm")
                      .DOMIdentity;

let util = window.QueryInterface(Ci.nsIInterfaceRequestor)
                 .getInterface(Ci.nsIDOMWindowUtils);
let outerWinId = util.outerWindowID;

const identity = window.navigator.mozId;

let index = 0;

// mimicking callback funtionality for ease of testing
// this observer auto-removes itself after the observe function
// is called, so this is meant to observe only ONE event.
function makeObserver(aObserveTopic, aObserveFunc) {
  function observe(aSubject, aTopic, aData) {
    if (aTopic == aObserveTopic) {
      aObserveFunc(aSubject, aTopic, aData);
      Services.obs.removeObserver(this, aObserveTopic);
    }
  }

  Services.obs.addObserver(observe, aObserveTopic, false);
}

function expectException(aFunc, msg) {
  msg = msg || "";
  let caughtEx;
  try {
    aFunc();
  } catch (ex) {
    info("caught: " + ex);
    // Don't count NS_* exceptions since they shouldn't be exposed to content
    if (ex.toString().indexOf("NS_ERROR_FAILURE") == -1) {
      caughtEx = ex;
    } else {
      ok(false, ex);
    }
  }
  ok(caughtEx, "Check for thrown exception: " + msg);
}

function next() {
  if (index >= steps.length) {
    ok(false, "Shouldn't get here!");
    return;
  }
  try {
    let fn = steps[index];
    info("Begin test " + index + " '" + steps[index].name + "'!");
    fn();
  } catch(ex) {
    ok(false, "Caught exception", ex);
  }
  index += 1;
}

function finish_tests() {
  info("all done");
  SpecialPowers.clearUserPref("toolkit.identity.debug");
  SimpleTest.finish();
}
