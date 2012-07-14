/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <MediaEngineWrapper.h>

namespace mozilla
{

WebRTCEngineWrapper* WebRTCEngineWrapper::_instance = NULL;

/**
 * Singleton for the wrapper class.
 * TODO:crypt: Make it thread-safe.
 */
WebRTCEngineWrapper* WebRTCEngineWrapper::Instance()
{
  if(_instance)
  {
     return _instance;
  } else {
    _instance = new WebRTCEngineWrapper();
    return _instance;
  }
}


}
