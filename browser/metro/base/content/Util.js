/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let Util = {
  /*
   * General purpose utilities
   */

  getWindowUtils: function getWindowUtils(aWindow) {
    return aWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
  },

  // Recursively find all documents, including root document.
  getAllDocuments: function getAllDocuments(doc, resultSoFar) {
    resultSoFar = resultSoFar || [doc];
    if (!doc.defaultView)
      return resultSoFar;
    let frames = doc.defaultView.frames;
    if (!frames)
      return resultSoFar;

    let i;
    let currentDoc;
    for (i = 0; i < frames.length; i++) {
      currentDoc = frames[i].document;
      resultSoFar.push(currentDoc);
      this.getAllDocuments(currentDoc, resultSoFar);
    }

    return resultSoFar;
  },

  // Put the Mozilla networking code into a state that will kick the
  // auto-connection process.
  forceOnline: function forceOnline() {
    Services.io.offline = false;
  },

  /*
   * Timing utilties
   */

  // Executes aFunc after other events have been processed.
  executeSoon: function executeSoon(aFunc) {
    Services.tm.mainThread.dispatch({
      run: function() {
        aFunc();
      }
    }, Ci.nsIThread.DISPATCH_NORMAL);
  },

  /*
   * Console printing utilities
   */

  dumpf: function dumpf(str) {
    let args = arguments;
    let i = 1;
    dump(str.replace(/%s/g, function() {
      if (i >= args.length) {
        throw "dumps received too many placeholders and not enough arguments";
      }
      return args[i++].toString();
    }));
  },

  // Like dump, but each arg is handled and there's an automatic newline
  dumpLn: function dumpLn() {
    for (let i = 0; i < arguments.length; i++)
      dump(arguments[i] + " ");
    dump("\n");
  },

  dumpElement: function dumpElement(aElement) {
    this.dumpLn(aElement.id);
  },

  dumpElementTree: function dumpElementTree(aElement) {
    let node = aElement;
    while (node) {
      this.dumpLn("node:", node, "id:", node.id, "class:", node.classList);
      node = node.parentNode;
    }
  },

  /*
   * Element utilities
   */

  highlightElement: function highlightElement(aElement) {
    if (aElement == null) {
      this.dumpLn("aElement is null");
      return;
    }
    aElement.style.border = "2px solid red";
  },

  getHrefForElement: function getHrefForElement(target) {
    let link = null;
    while (target) {
      if (target instanceof Ci.nsIDOMHTMLAnchorElement || 
          target instanceof Ci.nsIDOMHTMLAreaElement ||
          target instanceof Ci.nsIDOMHTMLLinkElement) {
          if (target.hasAttribute("href"))
            link = target;
      }
      target = target.parentNode;
    }

    if (link && link.hasAttribute("href"))
      return link.href;
    else
      return null;
  },

  /*
   * Rect and nsIDOMRect utilities
   */

  pointWithinRect: function pointWithinRect(aX, aY, aRect) {
    return (aRect.left < aX && aRect.top < aY &&
            aRect.right > aX && aRect.bottom > aY);
  },

  pointWithinDOMRect: function pointWithinDOMRect(aX, aY, aRect) {
    if (!aRect.width || !aRect.height)
      return false;
    return this.pointWithinRect(aX, aY, aRect);
  },

  isEmptyDOMRect: function isEmptyDOMRect(aRect) {
    if ((aRect.bottom - aRect.top) <= 0 &&
        (aRect.right - aRect.left) <= 0)
      return true;
    return false;
  },

  // Dumps the details of a dom rect to the console
  dumpDOMRect: function dumpDOMRect(aMsg, aRect) {
    try {
      Util.dumpLn(aMsg,
                  "left:" + Math.round(aRect.left) + ",",
                  "top:" + Math.round(aRect.top) + ",",
                  "right:" + Math.round(aRect.right) + ",",
                  "bottom:" + Math.round(aRect.bottom) + ",",
                  "width:" + Math.round(aRect.right - aRect.left) + ",",
                  "height:" + Math.round(aRect.bottom - aRect.top) );
    } catch (ex) {
      Util.dumpLn("dumpDOMRect:", ex.message);
    }
  },

  /*
   * URIs and schemes
   */

  makeURI: function makeURI(aURL, aOriginCharset, aBaseURI) {
    return Services.io.newURI(aURL, aOriginCharset, aBaseURI);
  },

  makeURLAbsolute: function makeURLAbsolute(base, url) {
    // Note:  makeURI() will throw if url is not a valid URI
    return this.makeURI(url, null, this.makeURI(base)).spec;
  },

  isLocalScheme: function isLocalScheme(aURL) {
    return ((aURL.indexOf("about:") == 0 &&
             aURL != "about:blank" &&
             aURL != "about:empty" &&
             aURL != "about:start") ||
            aURL.indexOf("chrome:") == 0);
  },

  isOpenableScheme: function isShareableScheme(aProtocol) {
    let dontOpen = /^(mailto|javascript|news|snews)$/;
    return (aProtocol && !dontOpen.test(aProtocol));
  },

  isShareableScheme: function isShareableScheme(aProtocol) {
    let dontShare = /^(chrome|about|file|javascript|resource)$/;
    return (aProtocol && !dontShare.test(aProtocol));
  },

  // Don't display anything in the urlbar for these special URIs.
  isURLEmpty: function isURLEmpty(aURL) {
    return (!aURL ||
            aURL == "about:blank" ||
            aURL == "about:empty" ||
            aURL == "about:home" ||
            aURL == "about:start");
  },

  // Don't remember these pages in the session store.
  isURLMemorable: function isURLMemorable(aURL) {
    return !(aURL == "about:blank" ||
             aURL == "about:empty" ||
             aURL == "about:start");
  },

  /*
   * Math utilities
   */

  clamp: function(num, min, max) {
    return Math.max(min, Math.min(max, num));
  },

  /*
   * Screen and layout utilities
   */

  get displayDPI() {
    delete this.displayDPI;
    return this.displayDPI = this.getWindowUtils(window).displayDPI;
  },

  isPortrait: function isPortrait() {
    return (window.innerWidth <= window.innerHeight);
  },

  LOCALE_DIR_RTL: -1,
  LOCALE_DIR_LTR: 1,
  get localeDir() {
    // determine browser dir first to know which direction to snap to
    let chromeReg = Cc["@mozilla.org/chrome/chrome-registry;1"].getService(Ci.nsIXULChromeRegistry);
    return chromeReg.isLocaleRTL("global") ? this.LOCALE_DIR_RTL : this.LOCALE_DIR_LTR;
  },

  /*
   * Process utilities
   */

  isParentProcess: function isInParentProcess() {
    let appInfo = Cc["@mozilla.org/xre/app-info;1"];
    return (!appInfo || appInfo.getService(Ci.nsIXULRuntime).processType == Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT);
  },

  /*
   * Event utilities
   */

  modifierMaskFromEvent: function modifierMaskFromEvent(aEvent) {
    return (aEvent.altKey   ? Ci.nsIDOMEvent.ALT_MASK     : 0) |
           (aEvent.ctrlKey  ? Ci.nsIDOMEvent.CONTROL_MASK : 0) |
           (aEvent.shiftKey ? Ci.nsIDOMEvent.SHIFT_MASK   : 0) |
           (aEvent.metaKey  ? Ci.nsIDOMEvent.META_MASK    : 0);
  },

  /*
   * Download utilities
   */

  insertDownload: function insertDownload(aSrcUri, aFile) {
    let dm = Cc["@mozilla.org/download-manager;1"].getService(Ci.nsIDownloadManager);
    let db = dm.DBConnection;

    let stmt = db.createStatement(
      "INSERT INTO moz_downloads (name, source, target, startTime, endTime, state, referrer) " +
      "VALUES (:name, :source, :target, :startTime, :endTime, :state, :referrer)"
    );

    stmt.params.name = aFile.leafName;
    stmt.params.source = aSrcUri.spec;
    stmt.params.target = aFile.path;
    stmt.params.startTime = Date.now() * 1000;
    stmt.params.endTime = Date.now() * 1000;
    stmt.params.state = Ci.nsIDownloadManager.DOWNLOAD_NOTSTARTED;
    stmt.params.referrer = aSrcUri.spec;

    stmt.execute();
    stmt.finalize();

    let newItemId = db.lastInsertRowID;
    let download = dm.getDownload(newItemId);
    //dm.resumeDownload(download);
    //Services.obs.notifyObservers(download, "dl-start", null);
  },

  /*
   * Local system utilities
   */

  createShortcut: function Util_createShortcut(aTitle, aURL, aIconURL, aType) {
    // The background images are 72px, but Android will resize as needed.
    // Bigger is better than too small.
    const kIconSize = 72;
    const kOverlaySize = 32;
    const kOffset = 20;

    // We have to fallback to something
    aTitle = aTitle || aURL;

    let canvas = document.createElementNS("http://www.w3.org/1999/xhtml", "canvas");
    canvas.setAttribute("style", "display: none");

    function _createShortcut() {
      let icon = canvas.toDataURL("image/png", "");
      canvas = null;
      try {
        let shell = Cc["@mozilla.org/browser/shell-service;1"].createInstance(Ci.nsIShellService);
        shell.createShortcut(aTitle, aURL, icon, aType);
      } catch(e) {
        Cu.reportError(e);
      }
    }

    // Load the main background image first
    let image = new Image();
    image.onload = function() {
      canvas.width = canvas.height = kIconSize;
      let ctx = canvas.getContext("2d");
      ctx.drawImage(image, 0, 0, kIconSize, kIconSize);

      // If we have a favicon, lets draw it next
      if (aIconURL) {
        let favicon = new Image();
        favicon.onload = function() {
          // Center the favicon and overlay it on the background
          ctx.drawImage(favicon, kOffset, kOffset, kOverlaySize, kOverlaySize);
          _createShortcut();
        }

        favicon.onerror = function() {
          Cu.reportError("CreateShortcut: favicon image load error");
        }

        favicon.src = aIconURL;
      } else {
        _createShortcut();
      }
    }

    image.onerror = function() {
      Cu.reportError("CreateShortcut: background image load error");
    }

    // Pick the right background
    image.src = aIconURL ? "chrome://browser/skin/images/homescreen-blank-hdpi.png"
                         : "chrome://browser/skin/images/homescreen-default-hdpi.png";
  },
};


