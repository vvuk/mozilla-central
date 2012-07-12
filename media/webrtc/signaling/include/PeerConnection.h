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
 
#ifndef _PEER_CONNECTION_INTERFACE_H_
#define _PEER_CONNECTION_INTERFACE_H_


#include <string>

#include "nsTArray.h"

#ifdef USE_FAKE_MEDIA_STREAMS
#include "FakeMediaStreams.h"
#else
#include "nsDOMMediaStream.h"
#endif

#include "peer_connection_types.h"

namespace sipcc {

// We have our own definition here even though it's the same
// as what's in the webrtc.org code.
// We're not guaranteed that webrtc.org will be our media backend.
struct CodecInfo
{
  int mType;
  std::string mName;
  int mFreq;
  int mPacSize;
  int mChannels;
  int mRate;
  
  CodecInfo(int type, std::string name, int freq, int pacSize, int channels, int rate) {
    mType = type;
    mName = name;
    mFreq = freq;
    mPacSize = pacSize;
    mChannels = channels;
    mRate = rate;
  }
  
};

  
enum Action { OFFER, ANSWER };


// PeerConnectionObserver has all the callbacks from the asynch PeerConnection calls.
class PeerConnectionObserver {
public:

  enum StateType {
    kReadyState,
    kIceState,
    kSdpState,
    kSipccState
  };

  // JSEP callbacks
  virtual void OnCreateOfferSuccess(const std::string& offer) = 0;
  virtual void OnCreateOfferError(StatusCode code) = 0;  
  virtual void OnCreateAnswerSuccess(const std::string& answer) = 0;
  virtual void OnCreateAnswerError(StatusCode code) = 0;
  virtual void OnSetLocalDescriptionSuccess(StatusCode code) = 0;  
  virtual void OnSetRemoteDescriptionSuccess(StatusCode code) = 0;
  virtual void OnSetLocalDescriptionError(StatusCode code) = 0;
  virtual void OnSetRemoteDescriptionError(StatusCode code) = 0;    
  
  // Notification of one of several types of state changed
  virtual void OnStateChange(StateType state_changed) = 0;

  // Changes to MediaStreams
  virtual void OnAddStream(MediaTrackTable* stream) = 0;
  virtual void OnRemoveStream() = 0;
  virtual void OnAddTrack() = 0;
  virtual void OnRemoveTrack() = 0;

  // When SDP is parsed and a candidate line is found this method is called.
  // It should hook back into the media transport to notify it of ICE candidates listed in the SDP
  // PeerConnectionImpl does not parse ICE candidates, just pulls them out of the SDP.
  virtual void FoundIceCandidate(const std::string& strCandidate) = 0;

protected:
  ~PeerConnectionObserver() {}
};


class PeerConnectionInterface {
public:

  enum ReadyState {
    kNew,
    kNegotiating,
    kActive,
    kClosing,
    kClosed
  };
  
  enum SipccState {
    kIdle,
    kStarting,
    kStarted
  };

  // TODO(ekr@rtfm.com): make this conform to the specifications
  enum IceState {
    kIceGathering,
    kIceWaiting,
    kIceChecking,
    kIceConnected,
    kIceFailed
  };

public:
  // Factory for creating a PeerConnectionInterface object
  static PeerConnectionInterface *CreatePeerConnection();

  // Initialize() should be called before any of the JSEP calls
  // It will startup and allocate everything needed for the state machine
  // It is asynch - observer->OnStateChange() will be called with the states
  // kStarting and then kStarted when it's ready to be used.
  virtual StatusCode Initialize(PeerConnectionObserver* observer) = 0;

  // JSEP calls
  virtual StatusCode CreateOffer(const std::string& hints) = 0;
  virtual StatusCode CreateAnswer(const std::string& hints, const  std::string& offer) = 0;
  virtual StatusCode SetLocalDescription(Action action, const  std::string& sdp) = 0;
  virtual StatusCode SetRemoteDescription(Action action, const std::string& sdp) = 0;
 
  // These methods don't do anything but return the latest SDP we are using local or remote.
  virtual const std::string& localDescription() const = 0;
  virtual const std::string& remoteDescription() const = 0;

  // Adds the stream created by GetUserMedia
  virtual void AddStream(nsRefPtr<nsDOMMediaStream>& aMediaStream) = 0;
  virtual void RemoveStream(nsRefPtr<nsDOMMediaStream>& aMediaStream) = 0;
  virtual void CloseStreams() = 0;

  // As the ICE candidates roll in this one should be called each time
  // in order to keep the candidate list up-to-date for the next SDP-related call
  // PeerConnectionImpl does not parse ICE candidates, just sticks them into the SDP.
  virtual void AddIceCandidate(const std::string& strCandidate) = 0; 

  // The state of the offer/answer negotiation
  virtual ReadyState ready_state() = 0;

  // The state of the SIPCC engine, e.g. 'Started'
  virtual SipccState sipcc_state() = 0;
  
  // ICE state
  virtual IceState ice_state() = 0;

  // puts the SIPCC engine back to 'kIdle', shuts down threads, deletes state, etc.
  virtual void Close() = 0;
  
  virtual ~PeerConnectionInterface() {};

protected:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(PeerConnectionInterface);
};

}  // end sipcc namespace


#endif // _PEER_CONNECTION_INTERFACE_H_

