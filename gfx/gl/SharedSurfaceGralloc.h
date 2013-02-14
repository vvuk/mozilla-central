/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACE_GRALLOC_H_
#define SHARED_SURFACE_GRALLOC_H_

#include "SharedSurfaceGL.h"

namespace mozilla {
namespace layers {
class ShadowLayerForwarder;
class SurfaceDescriptorGralloc;
}

namespace gl {
class GLContext;
class GLLibraryEGL;

class SharedSurface_Gralloc
    : public SharedSurface_GL
{
public:
    static SharedSurface_Gralloc* Create(GLContext* prodGL,
                                         const GLFormats& formats,
                                         const gfxIntSize& size,
                                         bool hasAlpha,
                                         layers::ShadowLayerForwarder* slf);

    static SharedSurface_Gralloc* Cast(SharedSurface* surf) {
        MOZ_ASSERT(surf->Type() == SharedSurfaceType::Gralloc);

        return (SharedSurface_Gralloc*)surf;
    }

protected:
    GLLibraryEGL* const mEGL;
    layers::ShadowLayerForwarder* const mSLF;
    layers::SurfaceDescriptorGralloc* const mDesc;
    const GLuint mProdTex;

    SharedSurface_Gralloc(GLContext* prodGL,
                          const gfxIntSize& size,
                          bool hasAlpha,
                          GLLibraryEGL* egl,
                          layers::ShadowLayerForwarder* slf,
                          layers::SurfaceDescriptorGralloc* desc,
                          GLuint prodTex)
        : SharedSurface_GL(SharedSurfaceType::Gralloc,
                           AttachmentType::GLTexture,
                           prodGL,
                           size,
                           hasAlpha)
        , mEGL(egl)
        , mSLF(slf)
        , mDesc(desc)
        , mProdTex(prodTex)
    {}

    static bool HasExtensions(GLLibraryEGL* egl, GLContext* gl);

public:
    virtual ~SharedSurface_Gralloc();

    virtual void LockProdImpl() {}
    virtual void UnlockProdImpl() {}


    virtual void Fence();
    virtual bool WaitSync();


    virtual GLuint Texture() const {
        return mProdTex;
    }

    layers::SurfaceDescriptorGralloc* GetDescriptor() const {
        return mDesc;
    }
};



class SurfaceFactory_Gralloc
    : public SurfaceFactory_GL
{
protected:
    layers::ShadowLayerForwarder* const mSLF;

public:
    SurfaceFactory_Gralloc(GLContext* prodGL,
                           const SurfaceCaps& caps,
                           layers::ShadowLayerForwarder* slf)
        : SurfaceFactory_GL(prodGL, SharedSurfaceType::Gralloc, caps)
        , mSLF(slf)
    {}

    virtual SharedSurface* CreateShared(const gfxIntSize& size) {
        bool hasAlpha = mReadCaps.alpha;
        return SharedSurface_Gralloc::Create(mGL, mFormats, size, hasAlpha, mSLF);
    }
};

} /* namespace gl */
} /* namespace mozilla */

#endif /* SHARED_SURFACE_GRALLOC_H_ */
