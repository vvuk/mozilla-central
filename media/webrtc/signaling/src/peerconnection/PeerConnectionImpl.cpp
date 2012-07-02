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

#include <string>

#include "CSFLog.h"
#include "ccapi_call_info.h"
#include "CC_SIPCCCallInfo.h"
#include "ccapi_device_info.h"
#include "CC_SIPCCDeviceInfo.h"
#include "vcm.h"
#include "PeerConnectionImpl.h"
#include "MediaSegment.h"
#include "cpr_socket.h"
#include "runnable_utils.h"

static const char* logTag = "PeerConnectionImpl";

namespace sipcc {

// LocalSourceStreamInfo
LocalSourceStreamInfo::LocalSourceStreamInfo(nsRefPtr<mozilla::MediaStream>& aMediaStream) :
  mMediaStream(aMediaStream)  
{  
}
  
LocalSourceStreamInfo:: ~LocalSourceStreamInfo()
{
  mMediaStream->Release();  
}

// We get this callback in order to find out which tracks are audio and which are video
// We should get this callback right away for existing streams after we add this class 
// as a listener.
void LocalSourceStreamInfo::NotifyQueuedTrackChanges(
  mozilla::MediaStreamGraph* aGraph, 
  mozilla::TrackID aID,
  mozilla::TrackRate aTrackRate,
  mozilla::TrackTicks aTrackOffset,
  PRUint32 aTrackEvents,
  const mozilla::MediaSegment& aQueuedMedia) 
{
  // Add the track ID to the list for audio/video so they can be counted when
  // createOffer/createAnswer is called.  
  // This tells us whether we have the camera, mic, or both for example.
  mozilla::MediaSegment::Type trackType = aQueuedMedia.GetType();
  
  if (trackType == mozilla::MediaSegment::AUDIO)
  {
    // Should have very few tracks so not a hashtable
    bool found = false;
    for (unsigned u = 0; u < mAudioTracks.Length(); u++)
    {
      if (aID == mAudioTracks.ElementAt(u))
      {
        found = true;
        break;
      }
    }

    if (!found)
    {
      mAudioTracks.AppendElement(aID);
    }
  }
  else if (trackType == mozilla::MediaSegment::VIDEO)
  {
    // Should have very few tracks so not a hashtable
    bool found = false;
    for (unsigned u = 0; u < mVideoTracks.Length(); u++)
    {
      if (aID == mVideoTracks.ElementAt(u))
      {
        found = true;
        break;
      }
    }

    if (!found)
    {
      mVideoTracks.AppendElement(aID);
    }
  }
  else
  {
    CSFLogError(logTag, "NotifyQueuedTrackChanges - unknown media type");
  }
}

nsRefPtr<mozilla::MediaStream> LocalSourceStreamInfo::GetMediaStream()
{
  return mMediaStream;
}
  
unsigned LocalSourceStreamInfo::AudioTrackCount()
{
  return mAudioTracks.Length();  
}
  
unsigned LocalSourceStreamInfo::VideoTrackCount()
{
  return mVideoTracks.Length();
}
  


// Signatures for address
std::string GetLocalActiveInterfaceAddressSDP();
std::string NetAddressToStringSDP(const struct sockaddr* net_address,
                               socklen_t address_len);
            
PeerConnectionInterface* PeerConnectionInterface::CreatePeerConnection() 
{
  PeerConnectionImpl *pc = new PeerConnectionImpl();
  return pc;
}
  
std::map<const std::string, PeerConnectionImpl *> 
   PeerConnectionImpl::peerconnections;

PeerConnectionImpl::PeerConnectionImpl() : 
  mAddr(""), 
  mCCM(NULL), 
  mDevice(NULL), 
  mCall(NULL), 
  mPCObserver(NULL), 
  mReadyState(kNew), 
  mSipccState(kIdle),
  mLocalSourceStreamsLock(PR_NewLock()) 
{
}

                         
PeerConnectionImpl::~PeerConnectionImpl() 
{
  peerconnections.erase(mHandle);
  Shutdown();  
  PR_DestroyLock(mLocalSourceStreamsLock);
}

StatusCode PeerConnectionImpl::Initialize(PeerConnectionObserver* observer) {
  if (kIdle == mSipccState) {
    if (!observer)
    	return PC_NO_OBSERVER;	
    
    mPCObserver = observer;
    ChangeSipccState(kStarting);
    mAddr = GetLocalActiveInterfaceAddressSDP();
    mCCM = CSF::CallControlManager::create();
    mCCM->setLocalIpAddressAndGateway(mAddr,"");

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

    mCCM->startSDPMode();
    mCCM->addCCObserver(this);
    mDevice = mCCM->getActiveDevice();	
    mCall = mDevice->createCall();
    
    // Generate a handle from our pointer.
    unsigned char handle_bin[8];
    PeerConnectionImpl *handle = this;
    PR_ASSERT(sizeof(handle_bin) <= sizeof(handle));

    memcpy(handle_bin, &handle, sizeof(handle));
    for (size_t i = 0; i<sizeof(handle_bin); i++) {
      char hex[3];
      
      snprintf(hex, 3, "%.2x", handle_bin[i]);
      mHandle += hex;
    }

    // TODO(ekr@rtfm.com): need some way to set not offerer later
    // Looks like a bug in the NrIceCtx API.
    mIceCtx = NrIceCtx::Create("PC", true);

    // Create two streams to start with, assume one for audio and
    // one for video
    mIceStreams.push_back(mIceCtx->CreateStream("stream1", 2));
    mIceStreams.push_back(mIceCtx->CreateStream("stream1", 2));
    
    // Start gathering
    nsresult res;
    mIceCtx->thread()->Dispatch(WrapRunnableRet(mIceCtx, 
        &NrIceCtx::StartGathering, &res), NS_DISPATCH_SYNC);
    PR_ASSERT(NS_SUCCEEDED(res));
    
     // Store under mHandle
    mCall->setPeerConnection(mHandle);
    peerconnections[mHandle] = this;
  }
   
   return PC_OK;
}

/*
 * CC_SDP_DIRECTION_SENDRECV will not be used when Constraints are implemented
 */
StatusCode PeerConnectionImpl::CreateOffer(const std::string& hints) {
  unsigned localSourceAudioTracks = 0;
  unsigned localSourceVideoTracks = 0;

  // Add up all the audio/video inputs, e.g. microphone/camera
  PR_Lock(mLocalSourceStreamsLock);
  for (unsigned u = 0; u < mLocalSourceStreams.Length(); u++)
  {
    nsRefPtr<LocalSourceStreamInfo> localSourceStream = mLocalSourceStreams[u];
    localSourceAudioTracks += localSourceStream->AudioTrackCount();
    localSourceVideoTracks += localSourceStream->VideoTrackCount();
  }
  PR_Unlock(mLocalSourceStreamsLock);

  // Tell the SDP creator about the streams we received from GetUserMedia
  // FIX - we are losing information here.  When we need SIPCC to handle two
  // cameras with different caps this will need to be changed
  mCall->setLocalSourceAudioVideo(localSourceAudioTracks, localSourceVideoTracks);

  mCall->createOffer(CC_SDP_DIRECTION_SENDRECV, hints);

  return PC_OK;
}

StatusCode PeerConnectionImpl::CreateAnswer(const std::string& hints, const  std::string& offer) {
  mCall->createAnswer(CC_SDP_DIRECTION_SENDRECV, hints, offer);
  return PC_OK;
}

StatusCode PeerConnectionImpl::SetLocalDescription(Action action, const  std::string& sdp) {
  mLocalRequestedSDP = sdp;
  mCall->setLocalDescription(CC_SDP_DIRECTION_SENDRECV, (cc_jsep_action_t)action, sdp);		
  return PC_OK;
}

StatusCode PeerConnectionImpl::SetRemoteDescription(Action action, const std::string& sdp) {
  mRemoteRequestedSDP = sdp;
  mCall->setRemoteDescription(CC_SDP_DIRECTION_SENDRECV, (cc_jsep_action_t)action, sdp);	
  return PC_OK;
} 

const std::string& PeerConnectionImpl::localDescription() const {
  return mLocalSDP;
}

const std::string& PeerConnectionImpl::remoteDescription() const {
  return mRemoteSDP;
}

void PeerConnectionImpl::AddStream(nsRefPtr<mozilla::MediaStream>& aMediaStream)
{
  CSFLogDebug(logTag, "AddStream");
  nsRefPtr<LocalSourceStreamInfo> localSourceStream = new LocalSourceStreamInfo(aMediaStream);
  
#if 0
  // Make it the listener for info from the MediaStream and add it to the list
  aMediaStream->AddListener(localSourceStream);
#endif

  PR_Lock(mLocalSourceStreamsLock);
  mLocalSourceStreams.AppendElement(localSourceStream);
  PR_Unlock(mLocalSourceStreamsLock);
}
  
void PeerConnectionImpl::RemoveStream(nsRefPtr<mozilla::MediaStream>& aMediaStream)
{
  CSFLogDebug(logTag, "RemoveStream");

  PR_Lock(mLocalSourceStreamsLock);
  for (unsigned u = 0; u < mLocalSourceStreams.Length(); u++)
  {
    nsRefPtr<LocalSourceStreamInfo> localSourceStream = mLocalSourceStreams[u];
    if (localSourceStream->GetMediaStream() == aMediaStream)
    {
      mLocalSourceStreams.RemoveElementAt(u);
      break;
    }    
  }
  PR_Unlock(mLocalSourceStreamsLock);
}
  
void PeerConnectionImpl::CloseStreams() {

  if (mReadyState != PeerConnectionInterface::kClosed)  {
    ChangeReadyState(PeerConnectionInterface::kClosing);
  }
  
  mCall->endCall();
}


void PeerConnectionImpl::AddIceCandidate(const std::string& strCandidate)
{
  mCall->addIceCandidate(strCandidate);
}

PeerConnectionInterface::ReadyState PeerConnectionImpl::ready_state() {
  return mReadyState;
}

PeerConnectionInterface::SipccState PeerConnectionImpl::sipcc_state() {
  return mSipccState;
}

void PeerConnectionImpl::Shutdown() {
  if (kStarting == mSipccState || kStarted == mSipccState) {
    mCall->endCall();
    mCCM->removeCCObserver(this);
    mCCM->destroy();
    mCCM.reset();
    ChangeSipccState(kIdle);
  }
}

void PeerConnectionImpl::onCallEvent(ccapi_call_event_e callEvent, CSF::CC_CallPtr call, CSF::CC_CallInfoPtr info)  {
	
  if(CCAPI_CALL_EV_CREATED == callEvent) {	
    if (CREATEOFFER == info->getCallState()) {
    
      std::string sdpstr = info->getSDP();
        if (mPCObserver)
          mPCObserver->OnCreateOfferSuccess(sdpstr);
          
    } else if (CREATEANSWER == info->getCallState()) {
    
      std::string sdpstr = info->getSDP();
      if (mPCObserver)
        mPCObserver->OnCreateAnswerSuccess(sdpstr);
        
    } else if (CREATEOFFERERROR == info->getCallState()) {
    
      StatusCode code = (StatusCode)info->getStatusCode();
      if (mPCObserver)
        mPCObserver->OnCreateOfferError(code);
        
    } else if (CREATEANSWERERROR == info->getCallState()) {
    
      StatusCode code = (StatusCode)info->getStatusCode();
      if (mPCObserver)
        mPCObserver->OnCreateAnswerError(code);
        
    } else if (SETLOCALDESC == info->getCallState()) {
    
      mLocalSDP = mLocalRequestedSDP;
      StatusCode code = (StatusCode)info->getStatusCode();
      if (mPCObserver)
        mPCObserver->OnSetLocalDescriptionSuccess(code);

    } else if (SETREMOTEDESC == info->getCallState()) {
    
      mRemoteSDP = mRemoteRequestedSDP;
      StatusCode code = (StatusCode)info->getStatusCode();
      if (mPCObserver)
        mPCObserver->OnSetRemoteDescriptionSuccess(code);
        
    }  else if (SETLOCALDESCERROR == info->getCallState()) {
    
      StatusCode code = (StatusCode)info->getStatusCode();
      if (mPCObserver)
        mPCObserver->OnSetLocalDescriptionError(code);

    } else if (SETREMOTEDESCERROR == info->getCallState()) {
    
      StatusCode code = (StatusCode)info->getStatusCode();
      if (mPCObserver)
        mPCObserver->OnSetRemoteDescriptionError(code);
        
    } else if (REMOTESTREAMADD == info->getCallState()) {

      MediaTrackTable* stream = info->getMediaTracks();
      if (mPCObserver)
        mPCObserver->OnAddStream(stream);
    }
  }
}
  
void PeerConnectionImpl::onDeviceEvent(ccapi_device_event_e deviceEvent, CSF::CC_DevicePtr device, CSF::CC_DeviceInfoPtr info ) {

  cc_service_state_t state = info->getServiceState();
	
  if (CC_STATE_INS == state) {	        
    // SIPCC is up	
    if (kStarting == mSipccState || kIdle == mSipccState) {
      ChangeSipccState(kStarted);
    }
  }
}  
 
void PeerConnectionImpl::ChangeReadyState(PeerConnectionInterface::ReadyState ready_state) {
  mReadyState = ready_state;
  if (mPCObserver)
    mPCObserver->OnStateChange(PeerConnectionObserver::kReadyState);
}
 
void PeerConnectionImpl::ChangeSipccState(PeerConnectionInterface::SipccState sipcc_state) {
  mSipccState = sipcc_state;
  if (mPCObserver)
    mPCObserver->OnStateChange(PeerConnectionObserver::kSipccState);  
}

PeerConnectionImpl *PeerConnectionImpl::AcquireInstance(const std::string& handle) {
  if (peerconnections.find(handle) == peerconnections.end())
    return NULL;

  // TODO(ekr@rtfm.com): Lock the PC
  return peerconnections[handle];
}

void PeerConnectionImpl::ReleaseInstance() {
  ;
}
 
const std::string& PeerConnectionImpl::GetHandle() {
  return mHandle;
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

}  // end sipcc namespace
