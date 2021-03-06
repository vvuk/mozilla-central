/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 ci et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZPOISONWRITE_H
#define MOZPOISONWRITE_H

#include "mozilla/Types.h"
#include <stdio.h>

MOZ_BEGIN_EXTERN_C
  void MozillaRegisterDebugFD(int fd);
  void MozillaRegisterDebugFILE(FILE *f);
  void MozillaUnRegisterDebugFD(int fd);
  void MozillaUnRegisterDebugFILE(FILE *f);
MOZ_END_EXTERN_C

#ifdef __cplusplus
namespace mozilla {
enum ShutdownChecksMode {
  SCM_CRASH,
  SCM_RECORD,
  SCM_NOTHING
};
extern ShutdownChecksMode gShutdownChecks;

void PoisonWrite();
void DisableWritePoisoning();
void EnableWritePoisoning();
}
#endif

#endif
