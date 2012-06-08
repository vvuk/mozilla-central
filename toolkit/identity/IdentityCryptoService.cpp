/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIIdentityCryptoService.h"
#include "mozilla/ModuleUtils.h"
#include "nsComponentManagerUtils.h"
#include "nsNSSShutDown.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"
#include "nsCOMPtr.h"
#include "nsStringGlue.h"

#include "plbase64.h"
#include "nss.h"
#include "pk11pub.h"
#include "secmod.h"
#include "secerr.h"
#include "keyhi.h"
#include "cryptohi.h"

using namespace mozilla;

namespace {

void
HexEncode(const SECItem * it, nsACString & result)
{
  const char * digits = "01234567890ABCDEF";
  result.SetCapacity((it->len * 2) + 1);
  result.SetLength(it->len * 2);
  char * p = result.BeginWriting();
  for (unsigned int i = 0; i < it->len; ++i) {
    *p++ = digits[it->data[i] >> 4];
    *p++ = digits[it->data[i] & 0x0f];
  }
} 

// Stolen from /toolkit/components/places/Helpers.cpp
// TODO: use the new base64 encoder khuey wrote
// TODO: padding?
void
Base64urlEncode(const SECItem * it, nsACString & result)
{
  // SetLength does not set aside space for NULL termination.  PL_Base64Encode
  // will not NULL terminate, however, nsCStrings must be NULL terminated.  As a
  // result, we set the capacity to be one greater than what we need, and the
  // length to our desired length.
  PRUint32 length = (it->len + 2) / 3 * 4; // +2 due to integer math.
  result.SetCapacity(length + 1);
  result.SetLength(length);
  char * out = result.BeginWriting();
  (void)PL_Base64Encode(reinterpret_cast<const char*>(it->data), it->len, out);

  // base64url encoding is defined in RFC 4648.  It replaces the last two
  // alphabet characters of base64 encoding with '-' and '_' respectively.

  for (unsigned int i = 0; i < length; ++i) {
    if (out[i] == '+') {
      out[i] = '-';
    } else if (out[i] == '/') {
      out[i] = '_';
    }
  }
}

// IMPORTANT: This must be called immediately after the function returning the
// SECStatus result. The recommended usage is:
//    nsresult rv = MapSECStatus(f(x, y, z));
nsresult
MapSECStatus(SECStatus rv)
{
  if (rv == SECSuccess)
    return NS_OK;

  return NS_ERROR_UNEXPECTED; // XXX: map error codes
}


#define DSA_KEY_TYPE_STRING (NS_LITERAL_CSTRING("DS160"))
#define RSA_KEY_TYPE_STRING (NS_LITERAL_CSTRING("RS256"))

class KeyPair : public nsIIdentityKeyPair, public nsNSSShutDownObject
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIIDENTITYKEYPAIR
  
  KeyPair(SECKEYPrivateKey* aPrivateKey, SECKEYPublicKey* aPublicKey);

private:
    ~KeyPair()
    {
      destructorSafeDestroyNSSReference();
      shutdown(calledFromObject);
    }

    void virtualDestroyNSSReference() MOZ_OVERRIDE {
      destructorSafeDestroyNSSReference();
    }

    void destructorSafeDestroyNSSReference()
    {
      if (isAlreadyShutDown()) // XXX: too many places
        return;

      SECKEY_DestroyPrivateKey(mPrivateKey);
      mPrivateKey = NULL;
      SECKEY_DestroyPublicKey(mPublicKey);
      mPublicKey = NULL;
    }

    SECKEYPrivateKey * mPrivateKey;
    SECKEYPublicKey * mPublicKey;
};

NS_IMPL_THREADSAFE_ISUPPORTS1(KeyPair, nsIIdentityKeyPair)

class KeyGenRunnable : public nsRunnable, public nsNSSShutDownObject
{
public:
  NS_DECL_NSIRUNNABLE

  KeyGenRunnable(KeyType keyType, nsIIdentityKeyGenCallback * aCallback);  
private:
  ~KeyGenRunnable()
  {
    destructorSafeDestroyNSSReference();
    shutdown(calledFromObject);
  }

  virtual void virtualDestroyNSSReference() MOZ_OVERRIDE
  {
    destructorSafeDestroyNSSReference();
  }

  void destructorSafeDestroyNSSReference()
  {
    if (isAlreadyShutDown())
      return;

     mKeyPair = NULL;
  }

