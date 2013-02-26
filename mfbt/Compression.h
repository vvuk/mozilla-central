/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Various simple compression/decompression functions. */

#ifndef mozilla_Compression_h_
#define mozilla_Compression_h_

#include "mozilla/Types.h"

namespace mozilla {
namespace Compression {

class lz4 {

public:

  /*
    Compresses 'isize' bytes from 'source' into 'dest'.
    Destination buffer must be already allocated,
    and must be sized to handle worst cases situations (input data not compressible)
    Worst case size evaluation is provided by function LZ4_compressBound()

    isize  : is the input size. Max supported value is ~1.9GB
    return : the number of bytes written in buffer dest
   */
  static MFBT_API int compress(const char* source, char* dest, int isize);

  /*
    Compress 'isize' bytes from 'source' into an output buffer 'dest'
    of maximum size 'maxOutputSize'.  If it cannot achieve it,
    compression will stop, and result of the function will be zero.
    This function never writes outside of provided output buffer.

    isize  : is the input size. Max supported value is ~1.9GB
    maxOutputSize : is the size of the destination buffer (which must be already allocated)
    return : the number of bytes written in buffer 'dest'
             or 0 if the compression fails
  */
  static MFBT_API int compress(const char* source, char* dest, int isize, int maxOutputSize);

  /*
    osize  : is the output size, therefore the original size
    return : the number of bytes read in the source buffer

    If the source stream is malformed, the function will stop decoding
    and return a negative result, indicating the byte position of the
    faulty instruction

    This function never writes outside of provided buffers, and never
    modifies input buffer.

    note : destination buffer must be already allocated.
           its size must be a minimum of 'osize' bytes.
  */
  static MFBT_API int uncompress(const char* source, char* dest, int osize);

  /*
    isize  : is the input size, therefore the compressed size
    maxOutputSize : is the size of the destination buffer (which must be already allocated)
    return : the number of bytes decoded in the destination buffer (necessarily <= maxOutputSize)

    If the source stream is malformed, the function will stop decoding
    and return a negative result, indicating the byte position of the
    faulty instruction.

    This function never writes beyond dest + maxOutputSize, and is
    therefore protected against malicious data packets.

    note   : Destination buffer must be already allocated.
             This version is slightly slower than LZ4_uncompress()
  */
  static MFBT_API int uncompress(const char* source, char* dest, int isize, int maxOutputSize);

  /*
    Provides the maximum size that LZ4 may output in a "worst case"
    scenario (input data not compressible) primarily useful for memory
    allocation of output buffer.

    isize  : is the input size. Max supported value is ~1.9GB
    return : maximum output size in a "worst case" scenario
    note : this function is limited by "int" range (2^31-1)
  */
  static MFBT_API int compressBound(int isize) { return ((isize) + ((isize)/255) + 16); }

};

} /* namespace Compression */
} /* namespace mozilla */

#endif /* mozilla_Compression_h_ */
