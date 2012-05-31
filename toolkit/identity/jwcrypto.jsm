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

var EXPORTED_SYMBOLS = ["jwcrypto"];

function log(aMsg)
{
  dump("jwcrypto: " + aMsg + "\n");
}

var Base64 = {
  // private property
  _keyStr : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=",
  
  // public method for encoding
  encode : function (input) {
    var output = "";
    var chr1, chr2, chr3, enc1, enc2, enc3, enc4;
    var i = 0;
    
    input = Base64._utf8_encode(input);
    
    while (i < input.length) {
      
      chr1 = input.charCodeAt(i++);
      chr2 = input.charCodeAt(i++);
      chr3 = input.charCodeAt(i++);
      
      enc1 = chr1 >> 2;
      enc2 = ((chr1 & 3) << 4) | (chr2 >> 4);
      enc3 = ((chr2 & 15) << 2) | (chr3 >> 6);
      enc4 = chr3 & 63;
      
      if (isNaN(chr2)) {
        enc3 = enc4 = 64;
      } else if (isNaN(chr3)) {
        enc4 = 64;
      }
      
      output = output +
        Base64._keyStr.charAt(enc1) + Base64._keyStr.charAt(enc2) +
        Base64._keyStr.charAt(enc3) + Base64._keyStr.charAt(enc4);
      
    }
    
    return output;
  },
  
  // public method for decoding
  decode : function (input) {
    var output = "";
    var chr1, chr2, chr3;
    var enc1, enc2, enc3, enc4;
    var i = 0;
    
    input = input.replace(/[^A-Za-z0-9\+\/\=]/g, "");
    
    while (i < input.length) {
      
      enc1 = Base64._keyStr.indexOf(input.charAt(i++));
      enc2 = Base64._keyStr.indexOf(input.charAt(i++));
      enc3 = Base64._keyStr.indexOf(input.charAt(i++));
      enc4 = Base64._keyStr.indexOf(input.charAt(i++));
      
      chr1 = (enc1 << 2) | (enc2 >> 4);
      chr2 = ((enc2 & 15) << 4) | (enc3 >> 2);
      chr3 = ((enc3 & 3) << 6) | enc4;
      
      output = output + String.fromCharCode(chr1);
      
      if (enc3 != 64) {
        output = output + String.fromCharCode(chr2);
      }
      if (enc4 != 64) {
        output = output + String.fromCharCode(chr3);
      }
      
    }
    
    output = Base64._utf8_decode(output);
    
    return output;
    
  },
  
  // private method for UTF-8 encoding
  _utf8_encode : function (string) {
    string = string.replace(/\r\n/g,"\n");
    var utftext = "";
    
    for (var n = 0; n < string.length; n++) {
      
      var c = string.charCodeAt(n);
      
      if (c < 128) {
        utftext += String.fromCharCode(c);
      }
      else if((c > 127) && (c < 2048)) {
        utftext += String.fromCharCode((c >> 6) | 192);
        utftext += String.fromCharCode((c & 63) | 128);
      }
      else {
        utftext += String.fromCharCode((c >> 12) | 224);
        utftext += String.fromCharCode(((c >> 6) & 63) | 128);
        utftext += String.fromCharCode((c & 63) | 128);
      }
      
    }
    
    return utftext;
  },
  
  // private method for UTF-8 decoding
  _utf8_decode : function (utftext) {
    var string = "";
    var i = 0;
    var c = c1 = c2 = 0;
    
    while ( i < utftext.length ) {
      
      c = utftext.charCodeAt(i);
      
      if (c < 128) {
        string += String.fromCharCode(c);
        i++;
      }
      else if((c > 191) && (c < 224)) {
        c2 = utftext.charCodeAt(i+1);
        string += String.fromCharCode(((c & 31) << 6) | (c2 & 63));
        i += 2;
      }
      else {
        c2 = utftext.charCodeAt(i+1);
        c3 = utftext.charCodeAt(i+2);
        string += String.fromCharCode(((c & 15) << 12) | ((c2 & 63) << 6) | (c3 & 63));
        i += 3;
      }
      
    }
    return string;
  }
};

function base64urlencode(arg) {
  var s = Base64.encode(arg);
  s = s.split('=')[0]; // Remove any trailing '='s
  s = s.replace(/\+/g, '-'); // 62nd char of encoding
  s = s.replace(/\//g, '_'); // 63rd char of encoding
  return s;
}

function base64urldecode(arg) {
  var s = arg;
  s = s.replace(/-/g, '+'); // 62nd char of encoding
  s = s.replace(/_/g, '/'); // 63rd char of encoding
  switch (s.length % 4) // Pad with trailing '='s
  {
  case 0: break; // No pad chars in this case
  case 2: s += "=="; break; // Two pad chars
  case 3: s += "="; break; // One pad char
  default: throw new InputException("Illegal base64url string!");
  }
  return Base64.decode(s); // Standard base64 decoder
}

function jwcryptoClass()
{
}

jwcryptoClass.prototype = {
  isCertValid: function(aCert, aCallback) {
    // XXX check expiration
    aCallback(true);
  },
  
  generateAssertion: function(aCert, aKeyPair, aAudience, aCallback) {
    // var header = {"alg": aKeyPair.algorithm};
    // for now, we hack the algorithm name
    var header = {"alg": "DS160"};
    var headerBytes = base64urlencode(JSON.stringify(header));

    var payload = {
      // expires in 2 minutes
      exp: new Date(new Date().valueOf() + (2 * 60 * 1000)).valueOf(),
      aud: aAudience
    };
    var jsonBytes = base64urlencode(JSON.stringify(payload));
    
    var signature = aKeyPair.sign(headerBytes + "." + jsonBytes);

    // append to cert
    var assertion = headerBytes + "." + jsonBytes + "." + signature;
    var fullAssertion = aCert + "~" + assertion;
    aCallback(null, fullAssertion);
  }
  
};

var jwcrypto = new jwcryptoClass();
