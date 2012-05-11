/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include <queue>

#include "nspr.h"
#include "nss.h"
#include "prerror.h"
#include "prio.h"
#include "ssl.h"
#include "sslerr.h"
#include "sslproto.h"

#include "logging.h"
#include "transportflow.h"
#include "transportlayerdtls.h"

MLOG_INIT("mtransport");

PRDescIdentity TransportLayerDtls::nspr_layer_identity = PR_INVALID_IO_LAYER;

std::string TransportLayerDtls::ID("mt_dtls");

#define UNIMPLEMENTED MLOG(PR_LOG_ERROR, \
    "Call to unimplemented function "<< __FUNCTION__); PR_ASSERT(false)

// TODO(ekr@rtfm.com): Surely there is something floating around in
// the Moz code base that will do this job.
// Note: using NSPR signatures rather than more modern signatures
// because this is only useful for NSPR.
struct Packet {
  Packet() : data_(NULL), len_(0), offset_(0) {}
  ~Packet() {
    delete data_;
  }
  void Assign(const void *data, PRInt32 len) {
    data_ = new unsigned char[len];
    memcpy(data_, data, len);
  }

  unsigned char *data_;
  PRInt32 len_;
  PRInt32 offset_;
};

void NSPRHelper::PacketReceived(const void *data, PRInt32 len) {
  input_.push(new Packet());
  input_.back()->Assign(data, len);
}

PRInt32 NSPRHelper::Read(void *data, PRInt32 len) {
  if (input_.empty()) {
    PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
    return -1;
  }
    
  Packet* front = input_.front();
  PRInt32 to_read = std::min(len, front->len_ - front->offset_);
  memcpy(data, front->data_, to_read);
  front->offset_ += to_read;
    
  if (front->offset_ == front->len_) {
    input_.pop();
    delete front;
  }

  return to_read;
}

PRInt32 NSPRHelper::Write(const void *buf, PRInt32 length) {
  TransportResult r = output_->SendPacket(
      static_cast<const unsigned char *>(buf), length);
  if (r >= 0) {
    return r;
  }

  if (r == TE_WOULDBLOCK) {
    PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
  } else {
    PR_SetError(PR_IO_ERROR, 0);
  }

  return -1;
}


// Implementation of NSPR methods
static PRStatus TransportLayerClose(PRFileDesc *f) {
  // Noop
  return PR_SUCCESS;
}

static PRInt32 TransportLayerRead(PRFileDesc *f, void *buf, PRInt32 length) {
  NSPRHelper *io = reinterpret_cast<NSPRHelper *>(f->secret);
  return io->Read(buf, length);
  return -1;
}

static PRInt32 TransportLayerWrite(PRFileDesc *f, const void *buf, PRInt32 length) {
  NSPRHelper *io = reinterpret_cast<NSPRHelper *>(f->secret);
  return io->Write(buf, length);
}

static PRInt32 TransportLayerAvailable(PRFileDesc *f) {
  UNIMPLEMENTED;
  return -1;
}

PRInt64 TransportLayerAvailable64(PRFileDesc *f) {
  UNIMPLEMENTED;
  return -1;
}

static PRStatus TransportLayerSync(PRFileDesc *f) {
  UNIMPLEMENTED;
  return PR_FAILURE;
}

static PROffset32 TransportLayerSeek(PRFileDesc *f, PROffset32 offset,
                             PRSeekWhence how) {
  UNIMPLEMENTED;
  return -1;
}

static PROffset64 TransportLayerSeek64(PRFileDesc *f, PROffset64 offset,
                               PRSeekWhence how) {
  UNIMPLEMENTED;
  return -1;
}

static PRStatus TransportLayerFileInfo(PRFileDesc *f, PRFileInfo *info) {
  UNIMPLEMENTED;
  return PR_FAILURE;
}

static PRStatus TransportLayerFileInfo64(PRFileDesc *f, PRFileInfo64 *info) {
  UNIMPLEMENTED;
  return PR_FAILURE;
}

static PRInt32 TransportLayerWritev(PRFileDesc *f, const PRIOVec *iov,
                     PRInt32 iov_size, PRIntervalTime to) {
  UNIMPLEMENTED;
  return -1;
}

static PRStatus TransportLayerConnect(PRFileDesc *f, const PRNetAddr *addr,
                              PRIntervalTime to) {
  UNIMPLEMENTED;
  return PR_FAILURE;
}

static PRFileDesc *TransportLayerAccept(PRFileDesc *sd, PRNetAddr *addr,
                                PRIntervalTime to) {
  UNIMPLEMENTED;
  return NULL;
}

static PRStatus TransportLayerBind(PRFileDesc *f, const PRNetAddr *addr) {
  UNIMPLEMENTED;
  return PR_FAILURE;
}

static PRStatus TransportLayerListen(PRFileDesc *f, PRIntn depth) {
  UNIMPLEMENTED;
  return PR_FAILURE;
}

static PRStatus TransportLayerShutdown(PRFileDesc *f, PRIntn how) {
  UNIMPLEMENTED;
  return PR_FAILURE;
}

// Note: this is always nonblocking and assumes a zero timeout.
// This function does not support peek.
static PRInt32 TransportLayerRecv(PRFileDesc *f, void *buf, PRInt32 amount,
                   PRIntn flags, PRIntervalTime to) {
  PR_ASSERT(flags == 0);

  if (flags != 0) {
    PR_SetError(PR_NOT_IMPLEMENTED_ERROR, 0);
    return -1;
  }

  return TransportLayerRead(f, buf, amount);
}

