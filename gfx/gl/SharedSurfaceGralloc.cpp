/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceGralloc.h"

#include "GLContext.h"
#include "SharedSurfaceGL.h"
#include "SurfaceFactory.h"
#include "GLLibraryEGL.h"

#include "ui/GraphicBuffer.h"
#include "../layers/ipc/ShadowLayers.h"
#include "mozilla/layers/LayersSurfaces.h"
//#include "../layers/ipc/SurfaceDescriptorGlue.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace gl;
using namespace layers;
using namespace android;

static bool
CreateGrallocBuffer(ShadowLayerForwarder* slf,
                    const gfxIntSize& size,
                    bool hasAlpha,
                    SurfaceDescriptorGralloc* desc_out)
{
    MOZ_ASSERT(desc_out);
    printf_stderr("CreateGrallocBuffer() begins.\n");

    gfxASurface::gfxContentType type = hasAlpha ? gfxASurface::CONTENT_COLOR_ALPHA
                                                : gfxASurface::CONTENT_COLOR;
    SurfaceDescriptor desc;
    if (!slf->AllocBuffer(size, type, &desc)) {
        return false;
    }

    if (desc.type() != SurfaceDescriptor::TSurfaceDescriptorGralloc) {
        slf->DestroySharedSurface(&desc);
        return false;
    }

    *desc_out = desc.get_SurfaceDescriptorGralloc();
    printf_stderr("CreateGrallocBuffer() successful.\n");

    return true;
}

static sp<GraphicBuffer>
GetGraphicBufferFromDesc(const SurfaceDescriptorGralloc& desc)
{
    return GrallocBufferActor::GetFrom(desc);
}

static void
DeleteGrallocBuffer(ShadowLayerForwarder* slf,
                    const SurfaceDescriptorGralloc& desc)
{
    SurfaceDescriptor handle = desc;
    slf->DestroySharedSurface(&handle);
}


SharedSurface_Gralloc*
SharedSurface_Gralloc::Create(GLContext* prodGL,
                              const GLFormats& formats,
                              const gfxIntSize& size,
                              bool hasAlpha,
                              ShadowLayerForwarder* slf)
{
    GLLibraryEGL* egl = prodGL->GetLibraryEGL();
    MOZ_ASSERT(egl);

    printf_stderr("[SharedSurface_Gralloc::Create] Begin.\n");

    if (!HasExtensions(egl, prodGL))
        return nullptr;

    printf_stderr("[SharedSurface_Gralloc::Create] Have exts.\n");

    SurfaceDescriptorGralloc desc;
    if (!CreateGrallocBuffer(slf, size, hasAlpha, &desc))
        return nullptr;

    sp<GraphicBuffer> buffer = GetGraphicBufferFromDesc(desc);

    EGLDisplay display = egl->Display();
    EGLClientBuffer clientBuffer = buffer->getNativeBuffer();
    EGLint attrs[] = {
        LOCAL_EGL_IMAGE_PRESERVED, LOCAL_EGL_TRUE,
        LOCAL_EGL_NONE, LOCAL_EGL_NONE
    };
    EGLImage image = egl->fCreateImage(display,
                                       EGL_NO_CONTEXT,
                                       LOCAL_EGL_NATIVE_BUFFER_ANDROID,
                                       clientBuffer, attrs);
    printf_stderr("[SharedSurface_Gralloc::Create] image: 0x%08x.\n",
                  (uint32_t)(uintptr_t)image);
    if (!image) {
        DeleteGrallocBuffer(slf, desc);
        return nullptr;
    }

    prodGL->MakeCurrent();
    GLuint prodTex = 0;
    prodGL->fGenTextures(1, &prodTex);

    ScopedBindTexture autoTex(prodGL, prodTex);
    prodGL->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_2D, image);
    egl->fDestroyImage(display, image);

    SurfaceDescriptorGralloc* pDesc = new SurfaceDescriptorGralloc(desc);

    printf_stderr("[SharedSurface_Gralloc::Create] Success.\n");

    return new SharedSurface_Gralloc(prodGL, size, hasAlpha, egl, slf, pDesc, prodTex);
}


bool
SharedSurface_Gralloc::HasExtensions(GLLibraryEGL* egl, GLContext* gl)
{
    return egl->HasKHRImageBase() &&
           gl->IsExtensionSupported(GLContext::OES_EGL_image);
}

SharedSurface_Gralloc::~SharedSurface_Gralloc()
{
    mGL->MakeCurrent();
    mGL->fDeleteTextures(1, (GLuint*)&mProdTex);

    DeleteGrallocBuffer(mSLF, *mDesc);
    delete mDesc;
}

void
SharedSurface_Gralloc::Fence()
{
    mGL->MakeCurrent();
    mGL->fFinish();
}

bool
SharedSurface_Gralloc::WaitSync()
{
    return true;
}
