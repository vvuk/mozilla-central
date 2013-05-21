/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/Effects.h"

namespace mozilla {
namespace layers {

/* static */ LayersBackend Compositor::sBackend = LAYERS_NONE;
/* static */ LayersBackend
Compositor::GetBackend()
{
  return sBackend;
}

#define ARGB(_c)  gfx::Color((((_c) >> 16) & 0xff) / 255.0f,            \
                             (((_c) >> 8) & 0xff) / 255.0f,             \
                             (((_c) >> 0) & 0xff) / 255.0f, 1.0f)

static const size_t kNumFrameBarColors = 16;
static gfx::Color sFrameBarColors[kNumFrameBarColors] = {
  ARGB(0x00262626),
  ARGB(0x000000bd),
  ARGB(0x005d0016),
  ARGB(0x00f20019),
  ARGB(0x00827800),
  ARGB(0x008f00c7),
  ARGB(0x000086fe),
  ARGB(0x00008000),
  ARGB(0x00aaaaaa),
  ARGB(0x0000fefe),
  ARGB(0x00fe68fe),
  ARGB(0x00fe8420),
  ARGB(0x0070fe00),
  ARGB(0x00fefe00),
  ARGB(0x00fed38b),
  ARGB(0x00a0d681)
};

void
Compositor::PreStartFrameDraw(const gfx::Rect& contentRect)
{
  mCurrentFrameRect = contentRect;
}

void
Compositor::PreFinishFrameDraw()
{
  if (IsDiagnosticEnabled(DiagnosticFrameBars)) {
    DrawDiagnosticFrameBar(mCurrentFrameRect);
  }

  if (IsDiagnosticEnabled(DiagnosticFPS)) {
    mFPSStats.AddFrame(TimeStamp::Now());
    DrawDiagnosticFPS();
  }
}

void
Compositor::DrawDiagnosticFrameBar(const gfx::Rect& contentRect)
{
  EffectChain effects;
  effects.mPrimaryEffect = new EffectSolidColor(sFrameBarColors[mCurrentFrameBarColor]);
  int barWidth = 6; // pixels
  mCurrentFrameBarColor = (mCurrentFrameBarColor + 1) % kNumFrameBarColors;

  this->DrawQuad(gfx::Rect(0, 0, barWidth, contentRect.height),
                 gfx::Rect(0, 0, barWidth, contentRect.height),
                 effects, 1.0,
                 gfx::Matrix4x4(), gfx::Point(0.0, 0.0));
}

void
Compositor::DrawDiagnosticColoredBorder(const gfx::Color& aColor,
                                        const gfx::Rect& rect,
                                        const gfx::Rect& aClipRect,
                                        const gfx::Matrix4x4& aTransform,
                                        const gfx::Point& aOffset)
{
  if (!IsDiagnosticEnabled(DiagnosticColoredBorders)) {
    return;
  }
  EffectChain effects;
  effects.mPrimaryEffect = new EffectSolidColor(aColor);
  int lWidth = 1;
  float opacity = 0.8;
  // left
  this->DrawQuad(gfx::Rect(rect.x, rect.y,
                           lWidth, rect.height),
                 aClipRect, effects, opacity,
                 aTransform, aOffset);
  // top
  this->DrawQuad(gfx::Rect(rect.x + lWidth, rect.y,
                           rect.width - 2 * lWidth, lWidth),
                 aClipRect, effects, opacity,
                 aTransform, aOffset);
  // right
  this->DrawQuad(gfx::Rect(rect.x + rect.width - lWidth, rect.y,
                           lWidth, rect.height),
                 aClipRect, effects, opacity,
                 aTransform, aOffset);
  // bottom
  this->DrawQuad(gfx::Rect(rect.x + lWidth, rect.y + rect.height-lWidth,
                           rect.width - 2 * lWidth, lWidth),
                 aClipRect, effects, opacity,
                 aTransform, aOffset);
}

} // namespace
} // namespace
