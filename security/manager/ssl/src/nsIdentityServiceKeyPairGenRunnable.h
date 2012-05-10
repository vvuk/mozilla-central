/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIdentityServiceKeyPairGenRunnable_h_
#define nsIdentityServiceKeyPairGenRunnable_h_

#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsThreadUtils.h"

#include "nsIdentityServiceKeyPair.h"

#include "nsNSSShutDown.h"

class nsIdentityServiceKeyPairGenRunnable : public nsRunnable, public nsNSSShutDownObject
{
public:
  nsIdentityServiceKeyPairGenRunnable(PRUint32 aAlgorithm, 
                                      nsIIdentityServiceKeyGenCallback * aCallback, 
                                      nsIdentityServiceKeyPair * aKeyPair);  
private:
  ~nsIdentityServiceKeyPairGenRunnable();
  NS_IMETHODIMP Run();
  PRUint32 mAlgorithm;
  nsCOMPtr<nsIIdentityServiceKeyGenCallback> mCallback;
  nsCOMPtr<nsIdentityServiceKeyPair> mKeyPair;
  
  virtual void virtualDestroyNSSReference();
  void destructorSafeDestroyNSSReference();
};

#endif // nsIdentityServiceKeyPairGenRunnable_h_
