/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["Sandbox"];

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

/**
 * An object that represents a sandbox in an iframe loaded with ID_URI. The
 * callback provided to the constructor will be invoked when the sandbox is
 * ready to be used. The callback will receive this object as its only argument
 * and the prepared sandbox may be accessed via the "sandbox" property.
 *
 * Please call free() when you are finished with the sandbox to explicitely free
 * up all associated resources.
 *
 * @param cb
 *        (function) Callback to be invoked with a Sandbox, when ready.
 */
function Sandbox(cb) {
  this._createFrame();
  this._createSandbox(cb);
}
Sandbox.prototype = {
  /**
   * Frees the sandbox and releases the iframe created to host it.
   */
  free: function free() {
    delete this._sandbox;
    this._container.removeChild(this._frame);
    this._frame = null;
    this._container = null;
  },

  /**
   * Creates an empty, hidden iframe and sets it to the _iframe
   * property of this object.
   *
   * @return frame
   *         (iframe) An empty, hidden iframe
   */
  _createFrame: function _createFrame() {
    // TODO: What if there is no most recent browser window? (bug 745415).
    let doc = Services.wm.getMostRecentWindow("navigator:browser").document;

    // Insert iframe in to create docshell.
    let frame = doc.createElement("iframe");
    frame.setAttribute("type", "content");
    frame.setAttribute("collapsed", "true");
    doc.documentElement.appendChild(frame);

    // Stop about:blank from being loaded.
    let webNav = frame.docShell.QueryInterface(Ci.nsIWebNavigation);
    webNav.stop(Ci.nsIWebNavigation.STOP_NETWORK);

    // Set instance properties.
    this._frame = frame;
    this._container = doc.documentElement;
  },
  
  _createSandbox: function _createSandbox(cb) {
    let self = this;
    this._frame.addEventListener(
      "DOMContentLoaded",
      function _makeSandboxContentLoaded(event) {
        if (event.target.location.toString() != ID_URI) {
          return;
        }
        event.target.removeEventListener(
          "DOMContentLoaded", _makeSandboxContentLoaded, false
        );
        let workerWindow = self._frame.contentWindow;
        self.sandbox = new Cu.Sandbox(workerWindow, {
          wantXrays:        false,
          sandboxPrototype: workerWindow
        });
        cb(self);
      },
      true
    );

    // Load the iframe.
    this._frame.docShell.loadURI(
      ID_URI,
      this._frame.docShell.LOAD_FLAGS_NONE,
      null, // referrer
      null, // postData
      null  // headers
    );
  },
};