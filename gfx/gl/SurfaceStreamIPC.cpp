/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SurfaceStreamIPC.h"

#include "SharedSurface.h"
#include "SurfaceFactory.h"
#include "../layers/ipc/ShadowLayers.h"

using namespace mozilla;
using namespace gfx;
using namespace layers;

SurfaceStream_IPC::SurfaceStream_IPC(SurfaceStream* prevStream)
    : SurfaceStream(SurfaceStreamType::IPC, prevStream)
    , mStaging(nullptr)
    , mTransit(nullptr)
    , mConsumer(nullptr)
    , mLayerManager(nullptr)
{
    if (!prevStream)
        return;

    SharedSurface* prevProducer = nullptr;
    SharedSurface* prevConsumer = nullptr;
    prevStream->SurrenderSurfaces(prevProducer, prevConsumer);

    if (prevConsumer == prevProducer)
        prevConsumer = nullptr;

    mProducer = Absorb(prevProducer);
    mConsumer = Absorb(prevConsumer);
}

SurfaceStream_IPC::~SurfaceStream_IPC()
{
    Delete(mStaging);
    Delete(mTransit);
    Delete(mConsumer);
}

SharedSurface*
SurfaceStream_IPC::SwapProducer(SurfaceFactory* factory,
                                const gfxIntSize& size)
{
    RecycleScraps(factory);
    WaitForCompositor(); // cons=>scraps, transit=>cons.

    if (mProducer)
        mProducer->Fence();

    Move(mProducer, mStaging);

    New(factory, size, mProducer);
    MOZ_ASSERT(mProducer);

    if (mStaging &&
        factory->Caps().preserve)
    {
        SharedSurface::Copy(mStaging, mProducer, factory);
    }

    return mProducer;
}

void
SurfaceStream_IPC::SetLayerManager(ShadowLayerManager* layerManager)
{
    mLayerManager = layerManager;
}

void
SurfaceStream_IPC::WaitForCompositor()
{
    MOZ_ASSERT(mLayerManager, "Layer manager not yet set.");
    mLayerManager->Flush(); // The secret sauce.

    if (mTransit) {
        Scrap(mConsumer);
        Move(mTransit, mConsumer);
    }
    // Our records now reflect the state of buffers after Layers Flush (Finish).
}


SharedSurface*
SurfaceStream_IPC::SwapTransit()
{
    Move(mStaging, mTransit);
    if (mTransit) {
       MOZ_ALWAYS_TRUE( mTransit->WaitSync() );
    }
    return mTransit;
}

void
SurfaceStream_IPC::SurrenderSurfaces(SharedSurface*& producer,
                                     SharedSurface*& consumer)
{
    WaitForCompositor();

    mIsAlive = false;

    producer = Surrender(mProducer);
    consumer = Surrender(mConsumer);

    NS_ERROR("Don't do this.");
    MOZ_CRASH();
}
