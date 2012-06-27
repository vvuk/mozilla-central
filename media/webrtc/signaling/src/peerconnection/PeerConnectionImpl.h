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
 
#ifndef _PEER_CONNECTION_IMPL_H_
#define _PEER_CONNECTION_IMPL_H_

#include <string>
#include <vector>
#include <map>

#include "prlock.h"
#include "PeerConnection.h"
#include "CallControlManager.h"
#include "CC_Device.h"
#include "CC_Call.h"
#include "CC_Observer.h"

namespace sipcc {

class LocalSourceStreamInfo : public mozilla::MediaStreamListener
{
public:
  LocalSourceStreamInfo(nsRefPtr<mozilla::MediaStream>& aMediaStream);
  ~LocalSourceStreamInfo();
  
  /**
   * Notify that changes to one of the stream tracks have been queued.
   * aTrackEvents can be any combination of TRACK_EVENT_CREATED and
   * TRACK_EVENT_ENDED. aQueuedMedia is the data being added to the track
   * at aTrackOffset (relative to the start of the stream).
   */
  virtual void NotifyQueuedTrackChanges(
    mozilla::MediaStreamGraph* aGraph, 
    mozilla::TrackID aID,
    mozilla::TrackRate aTrackRate,
    mozilla::TrackTicks aTrackOffset,
    PRUint32 aTrackEvents,
    const mozilla::MediaSegment& aQueuedMedia);

  nsRefPtr<mozilla::MediaStream> GetMediaStream();
  unsigned AudioTrackCount();
  unsigned VideoTrackCount();
  
private:
  nsRefPtr<mozilla::MediaStream> mMediaStream;
  nsTArray<mozilla::TrackID> mAudioTracks;
  nsTArray<mozilla::TrackID> mVideoTracks;
};
  
class PeerConnectionImpl : public PeerConnectionInterface, CSF::CC_Observer {
public:
  PeerConnectionImpl();
  ~PeerConnectionImpl();
    
  virtual StatusCode Initialize(PeerConnectionObserver* observer);
 
  // JSEP Calls
  virtual StatusCode CreateOffer(const std::string& hints);
  virtual StatusCode CreateAnswer(const std::string& hints, const  std::string& offer);
  virtual StatusCode SetLocalDescription(Action action, const  std::string& sdp);
  virtual StatusCode SetRemoteDescription(Action action, const std::string& sdp);
  virtual const std::string& localDescription() const;
  virtual const std::string& remoteDescription() const;
  
  virtual void AddStream(nsRefPtr<mozilla::MediaStream>& aMediaStream);
  virtual void RemoveStream(nsRefPtr<mozilla::MediaStream>& aMediaStream);
  virtual void CloseStreams();

  virtual void AddIceCandidate(const std::string& strCandidate); 

  virtual ReadyState ready_state();
  virtual SipccState sipcc_state();
  
  virtual void Shutdown();
  
  virtual void onDeviceEvent(ccapi_device_event_e deviceEvent, CSF::CC_DevicePtr device, CSF::CC_DeviceInfoPtr info);
  virtual void onFeatureEvent(ccapi_device_event_e deviceEvent, CSF::CC_DevicePtr device, CSF::CC_FeatureInfoPtr feature_info) {}
  virtual void onLineEvent(ccapi_line_event_e lineEvent, CSF::CC_LinePtr line, CSF::CC_LineInfoPtr info) {}
  virtual void onCallEvent(ccapi_call_event_e callEvent, CSF::CC_CallPtr call, CSF::CC_CallInfoPtr info);

  static PeerConnectionImpl *AcquireInstance(const std::string& handle);
  virtual void ReleaseInstance(PeerConnectionImpl *);
  virtual const std::string& GetHandle() { return mHandle; }

private:
  void ChangeReadyState(PeerConnectionInterface::ReadyState ready_state);
  void ChangeSipccState(PeerConnectionInterface::SipccState sipcc_state);
        
  PeerConnectionImpl(const PeerConnectionImpl&rhs);  
  PeerConnectionImpl& operator=(PeerConnectionImpl);   
  std::string mAddr;
  CSF::CallControlManagerPtr mCCM;
  CSF::CC_DevicePtr mDevice; 
  CSF::CC_CallPtr mCall;  
  PeerConnectionObserver* mPCObserver;
  ReadyState mReadyState;
  SipccState mSipccState;

  // The SDP sent in from JS - here for debugging.
  std::string mLocalRequestedSDP;
  std::string mRemoteRequestedSDP;
  // The SDP we are using.
  std::string mLocalSDP;
  std::string mRemoteSDP;
  
  // A list of streams returned from GetUserMedia
  PRLock *mLocalSourceStreamsLock;
  nsTArray<nsRefPtr<LocalSourceStreamInfo> > mLocalSourceStreams;

  // A handle to refer to this PC with
  std::string mHandle;

  // Singleton list of all the PeerConnections
  static std::map<const std::string, PeerConnectionImpl *> peerconnections;
  static int peerconnection_index;
};
 
}  // end sipcc namespace

#endif  // _PEER_CONNECTION_IMPL_H_
