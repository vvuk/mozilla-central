/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

function PeerConnection() {
  dump("!!!!!!! PeerConnection constructor called\n\n");
}
PeerConnection.prototype = {

  classID: Components.ID("{7cb2b368-b1ce-4560-acac-8e0dbda7d3d0}"),

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIDOMGlobalObjectConstructor,
                                         Ci.nsISupportsWeakReference,
                                         Ci.nsIDOMPeerConnection,
                                         Ci.nsIObserver]),

  _outerID: null,
  _innerID: null,

  init: function PC_init(win) {
    dump("!!!!!!! init called with " + win + "!!\n\n")
  },

  addStream: function PC_addStream(stream) {
    dump("!!!!!! PeerConnection.addStream called!!!!\n\n");
  }
};

let NSGetFactory = XPCOMUtils.generateNSGetFactory([PeerConnection]);
