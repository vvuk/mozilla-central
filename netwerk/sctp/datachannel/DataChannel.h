/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NETWERK_SCTP_DATACHANNEL_DATACHANNEL_H_
#define NETWERK_SCTP_DATACHANNEL_DATACHANNEL_H_

#include <string>
#include "nsISupports.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "nsTArray.h"
#include "mozilla/Mutex.h"

extern "C" {
  struct socket;
  struct sctp_rcvinfo;
  struct rtcweb_datachannel_open;
  struct rtcweb_datachannel_open_response;
  struct rtcweb_datachannel_ack;
}

namespace mozilla {

class DTLSConnection;
class DataChannelConnection;
class DataChannel;
class DataChannelOnMessageAvailable;

// Implemented by consumers of a Channel to receive messages.
// Can't nest it in DataChannelConnection because C++ doesn't allow forward
// refs to embedded classes
class DataChannelListener {
public:
  virtual ~DataChannelListener() {}

  // Called when a DOMString message is received.
  virtual void OnMessageAvailable(nsISupports *aContext,
                                  const nsACString& message) = 0;

  // Called when a binary message is received.
  virtual void OnBinaryMessageAvailable(nsISupports *aContext,
                                        const nsACString& message) = 0;

  // Called when the channel is connected
  virtual void OnChannelConnected(nsISupports *aContext) {}

  // Called when the channel is closed
  virtual void OnChannelClosed(nsISupports *aContext) {}
};


// One per PeerConnection
class DataChannelConnection {
public:

  class DataConnectionListener {
  public:
    virtual ~DataConnectionListener() {}

    // Called when a the connection is open
    virtual void OnConnection() = 0;

    // Called when a the connection is lost/closed
    virtual void OnClosedConnection() = 0;

    // Called when a new DataChannel has been opened by the other side.
    virtual void OnDataChannel(DataChannel *channel) = 0;
  };

  DataChannelConnection(DataConnectionListener *listener);
  virtual ~DataChannelConnection() {} // XXX need cleanup code for SCTP sockets

  bool Init(unsigned short port /* XXX DTLSConnection &tunnel*/);

  // XXX These will need to be replaced with something better
  // They block; they require something to decide on listener/connector,
  // etc.  Apparently SCTP associations can be simultaneously opened from
  // each end and the stack resolves it.
  bool Listen(unsigned short port);
  bool Connect(const char *addr, unsigned short port);

  typedef enum {
    RELIABLE=0,
    RELIABLE_STREAM = 1,
    UNRELIABLE = 2,
    PARTIAL_RELIABLE_REXMIT = 3,
    PARTIAL_RELIABLE_TIMED = 4
  } Type;
    
  DataChannel *Open(/* const std::wstring& channel_label,*/
                    Type type, bool inOrder, 
                    PRUint32 timeout, DataChannelListener *aListener,
                    nsISupports *aContext);

  void Close(PRUint16 stream);
  void CloseAll();

  PRInt32 SendMsgCommon(PRUint16 stream, const nsACString &aMsg, bool isBinary);
  PRInt32 SendMsg(PRUint16 stream, const nsACString &aMsg)
    {
      return SendMsgCommon(stream, aMsg, false);
    }
  PRInt32 SendBinaryMsg(PRUint16 stream, const nsACString &aMsg)
    {
      return SendMsgCommon(stream, aMsg, true);
    }

  // Called on data reception from the SCTP library
  // must(?) be public so my c->c++ tramploine can call it
  int ReceiveCallback(struct socket* sock, void *data, size_t datalen, struct sctp_rcvinfo rcv);

  // Find out state
  enum {
    CONNECTING = 0U,
    OPEN = 1U,
    CLOSING = 2U,
    CLOSED = 3U
  };
  PRUint16 GetReadyState() { return mState; }

  friend class DataChannel;
  Mutex  mLock;

  // XXX I'd like this to be protected or private...
  DataConnectionListener *mListener;

private:
  void SendErrorResponse(struct socket* sock,
                         struct rtcweb_datachannel_open *msg,
                         struct sctp_rcvinfo rcv,
                         uint8_t error);

  // NOTE: while these arrays will auto-expand, increases in the number of
  // channels available from the stack must be negotiated!
  typedef struct _DataChannelOut {
    // XXX these could roll up into a single state var
    bool     pending;      // open sent, no open_response yet
    bool     waiting_ack;  // open_response sent, but no ack yet
    uint8_t  channel_type;
    uint16_t flags;
    uint16_t reverse;
    uint16_t reliability_params;
    int16_t  priority;
    /* FIX! label, ref/release */
    DataChannel *channel;
  } DataChannelOut;

  nsAutoTArray<DataChannelOut,16> mChannelsOut;
  //DataChannelOut mChannelsOut[16];

  typedef struct _DataChannelIn {
    PRUint16 outgoing;
  } DataChannelIn;

  nsAutoTArray<DataChannelIn,16> mChannelsIn;
  //DataChannelIn mChannelsIn[16];

