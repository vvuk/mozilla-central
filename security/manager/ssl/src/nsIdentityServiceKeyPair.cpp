/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/////////////////////////////////////////////////////////////////////////////////
// nsIIdentityServiceKeyPair Implementation 
/////////////////////////////////////////////////////////////////////////////////

#include "nsCOMPtr.h"
#include "nsStringGlue.h"
#include "nsIClassInfoImpl.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"

#include "plbase64.h"

#include "nsIdentityServiceKeyPair.h"
#include "nsIdentityServiceKeyPairGenRunnable.h"

#include "nss.h"
#include "pk11func.h"
#include "secmod.h"
#include "secerr.h"
#include "keyhi.h"
#include "cryptohi.h"

#include "nsNSSCleaner.h"
NSSCleanupAutoPtrClass(SECKEYPrivateKey, SECKEY_DestroyPrivateKey)

using namespace mozilla;

// Stolen from /toolkit/components/places/Helpers.cpp
// TODO: use the new base64 encoder khuey wrote
static
nsresult
Base64urlEncode(const PRUint8* aBytes,
                PRUint32 aNumBytes,
                nsCString& _result)
{
  // SetLength does not set aside space for NULL termination.  PL_Base64Encode
  // will not NULL terminate, however, nsCStrings must be NULL terminated.  As a
  // result, we set the capacity to be one greater than what we need, and the
  // length to our desired length.
  PRUint32 length = (aNumBytes + 2) / 3 * 4; // +2 due to integer math.
  _result.SetCapacity(length + 1);
  _result.SetLength(length);
  (void)PL_Base64Encode(reinterpret_cast<const char*>(aBytes), aNumBytes,
                        _result.BeginWriting());

  // base64url encoding is defined in RFC 4648.  It replaces the last two
  // alphabet characters of base64 encoding with '-' and '_' respectively.
  _result.ReplaceChar('+', '-');
  _result.ReplaceChar('/', '_');
  return NS_OK;
}

NS_IMPL_THREADSAFE_ISUPPORTS1(nsIdentityServiceKeyPair, nsIIdentityServiceKeyPair)
nsIdentityServiceKeyPair::nsIdentityServiceKeyPair()
{
  mPrivateKey = NULL;
  mPublicKey = NULL;
  mKeyType = dsaKey; // DSA as default type
}

nsresult
nsIdentityServiceKeyPair::SetKeyPair(SECKEYPrivateKey * aPrivateKey, 
                                     SECKEYPublicKey * aPublicKey)
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return NS_OK;

  mPrivateKey = aPrivateKey;
  mPublicKey = aPublicKey;
  mKeyType = mPublicKey->keyType;

  return NS_OK;
}

NS_IMETHODIMP
nsIdentityServiceKeyPair::GetEncodedPublicKey(nsACString &aOutString)
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return NS_OK;

  // Convert the pubkey to DER via PK11_DEREncodePublicKey
  SECItem* derPubKey;
  derPubKey = PK11_DEREncodePublicKey(mPublicKey);
  // Base64Url encode this DER-encoded key
  // What length do we need here? 
  PRUint32 len;
  PRUint8* data;

  len = static_cast<PRUint32>(derPubKey->len);
  data = static_cast<PRUint8*>(derPubKey->data);

  nsCString tmpStr;

  nsresult rv = Base64urlEncode(data, len, tmpStr);

  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  aOutString = tmpStr;
  
  return NS_OK;
}

// RSA

NS_IMETHODIMP 
nsIdentityServiceKeyPair::GetEncodedRSAPublicKeyExponent(nsACString &aOutString)
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return NS_OK;

  bool isRSAKey = mPublicKey->keyType == rsaKey;

  NS_ENSURE_TRUE(isRSAKey, NS_ERROR_UNEXPECTED);
  NS_ENSURE_TRUE(mPublicKey->u.rsa.publicExponent.len, NS_ERROR_UNEXPECTED);

  PRUint32 len;
  PRUint8* data;
  len = static_cast<PRUint32>(mPublicKey->u.rsa.publicExponent.len);
  data = static_cast<PRUint8*>(mPublicKey->u.rsa.publicExponent.data);

  nsCString tmpStr;  
  nsresult rv = Base64urlEncode(data, len, tmpStr);
  
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  aOutString = tmpStr;

  return NS_OK;
}

NS_IMETHODIMP 
nsIdentityServiceKeyPair::GetEncodedRSAPublicKeyModulus(nsACString &aOutString)
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return NS_OK; // XXX: not sure what to return with this condition

  bool isRSAKey = mPublicKey->keyType == rsaKey;

  NS_ENSURE_TRUE(isRSAKey, NS_ERROR_UNEXPECTED);

  NS_ENSURE_TRUE(mPublicKey->u.rsa.modulus.len, NS_ERROR_UNEXPECTED);

  PRUint32 len;
  PRUint8* data;
  len = static_cast<PRUint32>(mPublicKey->u.rsa.modulus.len);
  data = static_cast<PRUint8*>(mPublicKey->u.rsa.modulus.data);

  nsCString tmpStr;  
  nsresult rv = Base64urlEncode(data, len, tmpStr);

  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  aOutString = tmpStr;

  return NS_OK;
}

// DSA

NS_IMETHODIMP 
nsIdentityServiceKeyPair::GetEncodedDSAGenerator(nsACString &aOutString)
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return NS_OK;

  bool isDSAKey = mPublicKey->keyType == dsaKey;
  NS_ENSURE_TRUE(isDSAKey, NS_ERROR_UNEXPECTED);

  NS_ENSURE_TRUE(mPublicKey->u.dsa.params.base.len, NS_ERROR_UNEXPECTED);
  // get DSA public key 'base'
  PRUint32 len;
  PRUint8* data;
  len = static_cast<PRUint32>(mPublicKey->u.dsa.params.base.len);
  data = static_cast<PRUint8*>(mPublicKey->u.dsa.params.base.data);

  nsCString tmpStr;  
  nsresult rv = Base64urlEncode(data, len, tmpStr);

  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  aOutString = tmpStr;

  return NS_OK;
}

