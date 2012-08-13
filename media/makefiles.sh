#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

add_makefiles "
  media/libcubeb/include/Makefile
  media/libcubeb/Makefile
  media/libcubeb/src/Makefile
  media/libjpeg/Makefile
  media/libjpeg/simd/Makefile
  media/libnestegg/include/Makefile
  media/libnestegg/Makefile
  media/libnestegg/src/Makefile
  media/libogg/include/Makefile
  media/libogg/include/ogg/Makefile
  media/libogg/Makefile
  media/libogg/src/Makefile
  media/libopus/Makefile
  media/libpng/Makefile
  media/libspeex_resampler/Makefile
  media/libspeex_resampler/src/Makefile
  media/libsydneyaudio/include/Makefile
  media/libsydneyaudio/Makefile
  media/libsydneyaudio/src/Makefile
  media/libtheora/include/Makefile
  media/libtheora/include/theora/Makefile
  media/libtheora/lib/Makefile
  media/libtheora/Makefile
  media/libvorbis/include/Makefile
  media/libvorbis/include/vorbis/Makefile
  media/libvorbis/lib/Makefile
  media/libvorbis/Makefile
  media/libvpx/Makefile
  media/mtransport/build/Makefile
  media/mtransport/standalone/Makefile
  media/mtransport/third_party/Makefile
  media/webrtc/Makefile
"

if [ "$ENABLE_TESTS" ]; then
  add_makefiles "
    media/mtransport/test/Makefile
    media/webrtc/signaling/test/Makefile
  "
fi
