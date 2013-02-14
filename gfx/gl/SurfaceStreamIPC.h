/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SURFACESTREAM_IPC_H_
#define SURFACESTREAM_IPC_H_

#include "SurfaceStream.h"

namespace mozilla {
namespace layers {
    class ShadowLayerManager;
}
namespace gfx {

class SurfaceStream_IPC
    : public SurfaceStream
{
protected:
    SharedSurface* mStaging;
    SharedSurface* mTransit;
    SharedSurface* mConsumer;

    layers::ShadowLayerManager* mLayerManager;


public:
    SurfaceStream_IPC(SurfaceStream* prevStream);
    virtual ~SurfaceStream_IPC();

    /* We use Layers::FinishTransactions to guarantee a
     * spooky SwapConsumer at a distance, from inside of SwapProducer.
     * In SwapProducer, before we swap out our finished frame to the
     * compositor, we make a synchronous call to the compositor, which will
     * block until the compositor can respond. Since IPC is in-order, we then
     * know that the previous composition (including SwapBuffers!) is complete,
     * which means that the compositor has finished rendering from the
     * buffer which we refer to as mTransit, or the buffer which we last sent
     * to the compositor, but are not sure if it has yet supplanted mConsumer.
     *
     * That is, the compositor is either sampling from mConsumer, or the newer
     * mTransit, but we can't know until we do a 'Layers Finish', which assures
     * that the compositor is done with mConsumer, and using mTransit. Thus
     * after the 'Layers Finish', we know that the compositor has scrapped
     * mConsumer and promoted mTransit to mConsumer. As such, after Finishing,
     * we update our records to reflect this knowledge.
     *
     * Therefore, we are free to reuse the just-scrapped buffer as our next
     * mProducer buffer, and submit our previously completed frame to the
     * compositor across IPC, via Layers.
     */
    virtual SharedSurface* SwapProducer(SurfaceFactory* factory,
                                        const gfxIntSize& size);

    virtual SharedSurface* SwapConsumer_NoWait() {
        //MOZ_NOT_REACHED("SurfaceStream_IPC doesn't use SwapConsumer.");
        //MOZ_CRASH();
        return nullptr;
    }


    void SetLayerManager(layers::ShadowLayerManager* layerManager);

    //TODO: Name this better.
    void WaitForCompositor();


    SharedSurface* SwapTransit();

    virtual void SurrenderSurfaces(SharedSurface*& producer, SharedSurface*& consumer);
};


} /* namespace gfx */
} /* namespace mozilla */

#endif
