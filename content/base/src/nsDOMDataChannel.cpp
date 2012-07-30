#include "nsIDOMDataChannel.h"
#include "nsIDOMFile.h"
#include "nsIDOMMessageEvent.h"
#include "nsIJSNativeInitializer.h"

#include "jsval.h"

#include "nsAutoPtr.h"
#include "nsContentUtils.h"
#include "nsDOMEventTargetHelper.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDOMClassInfo.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"

#include "DataChannel.h"

class nsDOMDataChannel : public nsDOMEventTargetHelper,
                         public nsIDOMDataChannel,
                         public mozilla::DataChannelListener
{
public:
  nsDOMDataChannel(mozilla::DataChannel* aDataChannel,
		   nsPIDOMWindow* aDOMWindow)
  {
    mDataChannel = aDataChannel;
    BindToOwner(aDOMWindow);
  }

  nsresult Init();

  NS_DECL_ISUPPORTS_INHERITED

  NS_DECL_NSIDOMDATACHANNEL

  NS_FORWARD_NSIDOMEVENTTARGET(nsDOMEventTargetHelper::)

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsDOMDataChannel,
                                           nsDOMEventTargetHelper)

  void
  DoOnMessageAvailable(const nsACString& message, bool isBinary);

  virtual void
  OnMessageAvailable(nsISupports* aContext, const nsACString& message);

  virtual void
  OnBinaryMessageAvailable(nsISupports* aContext, const nsACString& message);

  virtual void
  OnChannelConnected(nsISupports* aContext);

  virtual void
  OnChannelClosed(nsISupports* aContext);

  nsAutoPtr<mozilla::DataChannel> mDataChannel;

  NS_DECL_EVENT_HANDLER(open)
  NS_DECL_EVENT_HANDLER(error)
  NS_DECL_EVENT_HANDLER(close)
  NS_DECL_EVENT_HANDLER(message)
};

DOMCI_DATA(DataChannel, nsDOMDataChannel)

