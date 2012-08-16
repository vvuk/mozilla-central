/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla code.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2012
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Eric Rescorla <ekr@rtfm.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef dtls_identity_h__
#define dtls_identity_h__

#include <string>

#include "nspr.h"
#include "hasht.h"
#include "keyhi.h"
#include "nss.h"

#include "mozilla/RefPtr.h"
#include "nsISupportsImpl.h"

class DtlsIdentity : public mozilla::RefCounted<DtlsIdentity> {
 public:
  ~DtlsIdentity();

  static mozilla::RefPtr<DtlsIdentity> Generate(const std::string name);

  CERTCertificate *cert() { return cert_; }
  SECKEYPrivateKey *privkey() { return privkey_; }

  nsresult ComputeFingerprint(const std::string algorithm,
                              unsigned char *digest,
                              std::size_t size,
                              std::size_t *digest_length);

  static nsresult ComputeFingerprint(const CERTCertificate *cert,
                                     const std::string algorithm,
                                     unsigned char *digest,
                                     std::size_t size,
                                     std::size_t *digest_length);

 private:
  DtlsIdentity(SECKEYPrivateKey *privkey, CERTCertificate *cert)
      : privkey_(privkey), cert_(cert) {}

  SECKEYPrivateKey *privkey_;
  CERTCertificate *cert_;
};



#endif
