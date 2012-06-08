/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// This has the side-effect of populating Cc, Ci, Cu, Cr. It's best not to
// ask questions and just accept it.
do_load_httpd_js();

// The following boilerplate makes sure that XPCom calls
// that use the profile directory work.

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

let XULAppInfo = {
  vendor: "Mozilla",
  name: "XPCShell",
  ID: "xpcshell@tests.mozilla.org",
  version: "1",
  appBuildID: "20100621",
  platformVersion: "",
  platformBuildID: "20100621",
  inSafeMode: false,
  logConsoleErrors: true,
  OS: "XPCShell",
  XPCOMABI: "noarch-spidermonkey",
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIXULAppInfo, Ci.nsIXULRuntime]),
  invalidateCachesOnRestart: function invalidateCachesOnRestart() { }
};

let XULAppInfoFactory = {
  createInstance: function (outer, iid) {
    if (outer != null)
      throw Cr.NS_ERROR_NO_AGGREGATION;
    return XULAppInfo.QueryInterface(iid);
  }
};

let registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);
registrar.registerFactory(Components.ID("{fbfae60b-64a4-44ef-a911-08ceb70b9f31}"),
                          "XULAppInfo", "@mozilla.org/xre/app-info;1",
                          XULAppInfoFactory);

// The following are utility functions for Identity testing

function log(aMsg)
{
  dump("ID Tests: " + aMsg + "\n");
}

// Switch debug messages on by default
Services.prefs.setBoolPref("toolkit.identity.debug", true);
