/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Workers.h"

#include "nsRefPtrHashtable.h"

using namespace mozilla;
USING_WORKERS_NAMESPACE

namespace {

static nsRefPtrHashtable<nsCStringHashKey, nsICanvasRendingContextInternal> canvases = 

bool
RegisterWorkerCanvasContext(const char* aToken, nsICanvasRendingContextInternal* aContext)
{
}

already_AddRefed<nsICanvasRendingContextInternal>
GetWorkerCanvasContext(const char* aToken)
{
}


}

END_WORKERS_NAMESPACE
