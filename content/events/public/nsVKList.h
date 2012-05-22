/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This header file defines all DOM keys which are defined in nsIDOMKeyEvent.
 * You must define NS_DEFINE_VK macro before including this.
 *
 * It must have two arguments, (aDOMKeyName, aDOMKeyCode)
 * aDOMKeyName is a key name in DOM.
 * aDOMKeyCode is one of nsIDOMKeyEvent::DOM_VK_*.
 */

#define DEFINE_VK_INTERNAL(aKeyName) \
  NS_DEFINE_VK(VK##aKeyName, nsIDOMKeyEvent::DOM_VK##aKeyName)

// Some keycode may have different name in nsIDOMKeyEvent from its key name.
#define DEFINE_VK_INTERNAL2(aKeyName, aKeyCodeName) \
  NS_DEFINE_VK(VK##aKeyName, nsIDOMKeyEvent::DOM_VK##aKeyCodeName)

DEFINE_VK_INTERNAL(_CANCEL),
DEFINE_VK_INTERNAL(_HELP),
DEFINE_VK_INTERNAL2(_BACK, _BACK_SPACE),
DEFINE_VK_INTERNAL(_TAB),
DEFINE_VK_INTERNAL(_CLEAR),
DEFINE_VK_INTERNAL(_RETURN),
DEFINE_VK_INTERNAL(_ENTER),
DEFINE_VK_INTERNAL(_SHIFT),
DEFINE_VK_INTERNAL(_CONTROL),
DEFINE_VK_INTERNAL(_ALT),
DEFINE_VK_INTERNAL(_PAUSE),
DEFINE_VK_INTERNAL(_CAPS_LOCK),
DEFINE_VK_INTERNAL(_KANA),
DEFINE_VK_INTERNAL(_HANGUL),
DEFINE_VK_INTERNAL(_EISU),
DEFINE_VK_INTERNAL(_JUNJA),
DEFINE_VK_INTERNAL(_FINAL),
DEFINE_VK_INTERNAL(_HANJA),
DEFINE_VK_INTERNAL(_KANJI),
DEFINE_VK_INTERNAL(_ESCAPE),
DEFINE_VK_INTERNAL(_CONVERT),
DEFINE_VK_INTERNAL(_NONCONVERT),
DEFINE_VK_INTERNAL(_ACCEPT),
DEFINE_VK_INTERNAL(_MODECHANGE),
DEFINE_VK_INTERNAL(_SPACE),
DEFINE_VK_INTERNAL(_PAGE_UP),
DEFINE_VK_INTERNAL(_PAGE_DOWN),
DEFINE_VK_INTERNAL(_END),
DEFINE_VK_INTERNAL(_HOME),
DEFINE_VK_INTERNAL(_LEFT),
DEFINE_VK_INTERNAL(_UP),
DEFINE_VK_INTERNAL(_RIGHT),
DEFINE_VK_INTERNAL(_DOWN),
DEFINE_VK_INTERNAL(_SELECT),
DEFINE_VK_INTERNAL(_PRINT),
DEFINE_VK_INTERNAL(_EXECUTE),
DEFINE_VK_INTERNAL(_PRINTSCREEN),
DEFINE_VK_INTERNAL(_INSERT),
DEFINE_VK_INTERNAL(_DELETE),

DEFINE_VK_INTERNAL(_0),
DEFINE_VK_INTERNAL(_1),
DEFINE_VK_INTERNAL(_2),
DEFINE_VK_INTERNAL(_3),
DEFINE_VK_INTERNAL(_4),
DEFINE_VK_INTERNAL(_5),
DEFINE_VK_INTERNAL(_6),
DEFINE_VK_INTERNAL(_7),
DEFINE_VK_INTERNAL(_8),
DEFINE_VK_INTERNAL(_9),

DEFINE_VK_INTERNAL(_COLON),
DEFINE_VK_INTERNAL(_SEMICOLON),
DEFINE_VK_INTERNAL(_LESS_THAN),
DEFINE_VK_INTERNAL(_EQUALS),
DEFINE_VK_INTERNAL(_GREATER_THAN),
DEFINE_VK_INTERNAL(_QUESTION_MARK),
DEFINE_VK_INTERNAL(_AT),

DEFINE_VK_INTERNAL(_A),
DEFINE_VK_INTERNAL(_B),
DEFINE_VK_INTERNAL(_C),
DEFINE_VK_INTERNAL(_D),
DEFINE_VK_INTERNAL(_E),
DEFINE_VK_INTERNAL(_F),
DEFINE_VK_INTERNAL(_G),
DEFINE_VK_INTERNAL(_H),
DEFINE_VK_INTERNAL(_I),
DEFINE_VK_INTERNAL(_J),
DEFINE_VK_INTERNAL(_K),
DEFINE_VK_INTERNAL(_L),
DEFINE_VK_INTERNAL(_M),
DEFINE_VK_INTERNAL(_N),
DEFINE_VK_INTERNAL(_O),
DEFINE_VK_INTERNAL(_P),
DEFINE_VK_INTERNAL(_Q),
DEFINE_VK_INTERNAL(_R),
DEFINE_VK_INTERNAL(_S),
DEFINE_VK_INTERNAL(_T),
DEFINE_VK_INTERNAL(_U),
DEFINE_VK_INTERNAL(_V),
DEFINE_VK_INTERNAL(_W),
DEFINE_VK_INTERNAL(_X),
DEFINE_VK_INTERNAL(_Y),
DEFINE_VK_INTERNAL(_Z),

DEFINE_VK_INTERNAL(_WIN),
DEFINE_VK_INTERNAL(_CONTEXT_MENU),
DEFINE_VK_INTERNAL(_SLEEP),

DEFINE_VK_INTERNAL(_NUMPAD0),
DEFINE_VK_INTERNAL(_NUMPAD1),
DEFINE_VK_INTERNAL(_NUMPAD2),
DEFINE_VK_INTERNAL(_NUMPAD3),
DEFINE_VK_INTERNAL(_NUMPAD4),
DEFINE_VK_INTERNAL(_NUMPAD5),
DEFINE_VK_INTERNAL(_NUMPAD6),
DEFINE_VK_INTERNAL(_NUMPAD7),
DEFINE_VK_INTERNAL(_NUMPAD8),
DEFINE_VK_INTERNAL(_NUMPAD9),
DEFINE_VK_INTERNAL(_MULTIPLY),
DEFINE_VK_INTERNAL(_ADD),
DEFINE_VK_INTERNAL(_SEPARATOR),
DEFINE_VK_INTERNAL(_SUBTRACT),
DEFINE_VK_INTERNAL(_DECIMAL),
DEFINE_VK_INTERNAL(_DIVIDE),

DEFINE_VK_INTERNAL(_F1),
DEFINE_VK_INTERNAL(_F2),
DEFINE_VK_INTERNAL(_F3),
DEFINE_VK_INTERNAL(_F4),
DEFINE_VK_INTERNAL(_F5),
DEFINE_VK_INTERNAL(_F6),
DEFINE_VK_INTERNAL(_F7),
DEFINE_VK_INTERNAL(_F8),
DEFINE_VK_INTERNAL(_F9),
DEFINE_VK_INTERNAL(_F10),
DEFINE_VK_INTERNAL(_F11),
DEFINE_VK_INTERNAL(_F12),
DEFINE_VK_INTERNAL(_F13),
DEFINE_VK_INTERNAL(_F14),
DEFINE_VK_INTERNAL(_F15),
DEFINE_VK_INTERNAL(_F16),
DEFINE_VK_INTERNAL(_F17),
DEFINE_VK_INTERNAL(_F18),
DEFINE_VK_INTERNAL(_F19),
DEFINE_VK_INTERNAL(_F20),
DEFINE_VK_INTERNAL(_F21),
DEFINE_VK_INTERNAL(_F22),
DEFINE_VK_INTERNAL(_F23),
DEFINE_VK_INTERNAL(_F24),

DEFINE_VK_INTERNAL(_NUM_LOCK),
DEFINE_VK_INTERNAL(_SCROLL_LOCK),

DEFINE_VK_INTERNAL(_CIRCUMFLEX),
DEFINE_VK_INTERNAL(_EXCLAMATION),
DEFINE_VK_INTERNAL(_DOUBLE_QUOTE),
DEFINE_VK_INTERNAL(_HASH),
DEFINE_VK_INTERNAL(_DOLLAR),
DEFINE_VK_INTERNAL(_PERCENT),
DEFINE_VK_INTERNAL(_AMPERSAND),
DEFINE_VK_INTERNAL(_UNDERSCORE),
DEFINE_VK_INTERNAL(_OPEN_PAREN),
DEFINE_VK_INTERNAL(_CLOSE_PAREN),
DEFINE_VK_INTERNAL(_ASTERISK),
DEFINE_VK_INTERNAL(_PLUS),
DEFINE_VK_INTERNAL(_PIPE),
DEFINE_VK_INTERNAL(_HYPHEN_MINUS),

DEFINE_VK_INTERNAL(_OPEN_CURLY_BRACKET),
DEFINE_VK_INTERNAL(_CLOSE_CURLY_BRACKET),

DEFINE_VK_INTERNAL(_TILDE),

DEFINE_VK_INTERNAL(_COMMA),
DEFINE_VK_INTERNAL(_PERIOD),
DEFINE_VK_INTERNAL(_SLASH),
DEFINE_VK_INTERNAL(_BACK_QUOTE),
DEFINE_VK_INTERNAL(_OPEN_BRACKET),
DEFINE_VK_INTERNAL(_BACK_SLASH),
DEFINE_VK_INTERNAL(_CLOSE_BRACKET),
DEFINE_VK_INTERNAL(_QUOTE),

DEFINE_VK_INTERNAL(_META),
DEFINE_VK_INTERNAL(_ALTGR)

#undef DEFINE_VK_INTERNAL
#undef DEFINE_VK_INTERNAL2