/*
 * Timeout
 *
 * Helper class to nsITimer that adds a little more pizazz.  Callback can be an
 * object with a notify method or a function.
 */
Util.Timeout = function(aCallback) {
  this._callback = aCallback;
  this._timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  this._type = null;
};

Util.Timeout.prototype = {
  // Timer callback. Don't call this manually.
  notify: function notify() {
    if (this._type == this._timer.TYPE_ONE_SHOT)
      this._type = null;

    if (this._callback.notify)
      this._callback.notify();
    else
      this._callback.apply(null);
  },

  // Helper function for once and interval.
  _start: function _start(aDelay, aType, aCallback) {
    if (aCallback)
      this._callback = aCallback;
    this.clear();
    this._timer.initWithCallback(this, aDelay, aType);
    this._type = aType;
    return this;
  },

  // Do the callback once.  Cancels other timeouts on this object.
  once: function once(aDelay, aCallback) {
    return this._start(aDelay, this._timer.TYPE_ONE_SHOT, aCallback);
  },

  // Do the callback every aDelay msecs. Cancels other timeouts on this object.
  interval: function interval(aDelay, aCallback) {
    return this._start(aDelay, this._timer.TYPE_REPEATING_SLACK, aCallback);
  },

  // Clear any pending timeouts.
  clear: function clear() {
    if (this.isPending()) {
      this._timer.cancel();
      this._type = null;
    }
    return this;
  },

  // If there is a pending timeout, call it and cancel the timeout.
  flush: function flush() {
    if (this.isPending()) {
      this.notify();
      this.clear();
    }
    return this;
  },

  // Return true if we are waiting for a callback. 
  isPending: function isPending() {
    return this._type !== null;
  }
};