  struct socket *mMasterSocket;
  struct socket *mSocket;
  PRUint16 mNumChannels;
  PRUint16 mState;
};

class DataChannel {
public:
  enum {
    CONNECTING = 0U,
    OPEN = 1U,
    CLOSING = 2U,
    CLOSED = 3U
  };

  DataChannel(DataChannelConnection *connection,
              PRUint16 stream, PRUint16 state,
              DataChannelListener *aListener,
              nsISupports *aContext) : 
    mListener(aListener), mState(state), mConnection(connection), mStream(stream), mContext(aContext)
    {
      NS_ASSERTION(mConnection,"NULL connection");
    }

  ~DataChannel()
    {
      Close();
    }

  // Close this DataChannel.  Can be called multiple times.
  void Close() 
    { 
      if (mStream < 0) // Note that mStream is PRInt32, not PRUint16
        return;
      mState = CLOSING;
      mConnection->Close(mStream);
      mStream = -1;
    }

  // Set the listener (especially for channels created from the other side)
  void SetListener(DataChannelListener *aListener, nsISupports *aContext)
    { mContext = aContext; mListener = aListener; } // XXX Locking?

  // Send a string
  bool SendMsg(const nsACString &aMsg)
    {
      if (mStream >= 0)
        return (mConnection->SendMsg(mStream, aMsg) > 0);
      else
        return false;
    }

  // Send a binary message (blob or TypedArray)
  bool SendBinaryMsg(const nsACString &aMsg)
    {
      if (mStream >= 0)
        return (mConnection->SendBinaryMsg(mStream, aMsg) > 0);
      else
        return false;
    }

  // XXX I don't think we need SendBinaryStream()

  // Amount of data buffered to send
  PRUint32 GetBufferedAmount() { return 0; /* XXX */ }

  // Find out state
  PRUint16 GetReadyState() { return mState; }
  void SetReadyState(PRUint16 aState) { mState = aState; }

  // XXX I'd like this to be protected or private...
  DataChannelListener *mListener;

private:
  friend class DataChannelOnMessageAvailable;
  friend class DataChannelConnection;

  PRUint16 mState;
  DataChannelConnection *mConnection; // XXX nsRefPtr<DataChannelConnection> mConnection;
  PRInt32 mStream;
  nsCOMPtr<nsISupports> mContext;
};

// used to dispatch notifications of incoming data to the main thread
// Patterned on CallOnMessageAvailable in WebSockets
class DataChannelOnMessageAvailable : public nsRunnable
{
public:
  enum {
    ON_CONNECTION,
    ON_DISCONNECTED,
    ON_CHANNEL_CREATED,
    ON_CHANNEL_OPEN,
    ON_CHANNEL_CLOSED,
    ON_DATA,
  };  /* types */

  DataChannelOnMessageAvailable(PRInt32     aType,
                                DataChannel *aChannel,
                                nsCString   &aData,  // XXX this causes inefficiency
                                PRInt32     aLen)
    : mType(aType),
      mChannel(aChannel),
      mData(aData),
      mLen(aLen) {}

  DataChannelOnMessageAvailable(PRInt32     aType,
                                DataChannel *aChannel)
    : mType(aType),
      mChannel(aChannel) {}
  // XXX is it safe to leave mData/mLen uninitialized?  This should only be
  // used for notifications that don't use them, but I'd like more
  // bulletproof compile-time checking.

  DataChannelOnMessageAvailable(PRInt32     aType,
                                DataChannelConnection *aConnection,
                                DataChannel *aChannel)
    : mType(aType),
      mChannel(aChannel),
      mConnection(aConnection) {}

  NS_IMETHOD Run()
  {
    switch (mType) {
      case ON_DATA:
        if (mLen < 0)
          mChannel->mListener->OnMessageAvailable(mChannel->mContext, mData);
        else
          mChannel->mListener->OnBinaryMessageAvailable(mChannel->mContext, mData);
        break;
      case ON_CHANNEL_OPEN:
        mChannel->mListener->OnChannelConnected(mChannel->mContext);
        break;
      case ON_CHANNEL_CLOSED:
        mChannel->mListener->OnChannelClosed(mChannel->mContext);
        break;
      case ON_CHANNEL_CREATED:
        mConnection->mListener->OnDataChannel(mChannel);
        break;
      case ON_CONNECTION:
        mConnection->mListener->OnConnection();
        break;
      case ON_DISCONNECTED:
        mConnection->mListener->OnClosedConnection();
        break;
    }
    return NS_OK;
  }

private:
  ~DataChannelOnMessageAvailable() {}

  PRInt32                           mType;
  // XXX should use union
  // XXX these need to be refptrs so as to hold them open until it's delivered
  DataChannel                       *mChannel;    // XXX careful of ownership! 
  DataChannelConnection             *mConnection; // XXX careful of ownership! - should be nsRefPtr
  nsCString                         mData;
  PRInt32                           mLen;
};

}

#endif  // NETWERK_SCTP_DATACHANNEL_DATACHANNEL_H_