NS_IMPL_CYCLE_COLLECTION_CLASS(nsDOMDataChannel)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsDOMDataChannel,
                                                  nsDOMEventTargetHelper)
  NS_CYCLE_COLLECTION_TRAVERSE_EVENT_HANDLER(open)
  NS_CYCLE_COLLECTION_TRAVERSE_EVENT_HANDLER(error)
  NS_CYCLE_COLLECTION_TRAVERSE_EVENT_HANDLER(close)
  NS_CYCLE_COLLECTION_TRAVERSE_EVENT_HANDLER(message)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsDOMDataChannel,
                                                nsDOMEventTargetHelper)
  NS_CYCLE_COLLECTION_UNLINK_EVENT_HANDLER(open)
  NS_CYCLE_COLLECTION_UNLINK_EVENT_HANDLER(error)
  NS_CYCLE_COLLECTION_UNLINK_EVENT_HANDLER(close)
  NS_CYCLE_COLLECTION_UNLINK_EVENT_HANDLER(message)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(nsDOMDataChannel, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(nsDOMDataChannel, nsDOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsDOMDataChannel)
  NS_INTERFACE_MAP_ENTRY(nsIDOMDataChannel)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(DataChannel)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

nsresult
nsDOMDataChannel::Init()
{
  nsDOMEventTargetHelper::Init();

  MOZ_ASSERT(mDataChannel);

  mDataChannel->SetListener(this, nsnull);

  return NS_OK;
}

NS_IMPL_EVENT_HANDLER(nsDOMDataChannel, open)
NS_IMPL_EVENT_HANDLER(nsDOMDataChannel, error)
NS_IMPL_EVENT_HANDLER(nsDOMDataChannel, close)
NS_IMPL_EVENT_HANDLER(nsDOMDataChannel, message)

NS_IMETHODIMP
nsDOMDataChannel::GetLabel(nsAString& aLabel)
{
  aLabel = NS_LITERAL_STRING("Help I'm trapped inside a DataChannel factory! ");
  return NS_OK;
}

NS_IMETHODIMP
nsDOMDataChannel::GetReliable(bool* aReliable)
{
  *aReliable = false; // With the amount of time I implemented this in
                      // it certainly isn't reliable!
  return NS_OK;
}

NS_IMETHODIMP
nsDOMDataChannel::GetReadyState(PRUint16* aReadyState)
{
  *aReadyState = mDataChannel->GetReadyState();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMDataChannel::GetBufferedAmount(PRUint32* aBufferedAmount)
{
  *aBufferedAmount = mDataChannel->GetBufferedAmount();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMDataChannel::GetBinaryType(nsAString& aBinaryType)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDOMDataChannel::SetBinaryType(const nsAString& aBinaryType)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDOMDataChannel::Close()
{
  mDataChannel->Close();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMDataChannel::Send(const JS::Value& aValue, JSContext* aCx)
{
  JSObject* obj = JSVAL_TO_OBJECT(aValue);
  if (obj) {
    nsCOMPtr<nsIDOMBlob> blob = do_QueryInterface(
      nsContentUtils::XPConnect()->GetNativeOfWrapper(aCx, obj));
    if (blob) {
      nsCOMPtr<nsIInputStream> stream;
      nsresult rv = blob->GetInternalStream(getter_AddRefs(stream));
      NS_ENSURE_SUCCESS(rv, rv);

      PRUint32 avail = 0;
      rv = stream->Available(&avail);
      NS_ENSURE_SUCCESS(rv, rv);

      // XXXkhuey synchronous disk I/O!  sdwilsh is rolling in his grave
      nsCString string;
      rv = NS_ReadInputStreamToString(stream, string, avail);
      NS_ENSURE_SUCCESS(rv, rv);

      mDataChannel->SendBinaryMsg(string);
      return NS_OK;
    }

    // We might be an array buffer
    if (JS_IsArrayBufferObject(obj, aCx)) {
      PRUint32 len = JS_GetArrayBufferByteLength(obj, aCx);
      char* data = reinterpret_cast<char*>(JS_GetArrayBufferData(obj, aCx));

      nsDependentCString string((char*)data, len);
      mDataChannel->SendBinaryMsg(string);
      return NS_OK;
    }
  }
  // Not an object...
  JSString* str = JS_ValueToString(aCx, aValue);
  NS_ENSURE_TRUE(str, NS_ERROR_FAILURE);

  nsDependentJSString jsstr;
  NS_ENSURE_TRUE(jsstr.init(aCx, str), NS_ERROR_FAILURE);

  NS_ConvertUTF16toUTF8 cstring(jsstr);

  mDataChannel->SendMsg(cstring);
  return NS_OK;
}

void
nsDOMDataChannel::DoOnMessageAvailable(const nsACString& aMsg,
                                       bool isBinary)
{
  // XXX don't need this yet
  if (isBinary) {
    return;
  }

  nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(GetOwner());
  if (!sgo) { return; }

  nsIScriptContext* sc = sgo->GetContext();
  if (!sc) { return; }

  JSContext* cx = sc->GetNativeContext();
  if (!cx) { return; }

  JSAutoRequest ar(cx);

  NS_ConvertUTF8toUTF16 utf16data(aMsg);
  JSString* jsString;
  jsString = JS_NewUCStringCopyN(cx, utf16data.get(), utf16data.Length());
  if (!jsString) { return; }

  jsval data = STRING_TO_JSVAL(jsString);

  nsCOMPtr<nsIDOMEvent> event;
  nsresult rv = NS_NewDOMMessageEvent(getter_AddRefs(event), nsnull, nsnull);
  if (NS_FAILED(rv)) { return; }

  nsCOMPtr<nsIDOMMessageEvent> messageEvent = do_QueryInterface(event);
  rv = messageEvent->InitMessageEvent(NS_LITERAL_STRING("message"),
                                      false, false,
                                      data, EmptyString(), EmptyString(),
                                      nsnull);
  if (NS_FAILED(rv)) { return; }

  //nsCOMPtr<nsIDOMEvent> event = do_QueryInterface(event);
  event->SetTrusted(true);

  DispatchDOMEvent(nsnull, event, nsnull, nsnull);
}

void
nsDOMDataChannel::OnMessageAvailable(nsISupports* aContext,
                                     const nsACString& aMessage)
{
  MOZ_ASSERT(NS_IsMainThread());
  DoOnMessageAvailable(aMessage, false);
}

void
nsDOMDataChannel::OnBinaryMessageAvailable(nsISupports* aContext,
                                           const nsACString& aMessage)
{
  MOZ_ASSERT(NS_IsMainThread());
  DoOnMessageAvailable(aMessage, true);
}

void
nsDOMDataChannel::OnChannelConnected(nsISupports* aContext)
{
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIDOMEvent> event;
  nsresult rv = NS_NewDOMEvent(getter_AddRefs(event), nsnull, nsnull);
  if (NS_FAILED(rv)) { return; }

  rv = event->InitEvent(NS_LITERAL_STRING("open"), false, false);
  if (NS_FAILED(rv)) { return; }

  //nsCOMPtr<nsIDOMEvent> event = do_QueryInterface(event);
  event->SetTrusted(true);

  DispatchDOMEvent(nsnull, event, nsnull, nsnull);
}

void
nsDOMDataChannel::OnChannelClosed(nsISupports* aContext)
{
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIDOMEvent> event;
  nsresult rv = NS_NewDOMEvent(getter_AddRefs(event), nsnull, nsnull);
  if (NS_FAILED(rv)) { return; }

  rv = event->InitEvent(NS_LITERAL_STRING("close"), false, false);
  if (NS_FAILED(rv)) { return; }

  //nsCOMPtr<nsIPrivateDOMEvent> privateEvent = do_QueryInterface(event);
  event->SetTrusted(true);

  DispatchDOMEvent(nsnull, event, nsnull, nsnull);
}

nsresult
NS_NewDOMDataChannel(mozilla::DataChannel* dataChannel,
		     nsPIDOMWindow* aWindow,
                     nsIDOMDataChannel** domDataChannel)
{
  nsRefPtr<nsDOMDataChannel> domdc = new nsDOMDataChannel(dataChannel,
							  aWindow);

  domdc->Init();

  return CallQueryInterface(domdc, domDataChannel);
}
