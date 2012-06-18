/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

// This is a wrapper around the nICEr ICE stack
#ifndef nricemediastream_h__
#define nricemediastream_h__

#include <vector>

#include <talk/base/sigslot.h>

#include "mozilla/RefPtr.h"
#include "mozilla/Scoped.h"
#include "nsCOMPtr.h"
#include "nsIEventTarget.h"
#include "nsITimer.h"

#include "m_cpp_utils.h"


typedef struct nr_ice_media_stream_ nr_ice_media_stream;

class NrIceCtx;

class NrIceMediaStream {
 public:
  static mozilla::RefPtr<NrIceMediaStream> Create(NrIceCtx *ctx,
                                           const std::string& name,
                                           int components);

  // The name of the stream
  const std::string& name() const { return name_; }

  // Parse remote candidates
  nsresult ParseCandidates(std::vector<std::string>& candidates);

  // The underlying nICEr stream
  nr_ice_media_stream *stream() { return stream_; }
  // Signals to indicate events. API users can (and should)
  // register for these.

  // Send a packet
  nsresult SendPacket(int component_id, const unsigned char *data, size_t len);

  sigslot::signal2<NrIceMediaStream *, const std::string& >
    SignalCandidate;  // A new ICE candidate:
  sigslot::signal1<NrIceMediaStream *> SignalReady;  // Candidate pair ready.
  sigslot::signal1<NrIceMediaStream *> SignalFailed;  // Candidate pair failed.
  sigslot::signal4<NrIceMediaStream *, int, const unsigned char *, int>
    SignalPacketReceived;  // Incoming packet

  // Emit all the ICE candidates. Note that this doesn't 
  // work for trickle ICE yet--called internally
  void EmitAllCandidates();

  
  // Allow this to be refcountable
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NrIceMediaStream);
  
 private:
  NrIceMediaStream(NrIceCtx *ctx,  const std::string& name,
                   int components)
      : ctx_(ctx), name_(name), components_(components), stream_(NULL)  {}
  ~NrIceMediaStream();
  DISALLOW_COPY_ASSIGN(NrIceMediaStream);

  NrIceCtx *ctx_;
  const std::string name_;
  const int components_;
  nr_ice_media_stream *stream_;
};


#endif
