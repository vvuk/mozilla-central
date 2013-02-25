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
#include "nsThreadUtils.h"
#include "nsISocketTransport.h"
#include "nsIServerSocket.h"
#include "nsNetCID.h"
#include "nsIOutputStream.h"
#include "nsIEventTarget.h"

#include "lz4.h"

#ifdef __GNUC__
#define PACKED_STRUCT __attribute__((packed))
#else
#define PACKED_STRUCT
#endif

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;
using namespace mozilla::gl;
using namespace mozilla;

class DebugGLData;
class DebugDataSender;

static bool gDebugConnected = false;
static nsCOMPtr<nsIServerSocket> gDebugServerSocket;
static nsCOMPtr<nsIThread> gDebugSenderThread;
static nsCOMPtr<nsISocketTransport> gDebugSenderTransport;
static nsCOMPtr<nsIOutputStream> gDebugStream;
static nsCOMPtr<DebugDataSender> gCurrentSender;

class DebugGLData : public LinkedListElement<DebugGLData> {
public:
  typedef enum {
    FrameStart,
    FrameEnd,
    TextureData,
    ColorData
  } DataType;

  virtual ~DebugGLData() { }

  DataType GetDataType() const { return mDataType; }
  intptr_t GetContextAddress() const { return mContextAddress; }
  int64_t GetValue() const { return mValue; }

  DebugGLData(DataType dataType)
    : mDataType(dataType),
      mContextAddress(0),
      mValue(0)
  { }

  DebugGLData(DataType dataType, GLContext* cx)
    : mDataType(dataType),
      mContextAddress(reinterpret_cast<intptr_t>(cx)),
      mValue(0)
  { }

  DebugGLData(DataType dataType, GLContext* cx, int64_t value)
    : mDataType(dataType),
      mContextAddress(reinterpret_cast<intptr_t>(cx)),
      mValue(value)
  { }

  virtual bool Write() {
    if (mDataType != FrameStart &&
        mDataType != FrameEnd)
    {
      NS_WARNING("Unimplemented data type!");
      return false;
    }

    DebugGLData::BasicPacket packet;

    packet.type = mDataType;
    packet.ptr = static_cast<uint64_t>(mContextAddress);
    packet.value = mValue;

    return WriteToStream(&packet, sizeof(packet));
  }

  static bool WriteToStream(void *ptr, uint32_t size) {
    uint32_t written = 0;
    nsresult rv;
    while (written < size) {
      uint32_t cnt;
      rv = gDebugStream->Write(reinterpret_cast<char*>(ptr) + written,
                               size - written, &cnt);
      if (NS_FAILED(rv))
        return false;

      written += cnt;
    }

    return true;
  }

protected:
  DataType mDataType;
  intptr_t mContextAddress;
  int64_t mValue;

public:
  // the data packet formats; all packed
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
  typedef struct {
    uint32_t type;
    uint64_t ptr;
    uint64_t value;
  } PACKED_STRUCT BasicPacket;

  typedef struct {
    uint32_t type;
    uint64_t ptr;
    uint64_t layerref;
    uint32_t color;
    uint32_t width;
    uint32_t height;
  } PACKED_STRUCT ColorPacket;

  typedef struct {
    uint32_t type;
    uint64_t ptr;
    uint64_t layerref;
    uint32_t name;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t target;
    uint32_t dataFormat;
    uint32_t dataSize;
  } PACKED_STRUCT TexturePacket;
#ifdef _MSC_VER
#pragma pack(pop)
#endif
};

class DebugGLTextureData : public DebugGLData {
public:
  DebugGLTextureData(GLContext* cx, void *layerRef, GLuint target, GLenum name, gfxImageSurface* img)
    : DebugGLData(DebugGLData::TextureData, cx),
      mLayerRef(layerRef),
      mTarget(target),
      mName(name),
      mImage(img)
  {
  }

  void *GetLayerRef() const { return mLayerRef; }
  GLuint GetName() const { return mName; }
  gfxImageSurface* GetImage() const { return mImage; }
  GLenum GetTextureTarget() const { return mTarget; }

  virtual bool Write() {
    DebugGLData::TexturePacket packet;
    char *dataptr = nullptr;
    uint32_t datasize = 0;
    std::auto_ptr<char> compresseddata;

    packet.type = mDataType;
    packet.ptr = static_cast<uint64_t>(mContextAddress);
    packet.layerref = reinterpret_cast<uint64_t>(mLayerRef);
    packet.name = mName;
    packet.format = 0;
    packet.target = mTarget;
    packet.dataFormat = LOCAL_GL_RGBA;

    if (mImage) {
      packet.width = mImage->Width();
      packet.height = mImage->Height();
      packet.stride = mImage->Stride();
      packet.dataSize = mImage->GetDataSize();

      dataptr = (char *)mImage->Data();
      datasize = mImage->GetDataSize();

      compresseddata = std::auto_ptr<char>((char*) moz_malloc(LZ4_compressBound((int) datasize)));
      if (compresseddata.get()) {
        int ndatasize = LZ4_compress(dataptr, compresseddata.get(), datasize);
        if (ndatasize > 0) {
          datasize = ndatasize;
          dataptr = compresseddata.get();

          packet.dataFormat = (1 << 16) | packet.dataFormat;
          packet.dataSize = datasize;
        }
      }
    } else {
      packet.width = 0;
      packet.height = 0;
      packet.stride = 0;
      packet.dataSize = 0;
    }

    // write the packet header data
    if (!WriteToStream(&packet, sizeof(packet)))
      return false;

    // then the image data
    if (!WriteToStream(dataptr, datasize))
      return false;

    // then pad out to 4 bytes
    if (datasize % 4 != 0) {
      static char buf[] = { 0, 0, 0, 0 };
      if (!WriteToStream(buf, 4 - (datasize % 4)))
        return false;
    }

    return true;
  }

protected:
  void *mLayerRef;
  GLenum mTarget;
  GLuint mName;
  nsRefPtr<gfxImageSurface> mImage;
};