NS_IMETHODIMP 
nsIdentityServiceKeyPair::GetEncodedDSAPrime(nsACString &aOutString)
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return NS_OK;

  bool isDSAKey = mPublicKey->keyType == dsaKey;
  NS_ENSURE_TRUE(isDSAKey, NS_ERROR_UNEXPECTED);

  NS_ENSURE_TRUE(mPublicKey->u.dsa.params.prime.len, NS_ERROR_UNEXPECTED);

  // get DSA public key 'prime'
  PRUint32 len;
  PRUint8* data;
  len = static_cast<PRUint32>(mPublicKey->u.dsa.params.prime.len);
  data = static_cast<PRUint8*>(mPublicKey->u.dsa.params.prime.data);

  nsCString tmpStr;  
  nsresult rv = Base64urlEncode(data, len, tmpStr);

  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  aOutString = tmpStr;

  return NS_OK;
}

NS_IMETHODIMP 
nsIdentityServiceKeyPair::GetEncodedDSASubPrime(nsACString &aOutString)
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return NS_OK;

  bool isDSAKey = mPublicKey->keyType == dsaKey;
  NS_ENSURE_TRUE(isDSAKey, NS_ERROR_UNEXPECTED);

  NS_ENSURE_TRUE(mPublicKey->u.dsa.params.subPrime.len, NS_ERROR_UNEXPECTED);

  // get DSA public key 'subprime'
  PRUint32 len;
  PRUint8* data;
  len = static_cast<PRUint32>(mPublicKey->u.dsa.params.subPrime.len);
  data = static_cast<PRUint8*>(mPublicKey->u.dsa.params.subPrime.data);

  nsCString tmpStr;  
  nsresult rv = Base64urlEncode(data, len, tmpStr);

  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  aOutString = tmpStr;

  return NS_OK;
}

// keyType
NS_IMETHODIMP 
nsIdentityServiceKeyPair::GetKeyType(nsACString &aOutString)
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return NS_OK;

  if (mPublicKey->keyType == dsaKey) {
    aOutString = "2";
  } else if (mPublicKey->keyType == rsaKey) {
    aOutString = "1";
  } else if (mPublicKey->keyType != dsaKey && 
             mPublicKey->keyType != rsaKey) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

// Methods
NS_IMETHODIMP 
nsIdentityServiceKeyPair::Sign(const nsACString & aText, 
                               nsACString & _retval NS_OUTPARAM)
{
  SECItem signature = { siBuffer, NULL, 0 };
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return NS_OK;
  
  SECStatus status;

  if (mKeyType == rsaKey) {
    status = 
      SEC_SignData(&signature,
                   reinterpret_cast<const PRUint8 *>(aText.BeginReading()),
                   aText.Length(), 
                   mPrivateKey, 
                   SEC_OID_PKCS1_SHA256_WITH_RSA_ENCRYPTION);

  } else if (mKeyType == dsaKey) {
    status = 
      SEC_SignData(&signature,
                   reinterpret_cast<const PRUint8 *>(aText.BeginReading()),
                   aText.Length(), 
                   mPrivateKey, 
                   SEC_OID_ANSIX9_DSA_SIGNATURE_WITH_SHA1_DIGEST);
  }

  nsresult rv = NS_ERROR_UNEXPECTED;

  if (status == SECSuccess) {
    PRUint32 len;
    PRUint8* data;
    len = static_cast<PRUint32>(signature.len);
    data = static_cast<PRUint8*>(signature.data);
    nsCString tmpStr;  

    rv = Base64urlEncode(data, len, tmpStr); // XXX: use revtval directly
    if (NS_SUCCEEDED(rv)) {
      _retval = tmpStr;
    }
  } else {
    rv = NS_ERROR_UNEXPECTED;
  }

  SECITEM_FreeItem(&signature, PR_FALSE);

  return rv;
}

NS_IMETHODIMP 
nsIdentityServiceKeyPair::Verify(const nsACString & aSignature,
                                 const nsACString & aEncodedPublicKey,
                                 nsACString & _retval NS_OUTPARAM)
{
  // Must determine the key type first.
  // 1. The key is base64UrlEncoded
  // 2. The key is DER encoded
  
  return NS_OK;

}

nsIdentityServiceKeyPair::~nsIdentityServiceKeyPair()
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return;

  destructorSafeDestroyNSSReference();
  shutdown(calledFromObject);
}

void nsIdentityServiceKeyPair::virtualDestroyNSSReference()
{
  if (isAlreadyShutDown())
    return;

  destructorSafeDestroyNSSReference();
}

void nsIdentityServiceKeyPair::destructorSafeDestroyNSSReference()
{
  if (isAlreadyShutDown())
    return;

  SECKEY_DestroyPrivateKey(mPrivateKey);
  SECKEY_DestroyPublicKey(mPublicKey);
}

NS_IMETHODIMP
nsIdentityServiceKeyPair::GenerateKeyPair(PRUint32 aAlgorithm, 
                                          nsIIdentityServiceKeyGenCallback* aCallback)
{
  nsNSSShutDownPreventionLock locker;

  nsCOMPtr<nsIRunnable> r = 
    new nsIdentityServiceKeyPairGenRunnable(aAlgorithm, aCallback, this);

  nsCOMPtr<nsIThread> thread;

  nsresult rv = NS_NewThread(getter_AddRefs(thread), r);

  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}
