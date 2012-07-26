/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FAKE_MEDIA_STREAM_H_
#define FAKE_MEDIA_STREAM_H_

// #includes from MediaStream.h
#include "mozilla/Mutex.h"
#include "nsAudioStream.h"
#include "nsTArray.h"
#include "nsIRunnable.h"
#include "nsISupportsImpl.h"
#include "nsIDOMMediaStream.h"

class Fake_MediaStreamListener
{
public:
  virtual ~Fake_MediaStreamListener() {}

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Fake_MediaStreamListener)
};

class Fake_MediaStream
{
public:
  Fake_MediaStream () {}
  virtual ~Fake_MediaStream() {}

  void AddListener(Fake_MediaStreamListener *aListener) {}
};

class Fake_nsDOMMediaStream : public nsIDOMMediaStream
{
public:
  Fake_nsDOMMediaStream() : mMediaStream(new Fake_MediaStream()) {}
  virtual ~Fake_nsDOMMediaStream() { if (mMediaStream) { delete mMediaStream;} }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMMEDIASTREAM

  Fake_MediaStream *GetStream() { return mMediaStream; }

  static already_AddRefed<Fake_nsDOMMediaStream> CreateInputStream(PRUint32 aHintContents);

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


class Fake_MediaSegment
{
public:
  Fake_MediaSegment() : mType(AUDIO) {}
  virtual ~Fake_MediaSegment() {}

  enum Type {
    AUDIO,
    VIDEO,
    TYPE_COUNT
  };

  Type GetType() const { return mType; }

  Type mType;
};

typedef Fake_nsDOMMediaStream nsDOMMediaStream;

namespace mozilla
{

typedef Fake_MediaStream MediaStream;
typedef Fake_MediaStreamListener MediaStreamListener;
typedef Fake_MediaStreamGraph MediaStreamGraph;
typedef Fake_MediaSegment MediaSegment;

typedef PRInt32 TrackID;
typedef PRInt32 TrackRate;
typedef PRInt64 TrackTicks;

}

#endif
