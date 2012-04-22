/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#ifndef logging_h__
#define logging_h__

#include <sstream>

#include <prlog.h>


class LogCtx {
 public:
  //  LogCtx(const char* name) : module_(PR_NewLogModule(name)) {}
  LogCtx(const char* name) : module_(NULL) {}
  LogCtx(std::string& name) : module_(PR_NewLogModule(name.c_str())) {}

  PRLogModuleInfo* module() const { return module_; }
  
private:
  PRLogModuleInfo* module_;
};


#define MLOG_INIT(n) \
  static LogCtx mlog_ctx(n)

#define MLOG(level, b) \
  do { if (mlog_ctx.module()) {                            \
          std::stringstream str;                           \
          str << b;                                                     \
          PR_LOG(mlog_ctx.module(), level, ("%s", str.str().c_str())); }} while(0)
#endif
