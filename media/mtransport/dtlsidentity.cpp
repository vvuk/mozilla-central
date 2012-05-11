#include "nspr.h"
#include "cert.h"
#include "cryptohi.h"
#include "hasht.h"
#include "keyhi.h"
#include "nss.h"
#include "pk11pub.h"
#include "sechash.h"
#include "ssl.h"

#include "dtlsidentity.h"

// Helper class to avoid having a crapload of if (!NULL) statements at
// the end to clean up. The way you use this is you instantiate the
// object as scoped_c_ptr<PtrType> obj(value, destructor);
// TODO(ekr@rtfm.com): Move this to some generic location
template <class T> class scoped_c_ptr {
 public:
  scoped_c_ptr(T *t, void (*d)(T *)) : t_(t), d_(d) {} 
  scoped_c_ptr(void (*d)(T *)) : t_(NULL), d_(d) {}

  void reset(T *t) { t_ = t; }
  T* forget() { T* t = t_; t_ = NULL; return t; }
  T *get() { return t_; }
  ~scoped_c_ptr() {
    if (t_) {
      d_(t_);
    }
  }
  void operator=(T *t) {
    t_ = t;
  }

 private:       
  // TODO: implement copy and assignment operators
  // to remove danger
  T *t_;
  void (*d_)(T *);
};

// Auto-generate an identity based on name=name
DtlsIdentity *DtlsIdentity::Generate(const std::string name) {
  SECStatus rv;

  std::string subject_name_string = "CN=" + name;  
  CERTName *subject_name = CERT_AsciiToName(
      const_cast<char *>(subject_name_string.c_str()));
  if (!subject_name) {
    return NULL;
  }

  PK11RSAGenParams rsaparams;
  rsaparams.keySizeInBits = 1024;
  rsaparams.pe = 0x010001;

  scoped_c_ptr<SECKEYPrivateKey> private_key(SECKEY_DestroyPrivateKey);
  scoped_c_ptr<SECKEYPublicKey> public_key(SECKEY_DestroyPublicKey);
  SECKEYPublicKey *pubkey;

  private_key = PK11_GenerateKeyPair(PK11_GetInternalSlot(),
                                 CKM_RSA_PKCS_KEY_PAIR_GEN, &rsaparams, &pubkey,
                                 PR_FALSE, PR_TRUE, NULL);
  if (private_key.get() == NULL)
    return NULL;
  public_key= pubkey;
  
  scoped_c_ptr<CERTSubjectPublicKeyInfo> spki(SECKEY_DestroySubjectPublicKeyInfo);
  spki = SECKEY_CreateSubjectPublicKeyInfo(pubkey);
  if (!spki.get()) {
    return NULL;
  }

  scoped_c_ptr<CERTCertificateRequest> certreq(CERT_DestroyCertificateRequest);
  certreq = CERT_CreateCertificateRequest(subject_name, spki.get(), NULL);
  if (!certreq.get()) {
    return NULL;
  }

  PRTime notBefore = PR_Now() - (86400UL * PR_USEC_PER_SEC);
  PRTime notAfter = PR_Now() + (86400UL * 30 * PR_USEC_PER_SEC);

  scoped_c_ptr<CERTValidity> validity(CERT_DestroyValidity);
  validity = CERT_CreateValidity(notBefore, notAfter);
  if (!validity.get()) {
    return NULL;
  }

  unsigned long serial;
  // Note: This serial in principle could collide, but it's unlikely
  rv = PK11_GenerateRandom(reinterpret_cast<unsigned char *>(&serial),
                           sizeof(serial));
  if (rv != SECSuccess) {
    return NULL;
  }

  scoped_c_ptr<CERTCertificate> certificate(CERT_DestroyCertificate);
  certificate = CERT_CreateCertificate(serial, subject_name, validity.get(), certreq.get());
  if (!certificate.get()) {
    return NULL;
  }

  PRArenaPool *arena = certificate.get()->arena;

  rv = SECOID_SetAlgorithmID(arena, &certificate.get()->signature,
                             SEC_OID_PKCS1_SHA1_WITH_RSA_ENCRYPTION, 0);
  if (rv != SECSuccess)
    return NULL;

  // Set version to X509v3.
  *(certificate.get()->version.data) = 2;
  certificate.get()->version.len = 1;

  SECItem innerDER;
  innerDER.len = 0;
  innerDER.data = NULL;

  if (!SEC_ASN1EncodeItem(arena, &innerDER, certificate.get(),
                          SEC_ASN1_GET(CERT_CertificateTemplate)))
    return NULL;

  SECItem *signedCert 
      = reinterpret_cast<SECItem *>(PORT_ArenaZAlloc(arena, sizeof(SECItem)));
  if (!signedCert) {
    return NULL;
  }

  rv = SEC_DerSignData(arena, signedCert, innerDER.data, innerDER.len,
                       private_key.get(),
                       SEC_OID_PKCS1_SHA1_WITH_RSA_ENCRYPTION);
  if (rv != SECSuccess) {
    return NULL;
  }
  certificate.get()->derCert = *signedCert;

  return new DtlsIdentity(private_key.forget(), certificate.forget());
}

DtlsIdentity::~DtlsIdentity() {
  if (privkey_)
    SECKEY_DestroyPrivateKey(privkey_);

  if(cert_)
    CERT_DestroyCertificate(cert_);
}



