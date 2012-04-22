/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "transportflow.h"
#include "transportlayer.h"
#include "transportlayerlog.h"

int main(int argc, char **argv)
{
  TransportFlow *flow = new TransportFlow();
  TransportLayerLogging *l1 = new TransportLayerLogging();

  flow->PushLayer(l1);

  return 0;
}
