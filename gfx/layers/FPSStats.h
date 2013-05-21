/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_FPSSTATS_H
#define MOZILLA_GFX_FPSSTATS_H

#include "mozilla/TimeStamp.h"

namespace mozilla {
namespace layers {

const double kFpsWindowMs = 250.0;

// We need to keep enough timestamps to cover a few frames
const size_t kNumFrameTimeStamps = 256;

class FPSStats {
public:
  FPSStats() : mCurrentFrameIndex(0) {
    mFrames.SetLength(kNumFrameTimeStamps);
  }

  void Reset() {
    mCurrentFrameIndex = 0;
    for (int i = 0; i < kNumFrameTimeStamps; ++i) {
      mFrames[i] = TimeStamp();
    }
  }

  void AddFrame(TimeStamp aFrameTime) {
    mFrames[mCurrentFrameIndex] = aFrameTime;
    mCurrentFrameIndex = (mCurrentFrameIndex + 1) % kNumFrameTimeStamps;
  }

  double AddFrameAndGetFps(TimeStamp aFrameTime) {
    AddFrame(aFrameTime);
    return EstimateFps(aFrameTime);
  }

  double GetFps() {
    size_t previousFrame = mCurrentFrameIndex;
    if (previousFrame == 0) {
      previousFrame = kNumFrameTimeStamps - 1;
    } else {
      previousFrame--;
    }

    if (mFrames[previousFrame].IsNull())
      return 0.0;

    return EstimateFps(mFrames[previousFrame]);
  }

  double GetFpsAt(TimeStamp aNow) {
    return EstimateFps(aNow);
  }

protected:
  double EstimateFps(TimeStamp aNow) {
    TimeStamp beginningOfWindow =
      (aNow - TimeDuration::FromMilliseconds(kFpsWindowMs));
    TimeStamp earliestFrameInWindow = aNow;
    size_t numFramesDrawnInWindow = 0;
    for (size_t i = 0; i < kNumFrameTimeStamps; ++i) {
      const TimeStamp& frame = mFrames[i];
      if (!frame.IsNull() && frame > beginningOfWindow) {
        ++numFramesDrawnInWindow;
        earliestFrameInWindow = std::min(earliestFrameInWindow, frame);
      }
    }
    double realWindowSecs = (aNow - earliestFrameInWindow).ToSeconds();
    if (realWindowSecs == 0.0 || numFramesDrawnInWindow == 1) {
      return 0.0;
    }

    return double(numFramesDrawnInWindow - 1) / realWindowSecs;
  }

  size_t mCurrentFrameIndex;
  nsAutoTArray<TimeStamp, kNumFrameTimeStamps> mFrames;
};

} // namespace layers
} // namespace mozilla

#endif
