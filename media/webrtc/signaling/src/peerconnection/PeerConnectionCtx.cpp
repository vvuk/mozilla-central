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
 * Contributor(s):
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


#include "CallControlManager.h"
#include "CC_Device.h"
#include "CC_Call.h"
#include "CC_Observer.h"
#include "ccapi_call_info.h"
#include "CC_SIPCCCallInfo.h"
#include "ccapi_device_info.h"
#include "CC_SIPCCDeviceInfo.h"
#include "CSFLog.h"
#include "vcm.h"
#include "PeerConnection.h"
#include "PeerConnectionImpl.h"
#include "PeerConnectionCtx.h"
#include "cpr_socket.h"

static const char* logTag = "PeerConnectionCtx";

namespace sipcc {

PeerConnectionCtx* PeerConnectionCtx::instance;

PeerConnectionCtx* PeerConnectionCtx::GetInstance() {
  if (instance)
    return instance;
  
  CSFLogDebug(logTag, "Creating PeerConnectionCtx");
  PeerConnectionCtx *ctx = new PeerConnectionCtx();

  nsresult res = ctx->Initialize();
  PR_ASSERT(NS_SUCCEEDED(res));
  if (!NS_SUCCEEDED(res))
    return NULL;
  
  instance = ctx;

  return instance;
}

void PeerConnectionCtx::Destroy() {
  instance->Cleanup();
  delete instance;
  instance = NULL;
}

// Signatures for address
// TODO(ekr@rtfm.com): remove
std::string GetLocalActiveInterfaceAddressSDP();
std::string NetAddressToStringSDP(const struct sockaddr* net_address,
                               socklen_t address_len);

nsresult PeerConnectionCtx::Initialize() {
  // TODO(ekr@rtfm.com): Remove this code
  mAddr = GetLocalActiveInterfaceAddressSDP();
  mCCM = CSF::CallControlManager::create();
  if (!mCCM.get())
    return NS_ERROR_FAILURE;

  mCCM->setLocalIpAddressAndGateway(mAddr,"");  // TODO(ekr@rtfm.com): Remove

  // Add the local audio codecs
  // FIX - Get this list from MediaEngine instead
  // Turning them all on for now
  int codecMask = 0;
  codecMask |= VCM_CODEC_RESOURCE_G711;
  codecMask |= VCM_CODEC_RESOURCE_LINEAR;
  codecMask |= VCM_CODEC_RESOURCE_G722;
  codecMask |= VCM_CODEC_RESOURCE_iLBC;
  codecMask |= VCM_CODEC_RESOURCE_iSAC;
  mCCM->setAudioCodecs(codecMask);

  //Add the local video codecs
  // FIX - Get this list from MediaEngine instead
  // Turning them all on for now
  codecMask = 0;
  codecMask |= VCM_CODEC_RESOURCE_H263;
  codecMask |= VCM_CODEC_RESOURCE_H264;
  codecMask |= VCM_CODEC_RESOURCE_VP8;
  codecMask |= VCM_CODEC_RESOURCE_I420;
  mCCM->setVideoCodecs(codecMask);

  if (!mCCM->startSDPMode())
    return NS_ERROR_FAILURE;

  mCCM->addCCObserver(this);
  mDevice = mCCM->getActiveDevice();	
  if (!mDevice.get())
    return NS_ERROR_FAILURE;

  ChangeSipccState(PeerConnectionInterface::kStarting);

  return NS_OK;
}

nsresult PeerConnectionCtx::Cleanup() {
  mCCM->destroy();
  mCCM->removeCCObserver(this);
  return NS_OK;
}

CSF::CC_CallPtr PeerConnectionCtx::createCall() {
  return mDevice->createCall();
}

void PeerConnectionCtx::onDeviceEvent(ccapi_device_event_e deviceEvent, CSF::CC_DevicePtr device, CSF::CC_DeviceInfoPtr info ) {
  CSFLogDebug(logTag, "onDeviceEvent()");
  cc_service_state_t state = info->getServiceState();
	
  if (CC_STATE_INS == state) {	        
    // SIPCC is up	
    if (PeerConnectionInterface::kStarting == mSipccState ||
        PeerConnectionInterface::kIdle == mSipccState) {
      ChangeSipccState(PeerConnectionInterface::kStarted);
    }
  }
}  

// Demux the call event to the right PeerConnection
void PeerConnectionCtx::onCallEvent(ccapi_call_event_e callEvent,
                                    CSF::CC_CallPtr call,
                                    CSF::CC_CallInfoPtr info) {
  CSFLogDebug(logTag, "onCallEvent()");
  PeerConnectionImpl* pc = PeerConnectionImpl::AcquireInstance(
      call->getPeerConnection());

  if (!pc)  // This must be an event on a dead PC. Ignore
    return;

  CSFLogDebug(logTag, "Calling PC");
  pc->onCallEvent(callEvent, call, info);

  pc->ReleaseInstance();
}




#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>

// POSIX Only Implementation
std::string GetLocalActiveInterfaceAddressSDP() 
{
	std::string local_ip_address = "0.0.0.0";
#ifndef WIN32
	int sock_desc_ = INVALID_SOCKET;
	sock_desc_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in proxy_server_client;
 	proxy_server_client.sin_family = AF_INET;
	proxy_server_client.sin_addr.s_addr	= inet_addr("10.0.0.1");
	proxy_server_client.sin_port = 12345;
	fcntl(sock_desc_,F_SETFL,  O_NONBLOCK);
	int ret = connect(sock_desc_, reinterpret_cast<sockaddr*>(&proxy_server_client),
                    sizeof(proxy_server_client));

	if(ret == SOCKET_ERROR)
	{
	}
 
	struct sockaddr_storage source_address;
	socklen_t addrlen = sizeof(source_address);
	ret = getsockname(
			sock_desc_, reinterpret_cast<struct sockaddr*>(&source_address),&addrlen);

	
	//get the  ip address 
	local_ip_address = NetAddressToStringSDP(
						reinterpret_cast<const struct sockaddr*>(&source_address),
						sizeof(source_address));
	close(sock_desc_);
#else
	hostent* localHost;
	localHost = gethostbyname("");
	local_ip_v4_address_ = inet_ntoa (*(struct in_addr *)*localHost->h_addr_list);
#endif
	return local_ip_address;
}

//Only POSIX Complaint as of 7/6/11
#ifndef WIN32
std::string NetAddressToStringSDP(const struct sockaddr* net_address,
                               socklen_t address_len) {

  // This buffer is large enough to fit the biggest IPv6 string.
  char buffer[128];
  int result = getnameinfo(net_address, address_len, buffer, sizeof(buffer),
                           NULL, 0, NI_NUMERICHOST);
  if (result != 0) {
    buffer[0] = '\0';
  }
  return std::string(buffer);
}
#endif

}  // namespace sipcc
