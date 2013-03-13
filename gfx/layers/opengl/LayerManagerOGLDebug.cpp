/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/PLayers.h"

/* This must occur *after* layers/PLayers.h to avoid typedefs conflicts. */
#include "mozilla/Util.h"

#include "Composer2D.h"
#include "LayerManagerOGL.h"
#include "ThebesLayerOGL.h"
#include "ContainerLayerOGL.h"
#include "ImageLayerOGL.h"
#include "ColorLayerOGL.h"
#include "CanvasLayerOGL.h"
#include "TiledThebesLayerOGL.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Preferences.h"
#include "TexturePoolOGL.h"

#include "gfxContext.h"
#include "gfxUtils.h"
#include "gfxPlatform.h"
#include "nsIWidget.h"

#include "GLContext.h"
#include "GLContextProvider.h"

#include "nsIServiceManager.h"
#include "nsIConsoleService.h"

#include <memory>
#include <stdint.h>
#include "mozilla/LinkedList.h"
#include "mozilla/Compression.h"
#include "nsThreadUtils.h"
#include "nsISocketTransport.h"
#include "nsIServerSocket.h"
#include "nsNetCID.h"
#include "nsIOutputStream.h"
#include "nsIEventTarget.h"

#ifdef __GNUC__
#define PACKED_STRUCT __attribute__((packed))
#else
#define PACKED_STRUCT
#endif

namespace mozilla {
namespace layers {

void
LayerManagerOGL::DebugBeginFrame(int64_t aFrameStamp)
{
  LayerManagerDebug::EnsureSenderThread();

  if (!LayerManagerDebug::Connected)
    return;

#if 0
  // if we're sending data in between frames, flush the list down the socket,
  // and start a new one
  if (gCurrentSender) {
    gDebugSenderThread->Dispatch(gCurrentSender, NS_DISPATCH_NORMAL);
  }
#endif

  gCurrentSender = new DebugDataSender();
  gCurrentSender->Append(new DebugData(DebugData::FrameStart, mGLContext, aFrameStamp));
}

void
LayerManagerOGL::DebugEndFrame()
{
  if (!CheckSender())
    return;

  gCurrentSender->Append(new DebugData(DebugData::FrameEnd, mGLContext));
  gDebugSenderThread->Dispatch(gCurrentSender, NS_DISPATCH_NORMAL);
  gCurrentSender = nullptr;
}

void
LayerManagerOGL::DebugSendTexture(void *aLayerRef,
                                  GLenum aTextureTarget,
                                  GLuint aTextureID,
                                  GLuint aWidth,
                                  GLuint aHeight,
                                  ShaderProgramType aShader,
                                  bool aFlipY)
{
  if (!CheckSender())
    return;

  nsRefPtr<gfxImageSurface> img =
    mGLContext->ReadTextureImage(aTextureID, aTextureTarget,
                                 gfxIntSize(aWidth, aHeight),
                                 aShader, aFlipY);

  gCurrentSender->Append(new DebugGLTextureData(mGLContext,
                                                aLayerRef,
                                                aTextureTarget,
                                                aTextureID,
                                                img));
}

void
LayerManagerOGL::DebugSendColor(void *aLayerRef,
                                const gfxRGBA& aColor,
                                const nsIntSize& aSize)
{
  if (!CheckSender())
    return;

  gCurrentSender->Append(new DebugGLColorData(aLayerRef, aColor, aSize));
}

} /* layers */
} /* mozilla */
