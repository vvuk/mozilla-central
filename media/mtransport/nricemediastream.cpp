/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

// Some of this code is cut-and-pasted from nICEr. Copyright is:

/*
Copyright (c) 2007, Adobe Systems, Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of Adobe Systems, Network Resonance nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include <string>
#include <vector>

#include "nsError.h"

// nICEr includes
extern "C" {
#include "nr_api.h"
#include "registry.h"
#include "async_timer.h"
#include "ice_util.h"
#include "transport_addr.h"
#include "nr_crypto.h"
#include "nr_socket.h"
#include "nr_socket_local.h"
#include "stun_client_ctx.h"
#include "stun_server_ctx.h"
#include "ice_ctx.h"
#include "ice_candidate.h"
#include "ice_handler.h"
}

// Local includes
#include "logging.h"
#include "nricectx.h"
#include "nricemediastream.h"

MLOG_INIT("mtransport");

// NrIceMediaStream
mozilla::RefPtr<NrIceMediaStream>
NrIceMediaStream::Create(NrIceCtx *ctx,
                         const std::string& name,
                         int components) {
  mozilla::RefPtr<NrIceMediaStream> stream =
    new NrIceMediaStream(ctx, name, components);
  
  int r = nr_ice_add_media_stream(ctx->ctx(),
                                  const_cast<char *>(name.c_str()),
                                  components, &stream->stream_);
  if (r) {
    MLOG(PR_LOG_ERROR, "Couldn't create ICE media stream for '"
         << name << "'");
    return NULL;
  }

  return stream;
}

NrIceMediaStream::~NrIceMediaStream() {
  // TODO(ekr@rtfm.com): Implement this
}
                                           
nsresult NrIceMediaStream::ParseCandidates(std::vector<std::string>&
                                           candidates) {
  std::vector<char *> candidates_in;

  for (size_t i=0; i<candidates.size(); ++i) {
    candidates_in.push_back(const_cast<char *>(candidates[i].c_str()));
  }
  
  int r = nr_ice_peer_ctx_parse_stream_attributes(ctx_->peer(),
                                                  stream_,
                                                  &candidates_in[0],
                                                  candidates_in.size());
  if (r) {
    MLOG(PR_LOG_ERROR, "Couldn't parse attributes for stream "
         << name_ << "'");
    return NS_ERROR_FAILURE;
  }
  
  return NS_OK;
}

void NrIceMediaStream::EmitAllCandidates() {
  char **attrs = 0;
  int attrct;
  int r;
  r = nr_ice_media_stream_get_attributes(stream_,
                                         &attrs, &attrct);
  if (r) {
    MLOG(PR_LOG_ERROR, "Couldn't get ICE candidates for '"
         << name_ << "'");
    return;
  }
  
  for (size_t i=0; i<attrct; i++) {
    SignalCandidate(this, attrs[i]);
    RFREE(attrs[i]);
  }

  RFREE(attrs);
}

nsresult NrIceMediaStream::SendPacket(int component_id,
                                      const unsigned char *data,
                                      size_t len) {
  int r = nr_ice_media_stream_send(ctx_->peer(), stream_,
                                   component_id,
                                   const_cast<unsigned char *>(data), len);
  if (r) {
    MLOG(PR_LOG_ERROR, "Couldn't send media on '" << name_ << "'");
    if (r == R_WOULDBLOCK) {
      return NS_BASE_STREAM_WOULD_BLOCK;
    }

    return NS_BASE_STREAM_OSERROR;
  }

  return NS_OK;
}