  const KeyType mKeyType; // in
  nsCOMPtr<nsIIdentityKeyGenCallback> mCallback; // in
  nsresult mRv; // out
  nsCOMPtr<KeyPair> mKeyPair; // out
};

class SignRunnable : public nsRunnable, public nsNSSShutDownObject
{
public:
  NS_DECL_NSIRUNNABLE

  SignRunnable(const nsACString & textToSign, SECKEYPrivateKey * privateKey,
               nsIIdentitySignCallback * aCallback);  
private:

  ~SignRunnable()
  {
    destructorSafeDestroyNSSReference();
    shutdown(calledFromObject);
  }

  void virtualDestroyNSSReference() MOZ_OVERRIDE
  {
    destructorSafeDestroyNSSReference();
  }

  void destructorSafeDestroyNSSReference()
  {
    if (isAlreadyShutDown())
      return;

    SECKEY_DestroyPrivateKey(mPrivateKey); // XXX smart pointer
    mPrivateKey = NULL;
  }

  const nsCString mTextToSign; // in
  SECKEYPrivateKey* mPrivateKey; // in
  const nsCOMPtr<nsIIdentitySignCallback> mCallback; // in
  nsresult mRv; // out
  nsCString mSignature; // out
};

class IdentityCryptoService : public nsIIdentityCryptoService
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIIDENTITYCRYPTOSERVICE

  IdentityCryptoService() { }
  nsresult Init()
  {
    nsresult rv;
    nsCOMPtr<nsISupports> dummyUsedToEnsureNSSIsInitialized
      = do_CreateInstance("@mozilla.org/security/keyobjectfactory;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }
private:
};

NS_IMPL_THREADSAFE_ISUPPORTS1(IdentityCryptoService, nsIIdentityCryptoService)

NS_IMETHODIMP
IdentityCryptoService::GenerateKeyPair(
  const nsACString & keyTypeString, nsIIdentityKeyGenCallback * callback)
{
  KeyType keyType;
  if (keyTypeString.Equals(RSA_KEY_TYPE_STRING)) {
    keyType = rsaKey;
  } else if (keyTypeString.Equals(DSA_KEY_TYPE_STRING)) {
    keyType = dsaKey;
  } else {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIRunnable> r = new KeyGenRunnable(keyType, callback);
  nsCOMPtr<nsIThread> thread;
  nsresult rv = NS_NewThread(getter_AddRefs(thread), r);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

KeyPair::KeyPair(SECKEYPrivateKey * privateKey, SECKEYPublicKey * publicKey)
  : mPrivateKey(SECKEY_CopyPrivateKey(privateKey))
  , mPublicKey(SECKEY_CopyPublicKey(publicKey))
{
  MOZ_ASSERT(!NS_IsMainThread());
}

NS_IMETHODIMP 
KeyPair::GetHexRSAPublicKeyExponent(nsACString & result)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(mPublicKey, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_TRUE(mPublicKey->keyType == rsaKey, NS_ERROR_UNEXPECTED);
  HexEncode(&mPublicKey->u.rsa.publicExponent, result);
  return NS_OK;
}

NS_IMETHODIMP 
KeyPair::GetHexRSAPublicKeyModulus(nsACString & result)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(mPublicKey, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_TRUE(mPublicKey->keyType == rsaKey, NS_ERROR_UNEXPECTED);
  HexEncode(&mPublicKey->u.rsa.modulus, result);
  return NS_OK;
}

NS_IMETHODIMP 
KeyPair::GetHexDSAPrime(nsACString & result)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(mPublicKey, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_TRUE(mPublicKey->keyType == dsaKey, NS_ERROR_UNEXPECTED);
  HexEncode(&mPublicKey->u.dsa.params.prime, result);
  return NS_OK;
}

NS_IMETHODIMP 
KeyPair::GetHexDSASubPrime(nsACString & result)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(mPublicKey, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_TRUE(mPublicKey->keyType == dsaKey, NS_ERROR_UNEXPECTED);
  HexEncode(&mPublicKey->u.dsa.params.subPrime, result);
  return NS_OK;
}

NS_IMETHODIMP 
KeyPair::GetHexDSAGenerator(nsACString & result)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(mPublicKey, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_TRUE(mPublicKey->keyType == dsaKey, NS_ERROR_UNEXPECTED);
  HexEncode(&mPublicKey->u.dsa.params.base, result);
  return NS_OK;
}

NS_IMETHODIMP 
KeyPair::GetHexDSAPublicValue(nsACString & result)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(mPublicKey, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_TRUE(mPublicKey->keyType == dsaKey, NS_ERROR_UNEXPECTED);
  HexEncode(&mPublicKey->u.dsa.publicValue, result);
  return NS_OK;
}

NS_IMETHODIMP
KeyPair::GetKeyType(nsACString & result)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(mPublicKey, NS_ERROR_NOT_AVAILABLE);

  switch (mPublicKey->keyType) {
    case rsaKey: result = RSA_KEY_TYPE_STRING; return NS_OK;
    case dsaKey: result = DSA_KEY_TYPE_STRING; return NS_OK;
    default: return NS_ERROR_UNEXPECTED;
  }
}

NS_IMETHODIMP
KeyPair::Sign(const nsACString & textToSign,
              nsIIdentitySignCallback* callback)
{
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIRunnable> r = new SignRunnable(textToSign, mPrivateKey,
                                             callback);

  nsCOMPtr<nsIThread> thread;
  nsresult rv = NS_NewThread(getter_AddRefs(thread), r);
  return rv;
}

KeyGenRunnable::KeyGenRunnable(KeyType keyType,
                               nsIIdentityKeyGenCallback * callback)
  : mKeyType(keyType)
  , mCallback(callback)
  , mRv(NS_ERROR_NOT_INITIALIZED)
{
}

MOZ_WARN_UNUSED_RESULT nsresult
GenerateRSAKeyPair(PK11SlotInfo * slot,
                   NS_OUTPARAM SECKEYPrivateKey ** privateKey,
                   NS_OUTPARAM SECKEYPublicKey ** publicKey)
{
  MOZ_ASSERT(!NS_IsMainThread());

  PK11RSAGenParams rsaParams;
  rsaParams.keySizeInBits = 2048;
  rsaParams.pe = 0x10001;

  *privateKey = PK11_GenerateKeyPair(slot, 
                                     CKM_RSA_PKCS_KEY_PAIR_GEN, 
                                     &rsaParams, 
                                     publicKey, 
                                     PR_FALSE /*isPerm*/, 
                                     PR_TRUE /*isSensitive*/, 
                                     NULL /*&pwdata*/);

  NS_ENSURE_TRUE(*privateKey, NS_ERROR_FAILURE); // XXX: PRErrorCode -> nsresult
  NS_ENSURE_TRUE(*privateKey, NS_ERROR_UNEXPECTED);
  MOZ_ASSERT(*publicKey);

  return NS_OK;
}

MOZ_WARN_UNUSED_RESULT nsresult
GenerateDSAKeyPair(PK11SlotInfo * slot,
                   NS_OUTPARAM SECKEYPrivateKey ** privateKey,
                   NS_OUTPARAM SECKEYPublicKey ** publicKey)
{
  MOZ_ASSERT(!NS_IsMainThread());

  // XXX: These could probably be static const arrays, but this way we avoid
  // compiler warnings and also we avoid having to worry much about whether the
  // functions that take these inputs will (unexpectedly) modify them.

  // Generated using openssl dsaparam -C 1024
  PRUint8 P[] = {
	  0x88,0x47,0xCB,0x96,0x52,0x83,0x9F,0xD1,0xEB,0xEF,0x18,0x03,
	  0x1B,0x9F,0xAA,0xBD,0x59,0x08,0x88,0x3B,0xB0,0xB5,0x78,0x79,
	  0x98,0x12,0xAE,0xA9,0xCB,0xD4,0x14,0x51,0x30,0xBB,0xE6,0x69,
	  0x96,0x81,0x81,0x80,0xF6,0x6E,0xBE,0xF9,0xF8,0x3F,0x7A,0x19,
	  0x3D,0x0B,0xF4,0x26,0x65,0x65,0x2D,0xA3,0xF6,0x15,0xFD,0x28,
	  0xD8,0x6D,0xD1,0x92,0xC5,0x4E,0xA9,0xD2,0xCB,0x9C,0xE9,0x30,
	  0xBB,0x9F,0x57,0x31,0x8E,0x6F,0xF6,0x2A,0xF7,0x26,0xB4,0xD7,
	  0xCD,0xF2,0x29,0xEB,0x53,0xEE,0xBA,0x0D,0xD6,0x70,0xE6,0xE3,
	  0x88,0xC4,0xAC,0xDF,0x51,0x8F,0x46,0x94,0x60,0xD6,0xAC,0xA8,
	  0xDE,0x71,0xA5,0xE2,0x42,0xC2,0x32,0xEB,0x06,0xAE,0xDC,0x44,
	  0xCB,0xC8,0xD6,0x7F,0x68,0xFA,0x90,0xAB
	};

  PRUint8 Q[] = {
	  0x86,0xF4,0x16,0xAE,0x15,0x45,0xC6,0x98,0x27,0x5F,0x85,0x06,
	  0xF6,0x56,0xCF,0x44,0x56,0xFD,0xD4,0x9D
	};

  PRUint8 G[] = {
	  0x3C,0x91,0xB2,0x2F,0x49,0x2E,0xAB,0x00,0x5D,0x6A,0x52,0x38,
	  0xFA,0xA0,0x26,0x56,0xFD,0xC8,0xE8,0x77,0x91,0x70,0x71,0xF4,
	  0x28,0x18,0x80,0x7C,0xB4,0x2E,0x9E,0x12,0xEF,0xE6,0xDE,0xE7,
	  0x96,0x99,0x81,0xAF,0x9C,0x0F,0x8B,0x27,0x50,0xCE,0x49,0xE4,
	  0xAA,0x50,0xEE,0x27,0x05,0x92,0x3C,0xC1,0xA0,0x6B,0xE6,0x9A,
	  0x3F,0xCD,0x87,0xF3,0xBA,0x55,0xD7,0x36,0x19,0x13,0xDF,0xBC,
	  0xB6,0x61,0xB6,0x28,0x6D,0x93,0x28,0xBB,0xA9,0x40,0xA0,0x1A,
	  0x1F,0x57,0x36,0xF2,0x81,0x50,0x94,0x89,0x60,0x3F,0x80,0x6F,
	  0xCE,0x13,0x1A,0x4D,0xB2,0xDC,0x82,0x7F,0xAC,0xA3,0xA7,0x5E,
	  0x42,0x70,0xC8,0x5B,0x5A,0x5C,0x65,0xAD,0x89,0xFC,0x16,0x9B,
	  0x25,0x3B,0x60,0xB1,0xEF,0x39,0xF4,0x57
	};

  MOZ_STATIC_ASSERT(PR_ARRAY_SIZE(P) == 1024 / PR_BITS_PER_BYTE, "bad DSA P");
  MOZ_STATIC_ASSERT(PR_ARRAY_SIZE(Q) ==  160 / PR_BITS_PER_BYTE, "bad DSA Q");
  MOZ_STATIC_ASSERT(PR_ARRAY_SIZE(G) == 1024 / PR_BITS_PER_BYTE, "bad DSA G");

  PQGParams pqgParams  = {
    NULL /*arena*/,
    { siBuffer, P, PR_ARRAY_SIZE(P) },
    { siBuffer, Q, PR_ARRAY_SIZE(Q) },
    { siBuffer, G, PR_ARRAY_SIZE(G) }
  };

  *privateKey = PK11_GenerateKeyPair(slot,
                                     CKM_DSA_KEY_PAIR_GEN, 
                                     &pqgParams, 
                                     publicKey, 
                                     PR_FALSE /*isPerm*/, 
                                     PR_TRUE /*isSensitive*/, 
                                     NULL /*&pwdata*/);

  NS_ENSURE_TRUE(*privateKey, NS_ERROR_FAILURE); // XXX: PRErrorCode -> nsresult
  NS_ENSURE_TRUE(*publicKey, NS_ERROR_UNEXPECTED);

  return NS_OK;
}

NS_IMETHODIMP 
KeyGenRunnable::Run()
{
  if (!NS_IsMainThread()) {
    nsNSSShutDownPreventionLock locker;
    if (isAlreadyShutDown()) {
      mRv = NS_ERROR_ABORT;
    } else {
      // We always want to use the internal slot for BrowserID; in particular,
      // we want to avoid smartcard slots.
      PK11SlotInfo *slot = PK11_GetInternalSlot();
      if (!slot) {
        mRv = NS_ERROR_UNEXPECTED;
      } else {
        SECKEYPrivateKey *privk = NULL;
        SECKEYPublicKey *pubk = NULL;

        switch (mKeyType) {
        case rsaKey:
          mRv = GenerateRSAKeyPair(slot, &privk, &pubk);
          break;
        case dsaKey:
          mRv = GenerateDSAKeyPair(slot, &privk, &pubk);
          break;
        default:
          MOZ_NOT_REACHED("unknown key type");
          mRv = NS_ERROR_UNEXPECTED;
        }

        PK11_FreeSlot(slot);

        if (NS_SUCCEEDED(mRv)) {
          MOZ_ASSERT(privk);
          MOZ_ASSERT(pubk);
          mKeyPair = new KeyPair(privk, pubk);
        }

        SECKEY_DestroyPrivateKey(privk); // XXX: use smart pointer
        SECKEY_DestroyPublicKey(pubk); // XXX: use smart pointer
      }
    }

    NS_DispatchToMainThread(this);
  } else {
    // Back on Main Thread
    (void) mCallback->GenerateKeyPairFinished(mRv, mKeyPair);
  }
  return NS_OK;
}

SignRunnable::SignRunnable(const nsACString & aText,
                           SECKEYPrivateKey * privateKey,
                           nsIIdentitySignCallback * aCallback)
  : mTextToSign(aText)
  , mPrivateKey(SECKEY_CopyPrivateKey(privateKey))
  , mCallback(aCallback)
  , mRv(NS_ERROR_NOT_INITIALIZED)
{
}

NS_IMETHODIMP 
SignRunnable::Run()
{
  if (!NS_IsMainThread()) {
    nsNSSShutDownPreventionLock locker;
    if (isAlreadyShutDown()) {
      mRv = NS_ERROR_ABORT;
    } else {
      PRUint8 buffer[2048];
      SECItem signature = { siBuffer, buffer, sizeof buffer };

      switch (mPrivateKey->keyType) {
      case rsaKey:
        // XXX: is this in the right format
        mRv = MapSECStatus(SEC_SignData(&signature,
                reinterpret_cast<const PRUint8 *>(mTextToSign.BeginReading()),
                mTextToSign.Length(), 
                mPrivateKey, 
                SEC_OID_PKCS1_SHA256_WITH_RSA_ENCRYPTION));
        break;
      case dsaKey:
      {
        // We need the output in PKCS#11 format (r with leading padding,
        // followed by s with leading padding), not DER encoding, so we must use
        // PK11_HashBuf & PK11_Sign instead of SEC_SignData.
        PRUint8 hash[160 / BITS_PER_BYTE];
        mRv = MapSECStatus(PK11_HashBuf(SEC_OID_SHA1, hash,
                     const_cast<PRUint8*>(reinterpret_cast<const PRUint8 *>(
                                              mTextToSign.BeginReading())),
                                        mTextToSign.Length()));
        SECItem hashItem = { siBuffer, hash, PR_ARRAY_SIZE(hash) };
        if (NS_SUCCEEDED(mRv)) {
          mRv = MapSECStatus(PK11_Sign(mPrivateKey, &signature, &hashItem));
        }
        break;
      }
      default:
        MOZ_NOT_REACHED("unknown key type");
        mRv = NS_ERROR_UNEXPECTED;
      }

      if (NS_SUCCEEDED(mRv)) {
        Base64urlEncode(&signature, mSignature);
      }
    }

    NS_DispatchToMainThread(this);
  } else {
    // Back on Main Thread
    (void) mCallback->SignFinished(mRv, mSignature);
  }

  return NS_OK;
}

} // unnamed namespace

// XPCOM module registration

namespace mozilla {

NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(IdentityCryptoService, Init)

} // namespace mozilla

namespace {


#define NS_IDENTITYCRYPTOSERVICE_CID \
  {0xbea13a3a, 0x44e8, 0x4d7f, {0xa0, 0xa2, 0x2c, 0x67, 0xf8, 0x4e, 0x3a, 0x97}}

NS_DEFINE_NAMED_CID(NS_IDENTITYCRYPTOSERVICE_CID);

const mozilla::Module::CIDEntry kCIDs[] = {
  { &kNS_IDENTITYCRYPTOSERVICE_CID, false, NULL, IdentityCryptoServiceConstructor },
  { NULL }
};

const mozilla::Module::ContractIDEntry kContracts[] = {
  { NS_IDENTITYCRYPTOSERVICE_CONTRACTID, &kNS_IDENTITYCRYPTOSERVICE_CID },
  { NULL }
};

const mozilla::Module kModule = {
  mozilla::Module::kVersion,
  kCIDs,
  kContracts
};

} // unnamed namespace

NSMODULE_DEFN(identity) = &kModule;