class DebugGLColorData : public DebugGLData {
public:
  DebugGLColorData(void *layerRef, const gfxRGBA& color, const nsIntSize& size)
    : DebugGLData(DebugGLData::ColorData),
      mColor(color.Packed()),
      mSize(size)
  {
  }

  void *GetLayerRef() const { return mLayerRef; }
  uint32_t GetColor() const { return mColor; }
  const nsIntSize& GetSize() const { return mSize; }

  virtual bool Write() {
    DebugGLData::ColorPacket packet;

    packet.type = mDataType;
    packet.ptr = static_cast<uint64_t>(mContextAddress);
    packet.layerref = reinterpret_cast<uintptr_t>(mLayerRef);
    packet.color = mColor;
    packet.width = mSize.width;
    packet.height = mSize.height;

    return WriteToStream(&packet, sizeof(packet));
  }

protected:
  void *mLayerRef;
  uint32_t mColor;
  nsIntSize mSize;
};

static bool
CheckSender()
{
  if (!gDebugConnected)
    return false;

  // At some point we may want to support sending
  // data in between frames.
#if 1
  if (!gCurrentSender)
    return false;
#else
  if (!gCurrentSender)
    gCurrentSender = new DebugDataSender();
#endif

  return true;
}

class DebugListener : public nsIRunnable,
                      public nsIServerSocketListener
{
public:
  NS_DECL_ISUPPORTS

  DebugListener() { }

  /* nsIRunnable (to set up the server) */

  NS_IMETHODIMP Run() {
    gDebugServerSocket = do_CreateInstance(NS_SERVERSOCKET_CONTRACTID);
    gDebugServerSocket->Init(23456, false, -1);
    gDebugServerSocket->AsyncListen(this);
    return NS_OK;
  }

  /* nsIServerSocketListener */

  NS_IMETHODIMP OnSocketAccepted(nsIServerSocket *aServ,
                                 nsISocketTransport *aTransport)
  {
    gDebugConnected = true;
    gDebugSenderTransport = aTransport;
    aTransport->OpenOutputStream(nsITransport::OPEN_BLOCKING, 0, 0, getter_AddRefs(gDebugStream));
    return NS_OK;
  }

  NS_IMETHODIMP OnStopListening(nsIServerSocket *aServ,
                                nsresult aStatus)
  {
    return NS_OK;
  }
};

NS_IMPL_THREADSAFE_ISUPPORTS2(DebugListener, nsIRunnable, nsIServerSocketListener);

class DebugDataSender : public nsIRunnable
{
public:
  NS_DECL_ISUPPORTS

  DebugDataSender() {
    mList = new LinkedList<DebugGLData>();
  }

  virtual ~DebugDataSender() {
    Cleanup();
  }

  void Append(DebugGLData *d) {
    mList->insertBack(d);
  }

  void Cleanup() {
    if (!mList)
      return;

    DebugGLData *d;
    while ((d = mList->popFirst()) != nullptr)
      delete d;
    delete mList;

    mList = nullptr;
  }

  /* nsIRunnable impl; send the data */

  NS_IMETHODIMP Run() {
    DebugGLData *d;
    nsresult rv = NS_OK;

    // If we got closed while trying to write some stuff earlier, just throw away
    // everything.
    if (!gDebugStream) {
      Cleanup();
      return NS_OK;
    }

    while ((d = mList->popFirst()) != nullptr) {
      std::auto_ptr<DebugGLData> cleaner(d);
      if (!d->Write()) {
        rv = NS_ERROR_FAILURE;
        break;
      }
    }

    Cleanup();

    if (NS_FAILED(rv)) {
      gDebugSenderTransport->Close(rv);
      gDebugConnected = false;
      gDebugStream = nullptr;
    }

    return NS_OK;
  }

protected:
  LinkedList<DebugGLData> *mList;
};

NS_IMPL_THREADSAFE_ISUPPORTS1(DebugDataSender, nsIRunnable);

void
LayerManagerOGL::DebugBeginFrame(int64_t aFrameStamp)
{
  if (!gDebugSenderThread) {
    NS_NewThread(getter_AddRefs(gDebugSenderThread), new DebugListener());
  }

  if (!gDebugConnected)
    return;

#if 0
  // if we're sending data in between frames, flush the list down the socket,
  // and start a new one
  if (gCurrentSender) {
    gDebugSenderThread->Dispatch(gCurrentSender, NS_DISPATCH_NORMAL);
  }
#endif

  gCurrentSender = new DebugDataSender();
  gCurrentSender->Append(new DebugGLData(DebugGLData::FrameStart, mGLContext, aFrameStamp));
}

void
LayerManagerOGL::DebugEndFrame()
{
  if (!CheckSender())
    return;

  gCurrentSender->Append(new DebugGLData(DebugGLData::FrameEnd, mGLContext));
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
