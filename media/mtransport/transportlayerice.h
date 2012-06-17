/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

// This is a wrapper around the nICEr ICE stack
#ifndef transportlayerice_h__
#define transportlayerice_h__

#include <vector>

#include <talk/base/sigslot.h>

#include "mozilla/RefPtr.h"
#include "mozilla/Scoped.h"
#include "nsCOMPtr.h"
#include "nsIEventTarget.h"
#include "nsITimer.h"

#include "m_cpp_utils.h"


typedef struct nr_ice_ctx_ nr_ice_ctx;
typedef struct nr_ice_peer_ctx_ nr_ice_peer_ctx;
typedef struct nr_ice_media_stream_ nr_ice_media_stream;
typedef struct nr_ice_handler_ nr_ice_handler;
typedef struct nr_ice_handler_vtbl_ nr_ice_handler_vtbl;
typedef struct nr_ice_cand_pair_ nr_ice_cand_pair;

class NrIceMediaStream;

class NrIceCtx {
 public:
  static mozilla::RefPtr<NrIceCtx> Create(const std::string& name,
                                          bool offerer);
  virtual ~NrIceCtx();
  
  nr_ice_ctx *ctx() { return ctx_; }
  nr_ice_peer_ctx *peer() { return peer_; }

  // Create a media stream
  mozilla::RefPtr<NrIceMediaStream> CreateStream(const std::string& name,
                                                 int components);

  // The name of the ctx
  const std::string& name() const { return name_; }

  // Get the global attributes
  std::vector<std::string> GetGlobalAttributes();

  // Set the other side's global attributes
  nsresult ParseGlobalAttributes(std::vector<std::string> attrs);

  // Start ICE gathering
  nsresult StartGathering();

  // Start checking
  nsresult StartChecks();
  
  // Signals to indicate events. API users can (and should)
  // register for these.
  sigslot::signal1<NrIceCtx *> SignalGatheringCompleted;  // Done gathering
  sigslot::signal1<NrIceCtx *> SignalCompleted;  // Done handshaking

  // Allow this to be refcountable
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NrIceCtx);

 private:
  NrIceCtx(const std::string& name, bool offerer) 
      : name_(name),
        offerer_(offerer),
        streams_(),
        ctx_(NULL),
        peer_(NULL),
        ice_handler_vtbl_(NULL),
        ice_handler_(NULL) {}

  DISALLOW_COPY_ASSIGN(NrIceCtx);

  // Callbacks for nICEr 
  static void initialized_cb(void *s, int h, void *arg);  // ICE initialized

  // Handler implementation
  static int select_pair(void *obj,nr_ice_media_stream *stream, 
                         int component_id, nr_ice_cand_pair **potentials,
                         int potential_ct);
  static int stream_ready(void *obj, nr_ice_media_stream *stream);
  static int stream_failed(void *obj, nr_ice_media_stream *stream);
  static int ice_completed(void *obj, nr_ice_peer_ctx *pctx);
  static int msg_recvd(void *obj, nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream, int component_id,
                       unsigned char *msg, int len);

  // Iterate through all media streams and emit the candidates
  // Note that we don't do trickle ICE yet
  void EmitAllCandidates();

  const std::string name_;
  bool offerer_;
  std::vector<mozilla::RefPtr<NrIceMediaStream> > streams_;
  nr_ice_ctx *ctx_;
  nr_ice_peer_ctx *peer_;
  nr_ice_handler_vtbl *ice_handler_vtbl_;  // Must be pointer to isolate nICEr structs
  nr_ice_handler *ice_handler_;  // Must be pointer to isolate nICEr structs
};


class NrIceMediaStream {
 public:
  static mozilla::RefPtr<NrIceMediaStream> Create(mozilla::RefPtr<NrIceCtx> ctx,
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


  // A new ICE candidate:
  sigslot::signal2<NrIceMediaStream *, const std::string& > SignalCandidate;

  sigslot::signal1<NrIceMediaStream *> SignalReady;  // Candidate pair ready.
  sigslot::signal1<NrIceMediaStream *> SignalFailed;  // Candidate pair failed.

  // Emit all the ICE candidates. Note that this doesn't 
  // work for trickle ICE yet--called internally
  void EmitAllCandidates();

  
  // Allow this to be refcountable
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NrIceMediaStream);
  
 private:
  NrIceMediaStream(mozilla::RefPtr<NrIceCtx> ctx,  const std::string& name,
                   int components)
      : ctx_(ctx), name_(name), components_(components), stream_(NULL)  {}
  ~NrIceMediaStream();


                   
  DISALLOW_COPY_ASSIGN(NrIceMediaStream);

  mozilla::RefPtr<NrIceCtx> ctx_;
  const std::string name_;
  const int components_;
  nr_ice_media_stream *stream_;
};











class TransportLayerIce;

// An ICE transport layer -- corresponds to a single ICE
class TransportLayerIce {
 public:
  TransportLayerIce(const std::string& name,
                    NrIceCtx *ctx);
  virtual ~TransportLayerIce();

  // Allow this to be refcountable
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TransportLayerIce);

  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

 private:
  DISALLOW_COPY_ASSIGN(TransportLayerIce);

  NrIceCtx *ctx_;  // The parent context
  nr_ice_media_stream *media_stream;  // The media stream for this
  const std::string name_;
  nr_ice_media_stream *stream_;
};

#endif
