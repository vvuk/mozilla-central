/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <iostream>
#include <string>

using namespace std;

#include "base/basictypes.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

#include "nspr.h"
#include "nss.h"
#include "ssl.h"
#include "prthread.h"

#include "FakeMediaStreams.h"
#include "FakeMediaStreamsImpl.h"
#include "PeerConnectionImpl.h"
#include "runnable_utils.h"
#include "nsStaticComponents.h"
#include "nsIDOMRTCPeerConnection.h"

#include "mtransport_test_utils.h"
MtransportTestUtils test_utils;

static int kDefaultTimeout = 5000;


namespace test {

static const std::string strSampleSdpAudioVideoNoIce =
  "v=0\r\n"
  "o=Cisco-SIPUA 4949 0 IN IP4 10.86.255.143\r\n"
  "s=SIP Call\r\n"
  "t=0 0\r\n"
  "a=ice-ufrag:qkEP\r\n"
  "a=ice-pwd:ed6f9GuHjLcoCN6sC/Eh7fVl\r\n"
  "m=audio 16384 RTP/AVP 0 8 9 101\r\n"
  "c=IN IP4 10.86.255.143\r\n"
  "a=rtpmap:0 PCMU/8000\r\n"
  "a=rtpmap:8 PCMA/8000\r\n"
  "a=rtpmap:9 G722/8000\r\n"
  "a=rtpmap:101 telephone-event/8000\r\n"
  "a=fmtp:101 0-15\r\n"
  "a=sendrecv\r\n"
  "a=candidate:1 1 UDP 2130706431 192.168.2.1 50005 typ host\r\n"
  "a=candidate:2 2 UDP 2130706431 192.168.2.2 50006 typ host\r\n"
  "m=video 1024 RTP/AVP 97\r\n"
  "c=IN IP4 10.86.255.143\r\n"
  "a=rtpmap:97 H264/90000\r\n"
  "a=fmtp:97 profile-level-id=42E00C\r\n"
  "a=sendrecv\r\n"
  "a=candidate:1 1 UDP 2130706431 192.168.2.3 50007 typ host\r\n"
  "a=candidate:2 2 UDP 2130706431 192.168.2.4 50008 typ host\r\n";

class TestObserver : public IPeerConnectionObserver
{
public:
  enum Action {
    OFFER,
    ANSWER
  };

  enum StateType {
    kReadyState,
    kIceState,
    kSdpState,
    kSipccState
  };

  enum ResponseState {
    stateNoResponse,
    stateSuccess,
    stateError
  };

  TestObserver(sipcc::PeerConnectionImpl *peerConnection) :
    state(stateNoResponse),
    onAddStreamCalled(false),
    pc(peerConnection) {
  }

  virtual ~TestObserver() {}

  std::vector<nsDOMMediaStream *> GetStreams() { return streams; }

  NS_DECL_ISUPPORTS
  NS_DECL_IPEERCONNECTIONOBSERVER

