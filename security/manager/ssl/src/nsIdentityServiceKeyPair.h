/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIdentityServiceKeyPair_h_
#define nsIdentityServiceKeyPair_h_

#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsIIdentityServiceKeyPair.h"
#include "nsNSSShutDown.h"

#include "pk11func.h"
#include "secmod.h"
#include "secerr.h"
#include "blapit.h"
#include "cryptohi.h"

#define NS_IDENTITYSERVICEKEYPAIR_CONTRACTID "@mozilla.org/identityservice-keypair;1"
#define NS_IDENTITYSERVICEKEYPAIR_COMPONENT_CLASSNAME "Mozilla Identitiy Service Keypair"
#define NS_IDENTITYSERVICEKEYPAIR_CID {0x01673262, 0x2af1, 0x4daf, {0x92, 0xb6, 0x41, 0x6d, 0x5e, 0x47, 0xd0, 0x81}}

class nsIdentityServiceKeyPair : public nsIIdentityServiceKeyPair, 
                                 public nsNSSShutDownObject
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIIDENTITYSERVICEKEYPAIR
  
  nsIdentityServiceKeyPair();
  nsresult SetKeyPair(SECKEYPrivateKey* aPrivateKey, SECKEYPublicKey*  aPublicKey);

private:
  ~nsIdentityServiceKeyPair();
  SECKEYPrivateKey* mPrivateKey;
  SECKEYPublicKey*  mPublicKey;
  KeyType mKeyType;

  virtual void virtualDestroyNSSReference();
  void destructorSafeDestroyNSSReference();
};

#endif // nsIdentityServiceKeyPair_h_
