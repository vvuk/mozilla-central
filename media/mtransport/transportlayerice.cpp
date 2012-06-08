/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include <string>
#include <queue>

#include "nspr.h"
#include "nss.h"
#include "pk11pub.h"
#include "prlog.h"

#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsIEventTarget.h"
#include "nsNetCID.h"
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"

// nICEr includes
extern "C" {
#include "nr_api.h"
#include "registry.h"
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
#include "transportflow.h"
#include "transportlayerice.h"


MLOG_INIT("mtransport");

std::string TransportLayerIce::ID("mt_ice");
static int initialized = 0;


// Implement NSPR-based crypto algorithms
static int nr_crypto_nss_random_bytes(UCHAR *buf, int len) {
  SECStatus rv;

  rv = PK11_GenerateRandom(buf, len);
  if (rv != SECSuccess)
    return R_INTERNAL;

  return 0;
}

static int nr_crypto_nss_hmac(UCHAR *key, int keyl, UCHAR *buf, int bufl,
                              UCHAR *result) {
  CK_MECHANISM_TYPE mech = CKM_SHA_1_HMAC;
  PK11SlotInfo *slot = 0;
  SECItem keyi = { siBuffer, key, keyl};
  PK11SymKey *skey = 0;
  PK11Context *hmac_ctx = 0;
  SECStatus status;
  unsigned int hmac_len;
  SECItem param = { siBuffer, NULL, 0 };
  int err = R_INTERNAL;
  
  slot = PK11_GetInternalKeySlot();
  if (!slot)
    goto abort;

  skey = PK11_ImportSymKey(slot, mech, PK11_OriginUnwrap,
                          CKA_SIGN, &keyi, NULL);
  if (!skey)
    goto abort;

  
  hmac_ctx = PK11_CreateContextBySymKey(mech, CKA_SIGN,
                                        skey, &param);

  status = PK11_DigestBegin(hmac_ctx);
  if (status != SECSuccess)
    goto abort;

  status = PK11_DigestOp(hmac_ctx, buf, bufl);
  if (status != SECSuccess)
    goto abort;

  status = PK11_DigestFinal(hmac_ctx, result, &hmac_len, 20);
  if (status != SECSuccess)
    goto abort;
  
  PR_ASSERT(hmac_len == 20);
  
  err = 0;

 abort:
  if(hmac_ctx) PK11_DestroyContext(hmac_ctx, PR_TRUE);
  if (skey) PK11_FreeSymKey(skey);
  if (slot) PK11_FreeSlot(slot);

  return err;
}

static nr_ice_crypto_vtbl nr_ice_crypto_nss_vtbl = {
  nr_crypto_nss_random_bytes,
  nr_crypto_nss_hmac
};



TransportLayerIceCtx::TransportLayerIceCtx(const std::string& name, bool offerer) :
    name_(name), ctx_(NULL) {
  int r;
  
  if (!initialized) {
    nr_crypto_vtbl = &nr_ice_crypto_nss_vtbl;
    NR_reg_init(NR_REG_MODE_LOCAL);
    initialized = 1;
  }
}

nsresult TransportLayerIceCtx::Init() {
  // Create the ICE context
  r = nr_ice_ctx_create(const_cast<char *>(name_.c_str()),
                        offerer ? 
                        NR_ICE_CTX_FLAGS_OFFERER :
                        NR_ICE_CTX_FLAGS_ANSWERER,
                        &ctx_);
  if (r) {
    MLOG(PR_LOG_ERROR, "Couldn't create ICE ctx for '" << name_ << "'");
    return NS_ERROR_FAILURE;
  }
  
  return NS_OK;
}



TransportLayerIceCtx::~TransportLayerIceCtx() {
  // TODO(ekr@rtfm.com): Implement this
}


