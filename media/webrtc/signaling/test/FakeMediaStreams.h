/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FAKE_MEDIA_STREAM_H_
#define FAKE_MEDIA_STREAM_H_

#include "nsNetCID.h"
#include "nsITimer.h"
#include "nsComponentManagerUtils.h"
#include "nsIComponentManager.h"
#include "nsIComponentRegistrar.h"

// #includes from MediaStream.h
#include "mozilla/Mutex.h"
#include "AudioSegment.h"
#include "ImageLayers.h"
#include "MediaSegment.h"
#include "StreamBuffer.h"
#include "nsAudioStream.h"
#include "nsTArray.h"
#include "nsIRunnable.h"
#include "nsISupportsImpl.h"
#include "nsIDOMMediaStream.h"

namespace mozilla {
   class MediaStreamGraph;
   class MediaSegment;
};

class Fake_SourceMediaStream;

class Fake_MediaStreamListener
{
public:
  virtual ~Fake_MediaStreamListener() {}

  virtual void NotifyQueuedTrackChanges(mozilla::MediaStreamGraph* aGraph, mozilla::TrackID aID,
                                        mozilla::TrackRate aTrackRate,
                                        mozilla::TrackTicks aTrackOffset,
                                        PRUint32 aTrackEvents,
                                        const mozilla::MediaSegment& aQueuedMedia)  = 0;
  virtual void NotifyPull(mozilla::MediaStreamGraph* aGraph, mozilla::StreamTime aDesiredTime) = 0;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Fake_MediaStreamListener)
};


// Note: only one listener supported
class Fake_MediaStream {
public:
  Fake_MediaStream () : mListener(NULL) {}
  virtual ~Fake_MediaStream() { Stop(); }

  void AddListener(Fake_MediaStreamListener *aListener) {
    mListener = aListener;
  }

  void RemoveListener(Fake_MediaStreamListener *aListener) {
    mListener = NULL;
  }
  virtual Fake_SourceMediaStream *AsSourceStream() { return NULL; }

  virtual nsresult Start() { return NS_OK; }
  virtual nsresult Stop() { return NS_OK; }

 protected:
  Fake_MediaStreamListener *mListener;
};

class Fake_SourceMediaStream : public Fake_MediaStream {
 public:
  Fake_SourceMediaStream() : mPullEnabled(false) {}

  void AddTrack(mozilla::TrackID aID, mozilla::TrackRate aRate, mozilla::TrackTicks aStart,
                mozilla::MediaSegment* aSegment) {}

  void AppendToTrack(mozilla::TrackID aID, mozilla::MediaSegment* aSegment) {}

  void AdvanceKnownTracksTime(mozilla::StreamTime aKnownTime) {}
  
  void SetPullEnabled(bool aEnabled) {
    mPullEnabled = aEnabled;
  }
  virtual Fake_SourceMediaStream *AsSourceStream() { return this; }
 protected:
  bool mPullEnabled;
};

class Fake_nsDOMMediaStream : public nsIDOMMediaStream
{
public:
  Fake_nsDOMMediaStream() : mMediaStream(new Fake_MediaStream()) {}
  Fake_nsDOMMediaStream(Fake_MediaStream *stream) : 
      mMediaStream(stream) {}

  virtual ~Fake_nsDOMMediaStream() {
    // Note: memory leak
    mMediaStream->Stop();
  }

  
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMMEDIASTREAM

  Fake_MediaStream *GetStream() { return mMediaStream; }

  // Hints to tell the SDP generator about whether this
  // MediaStream probably has audio and/or video
  enum {
    HINT_CONTENTS_AUDIO = 0x00000001U,
    HINT_CONTENTS_VIDEO = 0x00000002U
  };
  PRUint32 GetHintContents() const { return mHintContents; }
  void SetHintContents(PRUint32 aHintContents) { mHintContents = aHintContents; }

private:
  Fake_MediaStream *mMediaStream;

  // tells the SDP generator about whether this
  // MediaStream probably has audio and/or video
  PRUint32 mHintContents;
};

class Fake_MediaStreamGraph
{
public:
  virtual ~Fake_MediaStreamGraph() {}
};


class Fake_MediaStreamBase : public Fake_SourceMediaStream,
                             public nsITimerCallback {
 public:
  virtual nsresult Start();
  virtual nsresult Stop();

  NS_DECL_ISUPPORTS
  NS_IMETHODIMP Notify(nsITimer* aTimer) = 0;

 private:
  nsCOMPtr<nsITimer> mTimer;
};


class Fake_AudioStreamSource : public Fake_MediaStreamBase {
 public:
  Fake_AudioStreamSource() : Fake_MediaStreamBase() {}

  NS_DECL_NSITIMERCALLBACK
};

class Fake_VideoStreamSource : public Fake_MediaStreamBase {
 public:
  Fake_VideoStreamSource() : Fake_MediaStreamBase() {}

  NS_DECL_NSITIMERCALLBACK

 protected:
  nsCOMPtr<nsITimer> mTimer;
};

class Fake_AudioStreamSink : public Fake_MediaStreamBase {
 public:
  Fake_AudioStreamSink() : Fake_MediaStreamBase() {}

  NS_DECL_NSITIMERCALLBACK
};



typedef Fake_nsDOMMediaStream nsDOMMediaStream;

namespace mozilla {
typedef Fake_MediaStream MediaStream;
typedef Fake_SourceMediaStream SourceMediaStream;
typedef Fake_MediaStreamListener MediaStreamListener;
}

#endif
