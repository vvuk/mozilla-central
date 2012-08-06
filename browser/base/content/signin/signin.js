/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource:///modules/SignInToBrowser.jsm");

// PAGES
const START_PAGE = 1;
const SIGNIN_PAGE = 2;
const SIGNUP_PAGE = 3;
const VERIFY_PAGE = 4;
const SUCCESS_PAGE = 5;


let formEl, nextEl, cancelEl, emailEl, emailFieldEl, accountListEl, passwordEl, passwordFieldEl,
    passwordConfirmEl, passwordConfirmFieldEl, forgotFieldEl, doneEl, signOutEl,
    verifyEl, successEl;
let page;

function onload() {
  formEl = document.getElementById("form");
  nextEl = document.getElementById("next");
  cancelEl = document.getElementById("cancel");
  emailEl = document.getElementById("email");
  emailFieldEl = document.getElementById("emailField");
  accountListEl = document.getElementById("accountList");
  passwordEl = document.getElementById("password");
  passwordFieldEl = document.getElementById("passwordField");
  passwordConfirmEl = document.getElementById("passwordConfirm");
  passwordConfirmFieldEl = document.getElementById("passwordConfirmField");
  forgotFieldEl = document.getElementById("forgotField");

  doneEl = document.getElementById("done");
  signOutEl = document.getElementById("signOut");
  verifyEl = document.getElementById("verify");
  successEl = document.getElementById("success");

  nextEl.addEventListener("click", onNextClick, false);
  cancelEl.addEventListener("click", onCancel, false);
  emailEl.addEventListener("input", onEmailEdit, false);
  emailEl.addEventListener("cut", onEmailEdit, false);
  signOutEl.addEventListener("click", onSignOut, false);
  doneEl.addEventListener("click", onDone, false);

  gotoPage(SignInToBrowser.signedIn ? SUCCESS_PAGE : SIGNIN_PAGE);
}

function onNextClick(aEvent) {
  if (!formEl.checkValidity())
    return false;

  switch(page) {
    // XXX invalid case now?
    case START_PAGE:
      // TODO: check validity of email address? use HTML5 form validation?
      // assume valid email address mmmmkkkkk
      // if (isUserEmail(emailEl.value)) {
      //   // go to SIGNIN_PAGE
      // }
      // else {
      //   // go to SIGNUP_PAGE
      // }
      SignInToBrowser.isUserEmail(emailEl.value) ? gotoPage(SIGNIN_PAGE)
                                                 : gotoPage(SIGNUP_PAGE);
      //gotoPage(BROWSERID_SERVICE.isUserEmail(emailEl.value) ? SIGNIN_PAGE : SIGNUP_PAGE);
      break;

    case SIGNIN_PAGE:
      if (SignInToBrowser.isUserEmail(emailEl.value)) {
        // TODO: more fail cases, async
        let userInfo = { email: emailEl.value, password: passwordEl.value };
        if (SignInToBrowser.signIn(userInfo)) {
          gotoPage(SUCCESS_PAGE);
        }
        else {
          alert("invalid password!");
          passwordEl.setAttribute("invalid", true);
          passwordEl.focus();
        }
      }
      else {
        gotoPage(SIGNUP_PAGE);
      }
      break;

    case SIGNUP_PAGE:
      // do something...
      // if (passwordEl.value) {
        if (passwordEl.value == passwordConfirmEl.value) {
          gotoPage(VERIFY_PAGE);
        }
        else {
          passwordEl.removeAttribute("invalid");
          alert("passwords don't match");
          passwordConfirmEl.setAttribute("invalid", true);
          passwordConfirmEl.focus();
        }
      // }
      // else {
      //   alert("need password");
      //   passwordEl.setAttribute("invalid", true);
      //   passwordEl.focus();
      // }
      break;
  }

}

function onCancel() {
  location.replace(location); // clear validity
}

function onDone() {
  //getBrowserWindow().gBrowser.removeCurrentTab();
  window.location = "about:home";
}

function onSignOut() {
  SignInToBrowser.signOut();
}

function onEmailEdit() {
  gotoPage(SIGNIN_PAGE);
}

function gotoPage(aPage, aOpts) {
  switch(aPage) {
    case START_PAGE:
      // code
      break;

    case SIGNIN_PAGE:
      if (accountListEl.options.length == 0) {
        for (let email of SignInToBrowser.localIdentities) {
          let option = document.createElement("option");
          option.value = email;
          accountListEl.appendChild(option);
        }
      }

      passwordFieldEl.hidden = false;
      passwordConfirmFieldEl.hidden = true;
      passwordConfirmFieldEl.required = false;
      forgotFieldEl.hidden = false;
      nextEl.value = "Next";
      emailEl.focus();
      break;

    case SIGNUP_PAGE:
      passwordFieldEl.hidden = false;
      passwordConfirmFieldEl.hidden = false;
      passwordConfirmFieldEl.required = true;
      passwordConfirmEl.focus();
      forgotFieldEl.hidden = true;
      nextEl.value = "Create Account";
      break;

    case VERIFY_PAGE:
      let els = [emailFieldEl, passwordFieldEl, passwordConfirmFieldEl, cancelEl, nextEl];
      els.forEach(function(el) {
        el.hidden = true;
      });

      verifyEl.hidden = false;
      setTimeout(function() {
        let userInfo = { email: emailEl.value, password: passwordEl.value };
        if (SignInToBrowser.register(userInfo))
          gotoPage(SUCCESS_PAGE);
        else
          passwordEl.setAttribute("invalid", "true");
      }, 5000);
      break;

    case SUCCESS_PAGE:
      formEl.hidden = true;
      successEl.hidden = false;

      let userEmail = SignInToBrowser.userInfo.email;
      if (userEmail)
        document.getElementById("successEmail").innerHTML = userEmail;
      doneEl.focus();
      break;
  }

  // TODO animate (css?)
  // if we haven't returned early, then it's ok to be on the next page
  page = aPage;

}

function getBrowserWindow() {
  return window.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIWebNavigation)
               .QueryInterface(Ci.nsIDocShellTreeItem).rootTreeItem
               .QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindow);
}

