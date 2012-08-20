/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef transportlayerdtls_h__
#define transportlayerdtls_h__

#include <queue>

#include <talk/base/sigslot.h>

#include "nspr.h"
#include "prio.h"

#include "mozilla/RefPtr.h"
#include "mozilla/Scoped.h"
#include "nsCOMPtr.h"
#include "nsIEventTarget.h"
#include "nsITimer.h"

#include "m_cpp_utils.h"
#include "dtlsidentity.h"
#include "transportflow.h"
#include "transportlayer.h"

struct Packet;

class NSPRHelper {
 public:
  NSPRHelper(TransportLayer *output) :
      output_(output),
      input_() {}

  void PacketReceived(const void *data, PRInt32 len);
  PRInt32 Read(void *data, PRInt32 len);
  PRInt32 Write(const void *buf, PRInt32 length);

 private:
  DISALLOW_COPY_ASSIGN(NSPRHelper);

  TransportLayer *output_;
  std::queue<Packet *> input_;
};


class TransportLayerDtls : public TransportLayer {
public:
 TransportLayerDtls() :
     TransportLayer(DGRAM),
     identity_(NULL),
     role_(CLIENT),
     verification_mode_(VERIFY_UNSET),
     digest_algorithm_(),
     digest_len_(0),
     digest_value_(),
     pr_fd_(NULL),
     ssl_fd_(NULL),
     helper_(NULL),
     peer_cert_(NULL),
     auth_hook_called_(false) {}

  virtual ~TransportLayerDtls();


  enum Role { CLIENT, SERVER};
  enum Verification { VERIFY_UNSET, VERIFY_ALLOW_ALL, VERIFY_DIGEST};
  const static int kMaxDigestLength = 64;

  // DTLS-specific operations
  void SetRole(Role role) { role_ = role;}
  Role role() { return role_; }

  void SetIdentity(mozilla::RefPtr<DtlsIdentity> identity) {
    identity_ = identity;
  }
  nsresult SetVerificationAllowAll();
  nsresult SetVerificationDigest(const std::string digest_algorithm,
                                 const unsigned char *digest_value,
                                 size_t digest_len);

  nsresult SetSrtpCiphers(std::vector<PRUint16> ciphers);
  nsresult GetSrtpCipher(PRUint16 *cipher);

  nsresult ExportKeyingMaterial(const std::string& label,
                                bool use_context,
                                const std::string& context,
                                unsigned char *out,
                                unsigned int outlen);

  const CERTCertificate *GetPeerCert() const { return peer_cert_; }

  // Transport layer overrides.
  virtual nsresult InitInternal();
  virtual void WasInserted();
  virtual TransportResult SendPacket(const unsigned char *data, size_t len);

  // Signals
  void StateChange(TransportLayer *layer, State state);
  void PacketReceived(TransportLayer* layer, const unsigned char *data,
      size_t len);

  // Return the layer id for this layer
  virtual const std::string& id() { return ID; }

  // A static version of the layer ID
  static std::string ID;

private:
  DISALLOW_COPY_ASSIGN(TransportLayerDtls);

  bool Setup();
  void Handshake();

  static SECStatus GetClientAuthDataHook(void *arg, PRFileDesc *fd,
                                         CERTDistNames *caNames,
                                         CERTCertificate **pRetCert,
                                         SECKEYPrivateKey **pRetKey);
  static SECStatus AuthCertificateHook(void *arg,
                                       PRFileDesc *fd,
                                       PRBool checksig,
                                       PRBool isServer);
  SECStatus AuthCertificateHook(PRFileDesc *fd,
                                PRBool checksig,
                                PRBool isServer);

  static void TimerCallback(nsITimer *timer, void *arg);

  mozilla::RefPtr<DtlsIdentity> identity_;
  std::vector<PRUint16> srtp_ciphers_;

  Role role_;
  Verification verification_mode_;
  std::string digest_algorithm_;
  size_t digest_len_;
  unsigned char digest_value_[kMaxDigestLength];

  PRFileDesc *pr_fd_;
  PRFileDesc *ssl_fd_;
  mozilla::ScopedDeletePtr<NSPRHelper> helper_;
  CERTCertificate *peer_cert_;
  nsCOMPtr<nsIEventTarget> target_;
  nsCOMPtr<nsITimer> timer_;
  bool auth_hook_called_;

  static PRDescIdentity nspr_layer_identity;  // The NSPR layer identity
};


#endif
