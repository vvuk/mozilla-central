/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "nspr.h"
#include "prio.h"

#include "nsCOMPtr.h"

#include "nsASocketHandler.h"
#include "nsISocketTransportService.h"
#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "nsXPCOM.h"

#include "logging.h"
#include "transportflow.h"
#include "transportlayerprsock.h"

MLOG_INIT("mtransport");

std::string TransportLayerPrsock::ID("mt_prsock");


NS_IMPL_ISUPPORTS0(TransportLayerPrsock);

nsresult TransportLayerPrsock::Import(PRFileDesc *fd) {
  fd_ = fd;

  // Get the transport service as a transport service
  nsresult rv;
  stservice_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  if (!NS_SUCCEEDED(rv))
    return rv;

  rv = stservice_->AttachSocket(fd_, this);
  if (!NS_SUCCEEDED(rv))
    return rv;
  
  return NS_OK;
}

int TransportLayerPrsock::SendPacket(const unsigned char *data, size_t len) {
  MLOG(PR_LOG_DEBUG, LAYER_INFO << "SendPacket(" << len << ")");

  PRInt32 status;
  status = PR_Write(fd_, data, len);
  
  if (status <= 0)
    return -1;

  return status;
}