  ResponseState state;
  char *lastString;
  PRUint32 lastStatusCode;
  PRUint32 lastStateType;
  bool onAddStreamCalled;

private:
  sipcc::PeerConnectionImpl *pc;
  std::vector<nsDOMMediaStream *> streams;
};

NS_IMPL_THREADSAFE_ISUPPORTS1(TestObserver, IPeerConnectionObserver)

NS_IMETHODIMP
TestObserver::OnCreateOfferSuccess(const char* offer)
{
  state = stateSuccess;
  cout << "onCreateOfferSuccess = " << offer << endl;
  lastString = strdup(offer);
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnCreateOfferError(PRUint32 code)
{
  state = stateError;
  cout << "onCreateOfferError" << endl;
  lastStatusCode = code;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnCreateAnswerSuccess(const char* answer)
{
  state = stateSuccess;
  cout << "onCreateAnswerSuccess = " << answer << endl;
  lastString = strdup(answer);
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnCreateAnswerError(PRUint32 code)
{
  state = stateError;
  lastStatusCode = code;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnSetLocalDescriptionSuccess(PRUint32 code)
{
  state = stateSuccess;
  lastStatusCode = code;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnSetRemoteDescriptionSuccess(PRUint32 code)
{
  state = stateSuccess;
  lastStatusCode = code;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnSetLocalDescriptionError(PRUint32 code)
{
  state = stateError;
  lastStatusCode = code;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnSetRemoteDescriptionError(PRUint32 code)
{
  state = stateError;
  lastStatusCode = code;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnStateChange(PRUint32 state_type)
{
  nsresult rv;
  PRUint32 gotstate;

  switch (state_type)
  {
  case kReadyState:
    rv = pc->GetReadyState(&gotstate);
    NS_ENSURE_SUCCESS(rv, rv);
    cout << "Ready State: " << gotstate << endl;
    break;
  case kIceState:
    rv = pc->GetIceState(&gotstate);
    NS_ENSURE_SUCCESS(rv, rv);
    cout << "ICE State: " << gotstate << endl;
    break;
  case kSdpState:
    cout << "SDP State: " << endl;
    NS_ENSURE_SUCCESS(rv, rv);
    break;
  case kSipccState:
    rv = pc->GetSipccState(&gotstate);
    NS_ENSURE_SUCCESS(rv, rv);
    cout << "SIPCC State: " << gotstate << endl;
    break;
  default:
    // Unknown State
    break;
  }

  state = stateSuccess;
  lastStateType = state_type;
  return NS_OK;
}


NS_IMETHODIMP
TestObserver::OnAddStream(nsIDOMMediaStream *stream)
{
  PR_ASSERT(stream);

  nsDOMMediaStream *ms = static_cast<nsDOMMediaStream *>(stream);

  cout << "OnAddStream called hints=" << ms->GetHintContents() << endl;
  state = stateSuccess;
  onAddStreamCalled = true;

  // We know that the media stream is secretly a Fake_SourceMediaStream,
  // so now we can start it pulling from us
  Fake_SourceMediaStream *fs = static_cast<Fake_SourceMediaStream *>(ms->GetStream());

  nsresult ret;
  test_utils.sts_target()->Dispatch(
    WrapRunnableRet(fs, &Fake_SourceMediaStream::Start, &ret),
    NS_DISPATCH_SYNC);

  streams.push_back(ms);

  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnRemoveStream()
{
  state = stateSuccess;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnAddTrack()
{
  state = stateSuccess;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnRemoveTrack()
{
  state = stateSuccess;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::FoundIceCandidate(const char* strCandidate)
{
  return NS_OK;
}

class SignalingAgent {
 public:
  SignalingAgent() {
    Init();
  }
  ~SignalingAgent() {
    Close();
  }

  void Init()
  {
    size_t found = 2;
    ASSERT_TRUE(found > 0);

    pc = sipcc::PeerConnectionImpl::CreatePeerConnection();
    ASSERT_TRUE(pc);

    pObserver = new TestObserver(pc);
    ASSERT_TRUE(pObserver);

    ASSERT_EQ(pc->Initialize(pObserver, nsnull), NS_OK);

    ASSERT_TRUE_WAIT(sipcc_state() == sipcc::PeerConnectionImpl::kStarted,
                     kDefaultTimeout);
    ASSERT_TRUE_WAIT(ice_state() == sipcc::PeerConnectionImpl::kIceWaiting, 5000);
    cout << "Init Complete" << endl;
  }

  PRUint32 sipcc_state()
  {
    PRUint32 res;

    pc->GetSipccState(&res);
    return res;
  }

  PRUint32 ice_state()
  {
    PRUint32 res;

    pc->GetIceState(&res);
    return res;
  }

  void Close()
  {
    cout << "Close" << endl;
    pc->Close();
    // Shutdown is synchronous evidently.
    // ASSERT_TRUE(pObserver->WaitForObserverCall());
    // ASSERT_EQ(pc->sipcc_state(), sipcc::PeerConnectionInterface::kIdle);
  }

  char* offer() const { return offer_; }
  char* answer() const { return answer_; }

  void CreateOffer(const char* hints, bool audio, bool video) {

    // Create a media stream as if it came from GUM
    Fake_AudioStreamSource *audio_stream = 
      new Fake_AudioStreamSource();

    nsresult ret;
    test_utils.sts_target()->Dispatch(
      WrapRunnableRet(audio_stream, &Fake_MediaStream::Start, &ret),
        NS_DISPATCH_SYNC);

    ASSERT_TRUE(NS_SUCCEEDED(ret));

    // store in object to be used by RemoveStream
    nsRefPtr<nsDOMMediaStream> domMediaStream = new nsDOMMediaStream(audio_stream);
    domMediaStream_ = domMediaStream;

    PRUint32 aHintContents = 0;

    if (audio) {
      aHintContents |= nsDOMMediaStream::HINT_CONTENTS_AUDIO;
    }
    if (video) {
      aHintContents |= nsDOMMediaStream::HINT_CONTENTS_VIDEO;
    }

    PR_ASSERT(aHintContents);

    domMediaStream->SetHintContents(aHintContents);

    pc->AddStream(domMediaStream);
    domMediaStream_ = domMediaStream;

    // Now call CreateOffer as JS would
    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->CreateOffer(hints), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
    SDPSanityCheck(pObserver->lastString, audio, video);
    offer_ = pObserver->lastString;
  }

  void CreateOfferExpectError(const char* hints) {
    ASSERT_EQ(pc->CreateOffer(hints), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateError, kDefaultTimeout);
  }

  void CreateAnswer(const char* offer, const char* hints) {
    // Create a media stream as if it came from GUM
    nsRefPtr<nsDOMMediaStream> domMediaStream = new nsDOMMediaStream();

    // Pretend GUM got both audio and video.
    domMediaStream->SetHintContents(nsDOMMediaStream::HINT_CONTENTS_AUDIO | nsDOMMediaStream::HINT_CONTENTS_VIDEO);

    pc->AddStream(domMediaStream);

    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->CreateAnswer(hints, offer), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
    SDPSanityCheck(pObserver->lastString, true, true);
    answer_ = pObserver->lastString;
  }

  void CreateOfferRemoveStream(const char* hints, bool audio, bool video) {

    PRUint32 aHintContents = 0;

    if (!audio) {
      aHintContents |= nsDOMMediaStream::HINT_CONTENTS_VIDEO;
    }
    if (!video) {
      aHintContents |= nsDOMMediaStream::HINT_CONTENTS_AUDIO;
    }

    domMediaStream_->SetHintContents(aHintContents);

    // When complete RemoveStream will remove and entire stream and its tracks
    // not just disable a track as this is currently doing
    pc->RemoveStream(domMediaStream_);

    // Now call CreateOffer as JS would
    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->CreateOffer(hints), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
    SDPSanityCheck(pObserver->lastString, video, audio);
    offer_ = pObserver->lastString;
  }

  void SetRemote(TestObserver::Action action, char* remote) {
    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->SetRemoteDescription(action, remote), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
  }

  void SetLocal(TestObserver::Action action, char* local) {
    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->SetLocalDescription(action, local), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
  }

  bool IceCompleted() {
    PRUint32 state;
    pc->GetIceState(&state);
    return state == sipcc::PeerConnectionImpl::kIceConnected;
  }

#if 0
  void CreateOfferSetLocal(const char* hints) {
      CreateOffer(hints);

      pObserver->state = TestObserver::stateNoResponse;
      ASSERT_EQ(pc->SetLocalDescription(sipcc::OFFER, pObserver->lastString), NS_OK);
      ASSERT_TRUE(pObserver->WaitForObserverCall());
      ASSERT_EQ(pObserver->state, TestObserver::stateSuccess);
      ASSERT_EQ(pc->SetRemoteDescription(sipcc::OFFER, strSampleSdpAudioVideoNoIce), NS_OK);
      ASSERT_TRUE(pObserver->WaitForObserverCall());
      ASSERT_EQ(pObserver->state, TestObserver::stateSuccess);
    }

    void CreateAnswer(const char* hints)
    {
      std::string offer = strSampleSdpAudioVideoNoIce;
      std::string strHints(hints);

      ASSERT_EQ(pc->CreateAnswer(strHints, offer), NS_OK);
      ASSERT_TRUE(pObserver->WaitForObserverCall());
      ASSERT_EQ(pObserver->state, TestObserver::stateSuccess);
      SDPSanityCheck(pObserver->lastString, true, true);
    }
#endif

  int GetPacketsReceived(int stream) {
    std::vector<nsDOMMediaStream *> streams = pObserver->GetStreams();

    if (streams.size() <= stream) {
      return 0;
    }

    return streams[stream]->GetStream()->AsSourceStream()->GetSegmentsAdded();
  }

  int GetPacketsSent(int stream) {
    return static_cast<Fake_MediaStreamBase *>(
        domMediaStream_->GetStream())->GetSegmentsAdded();
  }


public:
  mozilla::RefPtr<sipcc::PeerConnectionImpl> pc;
  nsRefPtr<TestObserver> pObserver;
  char* offer_;
  char* answer_;
  nsRefPtr<nsDOMMediaStream> domMediaStream_;


private:
  void SDPSanityCheck(std::string sdp, bool shouldHaveAudio, bool shouldHaveVideo)
  {
    ASSERT_NE(sdp.find("v=0"), std::string::npos);
    ASSERT_NE(sdp.find("c=IN IP4"), std::string::npos);

    if (shouldHaveAudio)
    {
      ASSERT_NE(sdp.find("a=rtpmap:0 PCMU/8000"), std::string::npos);
    }

    if (shouldHaveVideo)
    {
      ASSERT_NE(sdp.find("a=rtpmap:97 H264/90000"), std::string::npos);
    }
  }
};

/*
class SignalingEnvironment : public ::testing::Environment {
 public:
  void TearDown() {
    sipcc::PeerConnectionImpl::Shutdown();
  }
};
*/

class SignalingTest : public ::testing::Test {
public:
  void CreateOffer(const char* hints) {
    a1_.CreateOffer(hints, true, true);
  }

  void CreateSetOffer(const char* hints) {
    a1_.CreateOffer(hints, true, true);
    a1_.SetLocal(TestObserver::OFFER, a1_.offer());
  }

  void OfferAnswer(const char* ahints, const char* bhints) {
    a1_.CreateOffer(ahints, true, true);
    a1_.SetLocal(TestObserver::OFFER, a1_.offer());
    a2_.SetRemote(TestObserver::OFFER, a1_.offer());
    a2_.CreateAnswer(bhints, a1_.offer());
    a2_.SetLocal(TestObserver::ANSWER, a2_.answer());
    a1_.SetRemote(TestObserver::ANSWER, a2_.answer());
    ASSERT_TRUE_WAIT(a1_.IceCompleted() == true, kDefaultTimeout);
    ASSERT_TRUE_WAIT(a2_.IceCompleted() == true, kDefaultTimeout);
  }

  void CreateOfferVideoOnly(const char* hints) {
    a1_.CreateOffer(hints, false, true);
  }

  void CreateOfferAudioOnly(char * hints) {
    a1_.CreateOffer(hints, true, false);
  }

  void CreateOfferRemoveStream(char * hints) {
	a1_.CreateOffer(hints, true, true);
    a1_.CreateOfferRemoveStream(hints, false, true);
  }

 protected:
  SignalingAgent a1_;  // Canonically "caller"
  SignalingAgent a2_;  // Canonically "callee"
};


TEST_F(SignalingTest, JustInit)
{
}

TEST_F(SignalingTest, CreateOfferNoHints)
{
  CreateOffer("");
}

TEST_F(SignalingTest, CreateSetOffer)
{
  CreateSetOffer("");
}

TEST_F(SignalingTest, CreateOfferVideoOnly)
{
  CreateOfferVideoOnly("");
}

TEST_F(SignalingTest, CreateOfferAudioOnly)
{
  CreateOfferAudioOnly("");
}

TEST_F(SignalingTest, CreateOfferRemoveStream)
{
	CreateOfferRemoveStream("");
}

TEST_F(SignalingTest, OfferAnswer)
{
  OfferAnswer("", "");
}

TEST_F(SignalingTest, FullCall)
{
  OfferAnswer("", "");
  PR_Sleep(kDefaultTimeout * 2); // Wait for some data to get written

  // Check that we wrote a bunch of data
  ASSERT_GE(a1_.GetPacketsSent(0), 40);
  //ASSERT_GE(a2_.GetPacketsSent(0), 40);
  //ASSERT_GE(a1_.GetPacketsReceived(0), 40);
  ASSERT_GE(a2_.GetPacketsReceived(0), 40);
}

//TEST_F(SignalingTest, CreateOfferHints)
//{
//  CreateOffer("audio,video");
//}

//TEST_F(SignalingTest, CreateOfferBadHints)
//{
//  CreateOfferExpectError("9.uoeuhaoensthuaeugc.pdu8g");
//}

//TEST_F(SignalingTest, CreateOfferSetLocal)
//{
//  CreateOfferSetLocal("");
//}

//TEST_F(SignalingTest, CreateAnswerNoHints)
//{
//  CreateAnswer("");
//}

} // End namespace test.

int main(int argc, char **argv)
{
  test_utils.InitServices();
  NSS_NoDB_Init(NULL);
  NSS_SetDomesticPolicy();

  //AddGlobalTestEnvironment(new SignalingEnvironment);
  ::testing::InitGoogleTest(&argc, argv);

  for(int i=0; i<argc; i++) {
    if (!strcmp(argv[i],"-t")) {
      kDefaultTimeout = 20000;
    }

  }

  int result = RUN_ALL_TESTS();
  return result;
}
