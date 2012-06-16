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
  void StartGathering(nsresult *res);


  // Signals to indicate events. API users can (and should)
  // register for these.
  sigslot::signal1<NrIceCtx *> SignalGatheringComplete;  // Done gathering
  
  // Allow this to be refcountable
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NrIceCtx);

 private:
  NrIceCtx(const std::string& name, bool offerer)
      : name_(name),
        offerer_(offerer),
        streams_(),
        ctx_(NULL),
        peer_(NULL),
        ice_handler_(NULL) {
  }

  DISALLOW_COPY_ASSIGN(NrIceCtx);

  static void initialized_cb(int s, int h, void *arg);

  // Iterate through all media streams and emit the candidates
  // Note that we don't do trickle ICE yet
  void EmitAllCandidates();

  const std::string name_;
  bool offerer_;
  std::vector<mozilla::RefPtr<NrIceMediaStream> > streams_;
  nr_ice_ctx *ctx_;
  nr_ice_peer_ctx *peer_;
  nr_ice_handler *ice_handler_;
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

  // Signals to indicate events. API users can (and should)
  // register for these.

  // A new ICE candidate
  sigslot::signal2<NrIceMediaStream *, const std::string& > SignalCandidate;
  

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
