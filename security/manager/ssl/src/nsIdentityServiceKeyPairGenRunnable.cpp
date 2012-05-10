/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/////////////////////////////////////////////////////////////////////////////////
// nsIIdentityServiceKeyPairGenRunnable Implementation 
/////////////////////////////////////////////////////////////////////////////////

#include "nsCOMPtr.h"
#include "nsStringGlue.h"
#include "nsIClassInfoImpl.h"

#include "nsIdentityServiceKeyPairGenRunnable.h"

#include "nss.h"
#include "pk11func.h"
#include "secmod.h"
#include "secerr.h"
#include "keyhi.h"
#include "cryptohi.h"

nsIdentityServiceKeyPairGenRunnable::nsIdentityServiceKeyPairGenRunnable(PRUint32 aAlgorithm, nsIIdentityServiceKeyGenCallback * aCallback, nsIdentityServiceKeyPair * aKeyPair)
{
  mAlgorithm = aAlgorithm;
  mCallback = do_QueryInterface(aCallback);
  mKeyPair = aKeyPair;
}

NS_IMETHODIMP 
nsIdentityServiceKeyPairGenRunnable::Run()
{
  nsNSSShutDownPreventionLock locker;
  if (!NS_IsMainThread()) {
    // Assemble the requisite args for generating a keypair
    SECKEYPublicKey *pubk;
    SECKEYPrivateKey *privk;
    PK11SlotInfo *slot = PK11_GetInternalSlot();
    nsresult rv;

    switch (mAlgorithm) {
    case rsaKey:
      {
        int keySizeInBits = 2048;
        PK11RSAGenParams rsaParams;

        rsaParams.keySizeInBits = keySizeInBits;
        rsaParams.pe = 0x10001;

        privk = PK11_GenerateKeyPair(slot, 
                                     CKM_RSA_PKCS_KEY_PAIR_GEN, 
                                     &rsaParams, 
                                     &pubk, 
                                     PR_FALSE /*isPerm*/, 
                                     PR_TRUE /*isSensitive*/, 
                                     NULL /*&pwdata*/);
      }
      break;
    case dsaKey:
      {
        PK11SlotInfo *dsa_slot = PK11_GetBestSlot(CKM_DSA_KEY_PAIR_GEN, NULL);

        /* FIPS preprocessor directives for DSA. */
        #define FIPS_DSA_TYPE                           siBuffer
        #define FIPS_DSA_SUBPRIME_LENGTH                20  /* 160-bits */
        #define FIPS_DSA_PRIME_LENGTH                   64  /* 512-bits */
        #define FIPS_DSA_BASE_LENGTH                    64  /* 512-bits */

        /* DSA Known P (1024-bits), Q (160-bits), and G (1024-bits) Values. */
        static const PRUint8 * dsa_P = (const PRUint8 *)
          "\x8d\xf2\xa4\x94\x49\x22\x76\xaa"
          "\x3d\x25\x75\x9b\xb0\x68\x69\xcb"
          "\xea\xc0\xd8\x3a\xfb\x8d\x0c\xf7"
          "\xcb\xb8\x32\x4f\x0d\x78\x82\xe5"
          "\xd0\x76\x2f\xc5\xb7\x21\x0e\xaf"
          "\xc2\xe9\xad\xac\x32\xab\x7a\xac"
          "\x49\x69\x3d\xfb\xf8\x37\x24\xc2"
          "\xec\x07\x36\xee\x31\xc8\x02\x91";
        
        static const PRUint8 * dsa_Q = (const PRUint8 *)
          "\xc7\x73\x21\x8c\x73\x7e\xc8\xee"
          "\x99\x3b\x4f\x2d\xed\x30\xf4\x8e"
          "\xda\xce\x91\x5f";        

        static const PRUint8 * dsa_G  = (const PRUint8 *)
          "\x62\x6d\x02\x78\x39\xea\x0a\x13"
          "\x41\x31\x63\xa5\x5b\x4c\xb5\x00"
          "\x29\x9d\x55\x22\x95\x6c\xef\xcb"
          "\x3b\xff\x10\xf3\x99\xce\x2c\x2e"
          "\x71\xcb\x9d\xe5\xfa\x24\xba\xbf"
          "\x58\xe5\xb7\x95\x21\x92\x5c\x9c"
          "\xc4\x2e\x9f\x6f\x46\x4b\x08\x8c"
          "\xc5\x72\xaf\x53\xe6\xd7\x88\x02";

        PQGParams pqgParams  = { NULL,
                                 { FIPS_DSA_TYPE, 
                                   (unsigned char *)dsa_P, 
                                   FIPS_DSA_PRIME_LENGTH },
                                 { FIPS_DSA_TYPE, 
                                   (unsigned char *)dsa_Q, 
                                   FIPS_DSA_SUBPRIME_LENGTH },
                                 { FIPS_DSA_TYPE, 
                                   (unsigned char *)dsa_G, 
                                   FIPS_DSA_BASE_LENGTH }};

        privk = PK11_GenerateKeyPair(dsa_slot,
                                     CKM_DSA_KEY_PAIR_GEN, 
                                     &pqgParams, 
                                     &pubk, 
                                     PR_FALSE /*isPerm*/, 
                                     PR_TRUE /*isSensitive*/, 
                                     NULL /*&pwdata*/);
      }
      break;
    default:
      return NS_ERROR_INVALID_ARG;
    }

    if (privk != NULL && pubk != NULL) {    

      mKeyPair->SetKeyPair(privk, pubk);

      NS_DispatchToMainThread(this);
      
    } else {
      int error;
      error = PORT_GetError();
      if (IS_SEC_ERROR(error)) {
        NS_ERROR("Keypair generation failed");
      } else {
        NS_ERROR("Unknown error while generating keypair");
      }
      return SECFailure;
    }

  } else {
    // Back on Main Thread
    nsresult rv = mCallback->KeyPairGenFinished(mKeyPair);
  }
  return NS_OK;
}

nsIdentityServiceKeyPairGenRunnable::~nsIdentityServiceKeyPairGenRunnable()
{
  nsNSSShutDownPreventionLock locker;

  if (isAlreadyShutDown())
    return;

  destructorSafeDestroyNSSReference();
  shutdown(calledFromObject);
}


void nsIdentityServiceKeyPairGenRunnable::virtualDestroyNSSReference()
{
  if (isAlreadyShutDown())
    return;

  destructorSafeDestroyNSSReference();
}

void nsIdentityServiceKeyPairGenRunnable::destructorSafeDestroyNSSReference()
{
  if (isAlreadyShutDown())
    return;
}
