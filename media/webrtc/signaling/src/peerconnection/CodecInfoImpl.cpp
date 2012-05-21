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

#include "CodecInfoImpl.h"

namespace sipcc
{

CodecListImpl::CodecListImpl()
{
  mLockAudioCodecs = PR_NewLock();
  mLockVideoCodecs = PR_NewLock();
}

CodecListImpl::~CodecListImpl()
{
  std::map<std::string, CodecInfo*>::iterator it;
  // Destroy and free the audio list
  PR_Lock(mLockAudioCodecs);
  DestroyCodecList(mAudioCodecs);  
  PR_Unlock(mLockAudioCodecs);
  PR_DestroyLock(mLockAudioCodecs);

  // Destroy and free the video list
  PR_Lock(mLockVideoCodecs);
  DestroyCodecList(mVideoCodecs);  
  PR_Unlock(mLockVideoCodecs);
  PR_DestroyLock(mLockVideoCodecs);
}

void CodecListImpl::DestroyCodecList(std::map<std::string, CodecInfo*>& codecMap)
{
  std::map<std::string, CodecInfo*>::iterator it;

  while ((it = codecMap.begin()) != codecMap.end())
  {
    delete it->second;
    codecMap.erase(it);
  }
}

void CodecListImpl::AddCodec(std::map<std::string, CodecInfo*>& codecMap, CodecInfo *pCodec)
{
  std::map<std::string, CodecInfo*>::iterator it;

  if ((it = codecMap.find(pCodec->mName)) != codecMap.end())
  {
    // Codec with this name already listed, remove and add new one.
    delete it->second;
    codecMap.erase(it);
  }

  codecMap.insert(std::pair<std::string, CodecInfo*>(pCodec->mName, pCodec));
}

void CodecListImpl::AddAudioCodec(CodecInfo *pCodec)
{
  PR_Lock(mLockAudioCodecs);
  AddCodec(mAudioCodecs, pCodec);
  PR_Unlock(mLockAudioCodecs);
}

void CodecListImpl::AddVideoCodec(CodecInfo *pCodec)
{
  PR_Lock(mLockVideoCodecs);
  AddCodec(mVideoCodecs, pCodec);
  PR_Unlock(mLockVideoCodecs);
}

std::map<std::string, CodecInfo*>* CodecListImpl::GetAndLockAudioCodecs()
{
  PR_Lock(mLockAudioCodecs);
  return &mAudioCodecs;
}

void CodecListImpl::UnlockAudioCodecs()
{
  PR_Unlock(mLockAudioCodecs);
}

std::map<std::string, CodecInfo*>* CodecListImpl::GetAndLockVideoCodecs()
{
  PR_Lock(mLockVideoCodecs);
  return &mVideoCodecs;
}

void CodecListImpl::UnlockVideoCodecs()
{
  PR_Unlock(mLockVideoCodecs);
}

}
