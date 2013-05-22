/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FPSStats.h"
#include "mozilla/gfx/2D.h"

using namespace mozilla;
using namespace mozilla::layers;

static const int32_t kTextSpriteWidth = 3;
static const int32_t kTextSpriteHeight = 5;

// largely stolen from gfxFontMissingGlyphs
//#define MAKE_PIXEL(bit)  ((bit) ? 0xff0000ff : 0x00000000)
#define MAKE_PIXEL(bit)  ((bit) ? 1 : 0)
#define CHAR_BITS(b00, b01, b02, b10, b11, b12, b20, b21, b22, b30, b31, b32, b40, b41, b42) \
    { MAKE_PIXEL(b00), MAKE_PIXEL(b01), MAKE_PIXEL(b02),                      \
      MAKE_PIXEL(b10), MAKE_PIXEL(b11), MAKE_PIXEL(b12),                      \
      MAKE_PIXEL(b20), MAKE_PIXEL(b21), MAKE_PIXEL(b22),                      \
      MAKE_PIXEL(b30), MAKE_PIXEL(b31), MAKE_PIXEL(b32),                      \
      MAKE_PIXEL(b40), MAKE_PIXEL(b41), MAKE_PIXEL(b42) }

// 10 characters, 0-9; each one is 3*5 in size
static const uint8_t kNumberFont[][3*5] = {
  CHAR_BITS(0, 1, 0,
            1, 0, 1,
            1, 0, 1,
            1, 0, 1,
            0, 1, 0),
  CHAR_BITS(0, 1, 0,
            0, 1, 0,
            0, 1, 0,
            0, 1, 0,
            0, 1, 0),
  CHAR_BITS(1, 1, 1,
            0, 0, 1,
            1, 1, 1,
            1, 0, 0,
            1, 1, 1),
  CHAR_BITS(1, 1, 1,
            0, 0, 1,
            1, 1, 1,
            0, 0, 1,
            1, 1, 1),
  CHAR_BITS(1, 0, 1,
            1, 0, 1,
            1, 1, 1,
            0, 0, 1,
            0, 0, 1),
  CHAR_BITS(1, 1, 1,
            1, 0, 0,
            1, 1, 1,
            0, 0, 1,
            1, 1, 1),
  CHAR_BITS(1, 1, 1,
            1, 0, 0,
            1, 1, 1,
            1, 0, 1,
            1, 1, 1),
  CHAR_BITS(1, 1, 1,
            0, 0, 1,
            0, 0, 1,
            0, 0, 1,
            0, 0, 1),
  CHAR_BITS(0, 1, 0,
            1, 0, 1,
            0, 1, 0,
            1, 0, 1,
            0, 1, 0),
  CHAR_BITS(1, 1, 1,
            1, 0, 1,
            1, 1, 1,
            0, 0, 1,
            0, 0, 1),
  CHAR_BITS(0, 0, 0,
            0, 0, 0,
            0, 0, 0,
            0, 0, 0,
            0, 0, 0)
};

static void
draw_digits_doubled(uint8_t *data,
                    int32_t stride,
                    uint32_t color,
                    const uint8_t* nums[])
{
  for (int j = 0; j < kTextSpriteHeight; ++j) {
    for (int k = 0; k < 2; ++k) {
      uint32_t *line = reinterpret_cast<uint32_t*>(data + (j * 2 + k) * stride);

      for (int num = 0; num < 3; ++num) {
        line[(kTextSpriteWidth + 1) * 2 * num + 0] = nums[num][j*3+0] * color;
        line[(kTextSpriteWidth + 1) * 2 * num + 1] = nums[num][j*3+0] * color;

        line[(kTextSpriteWidth + 1) * 2 * num + 2] = nums[num][j*3+1] * color;
        line[(kTextSpriteWidth + 1) * 2 * num + 3] = nums[num][j*3+1] * color;

        line[(kTextSpriteWidth + 1) * 2 * num + 4] = nums[num][j*3+2] * color;
        line[(kTextSpriteWidth + 1) * 2 * num + 5] = nums[num][j*3+2] * color;
      }
    }
  }
}

bool
FPSUtils::FillFPSSurface(gfx::DataSourceSurface* aSurface,
                         const FPSStats& fpsStats,
                         const FPSStats& txnStats)
{
    int32_t width = aSurface->GetSize().width;
    int32_t height = aSurface->GetSize().height;

    uint8_t *data = aSurface->GetData();
    int32_t stride = aSurface->Stride();

    memset(data, 0, stride*height);

    /* We need room for 2 3-digit numbers, and then the graph.
     * We're going to do it like this:
     *
     * XXX +--------------+
     * YYY |              |
     *     |              |
     *     +--------------+
     */

    // note that we're going to double our font, so *2
    static const int32_t number_width = (kTextSpriteWidth + 1) * 2 * 3;
    static const int32_t number_height = (kTextSpriteHeight + 1) * 2;

    if (width < number_width || height < number_height * 2) {
      // we don't have enough room for anything
      return false;
    }

    int32_t chart_width = width - number_width;

    const uint8_t *nums[3];
    int32_t num = (int32_t) fpsStats.GetFps();
    if (num > 1000)
        num = 999;

    nums[0] = kNumberFont[(num > 99) ? ((num % 1000) / 100) : 10];
    nums[1] = kNumberFont[(num > 9) ? ((num % 100) / 10) : 10];
    nums[2] = kNumberFont[num % 10];
    draw_digits_doubled(data, stride, 0xff0000ffU, nums);

    num = (int32_t) txnStats.GetFps();
    if (num > 1000)
        num = 999;

    nums[0] = kNumberFont[(num > 99) ? ((num % 1000) / 100) : 10];
    nums[1] = kNumberFont[(num > 9) ? ((num % 100) / 10) : 10];
    nums[2] = kNumberFont[num % 10];
    draw_digits_doubled(data + stride * number_height, stride, 0xffff00ffU, nums);

    return true;
}