// Note: this is always nonblocking and assumes a zero timeout.
// This function does not support peek.
static PRInt32 TransportLayerSend(PRFileDesc *f, const void *buf, PRInt32 amount,
                   PRIntn flags, PRIntervalTime to) {
  PRInt32 written = TransportLayerWrite(f, buf, amount);

  return written;
}

static PRInt32 TransportLayerRecvfrom(PRFileDesc *f, void *buf, PRInt32 amount,
                       PRIntn flags, PRNetAddr *addr, PRIntervalTime to) {
  UNIMPLEMENTED;
  return -1;
}

static PRInt32 TransportLayerSendto(PRFileDesc *f, const void *buf, PRInt32 amount,
                     PRIntn flags, const PRNetAddr *addr, PRIntervalTime to) {
  UNIMPLEMENTED;
  return -1;
}

static PRInt16 TransportLayerPoll(PRFileDesc *f, PRInt16 in_flags, PRInt16 *out_flags) {
  UNIMPLEMENTED;
  return -1;
}

static PRInt32 TransportLayerAcceptRead(PRFileDesc *sd, PRFileDesc **nd,
                                PRNetAddr **raddr,
                                void *buf, PRInt32 amount, PRIntervalTime t) {
  UNIMPLEMENTED;
  return -1;
}

static PRInt32 TransportLayerTransmitFile(PRFileDesc *sd, PRFileDesc *f,
                                  const void *headers, PRInt32 hlen,
                                  PRTransmitFileFlags flags, PRIntervalTime t) {
  UNIMPLEMENTED;
  return -1;
}

static PRStatus TransportLayerGetpeername(PRFileDesc *f, PRNetAddr *addr) {
  // TODO(ekr@rtfm.com): Modify to return unique names for each channel
  // somehow, as opposed to always the same static address. The current
  // implementation messes up the session cache, which is why it's off
  // elsewhere
  addr->inet.family = PR_AF_INET;
  addr->inet.port = 0;
  addr->inet.ip = 0;

  return PR_SUCCESS;
}

static PRStatus TransportLayerGetsockname(PRFileDesc *f, PRNetAddr *addr) {
  UNIMPLEMENTED;
  return PR_FAILURE;
}

static PRStatus TransportLayerGetsockoption(PRFileDesc *f, PRSocketOptionData *opt) {
  switch (opt->option) {
    case PR_SockOpt_Nonblocking:
      opt->value.non_blocking = PR_TRUE;
      return PR_SUCCESS;
    default:
      UNIMPLEMENTED;
      break;
  }

  return PR_FAILURE;
}

// Imitate setting socket options. These are mostly noops.
static PRStatus TransportLayerSetsockoption(PRFileDesc *f,
                                    const PRSocketOptionData *opt) {
  switch (opt->option) {
    case PR_SockOpt_Nonblocking:
      return PR_SUCCESS;
    case PR_SockOpt_NoDelay:
      return PR_SUCCESS;
    default:
      UNIMPLEMENTED;
      break;
  }

  return PR_FAILURE;
}

static PRInt32 TransportLayerSendfile(PRFileDesc *out, PRSendFileData *in,
                              PRTransmitFileFlags flags, PRIntervalTime to) {
  UNIMPLEMENTED;
  return -1;
}

static PRStatus TransportLayerConnectContinue(PRFileDesc *f, PRInt16 flags) {
  UNIMPLEMENTED;
  return PR_FAILURE;
}

static PRIntn TransportLayerReserved(PRFileDesc *f) {
  UNIMPLEMENTED;
  return -1;
}

static const struct PRIOMethods nss_methods = {
  PR_DESC_LAYERED,
  TransportLayerClose,
  TransportLayerRead,
  TransportLayerWrite,
  TransportLayerAvailable,
  TransportLayerAvailable64,
  TransportLayerSync,
  TransportLayerSeek,
  TransportLayerSeek64,
  TransportLayerFileInfo,
  TransportLayerFileInfo64,
  TransportLayerWritev,
  TransportLayerConnect,
  TransportLayerAccept,
  TransportLayerBind,
  TransportLayerListen,
  TransportLayerShutdown,
  TransportLayerRecv,
  TransportLayerSend,
  TransportLayerRecvfrom,
  TransportLayerSendto,
  TransportLayerPoll,
  TransportLayerAcceptRead,
  TransportLayerTransmitFile,
  TransportLayerGetsockname,
  TransportLayerGetpeername,
  TransportLayerReserved,
  TransportLayerReserved,
  TransportLayerGetsockoption,
  TransportLayerSetsockoption,
  TransportLayerSendfile,
  TransportLayerConnectContinue,
  TransportLayerReserved,
  TransportLayerReserved,
  TransportLayerReserved,
  TransportLayerReserved
};

void TransportLayerDtls::WasInserted() {
  helper_ = new NSPRHelper(downward_);

  if (!nspr_layer_identity == PR_INVALID_IO_LAYER) {
    nspr_layer_identity = PR_GetUniqueIdentity("nssstreamadapter");
  }
  pr_fd_ = PR_CreateIOLayerStub(nspr_layer_identity, &nss_methods);
  PR_ASSERT(pr_fd_ != NULL);
  if (!pr_fd_)
    SetState(ERROR);
  pr_fd_->secret = reinterpret_cast<PRFilePrivate *>(helper_);

  PRFileDesc *ssl_fd;
  if (mode_ == DGRAM) {
    ssl_fd = DTLS_ImportFD(NULL, pr_fd_);
  } else {
    ssl_fd = SSL_ImportFD(NULL, pr_fd_);
  }
  PR_ASSERT(ssl_fd != NULL);  // This should never happen
  if (!ssl_fd) {
    PR_Close(pr_fd_);
    pr_fd_ = NULL;
    SetState(ERROR);
  }

}
