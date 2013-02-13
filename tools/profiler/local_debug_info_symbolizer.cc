#include "local_debug_info_symbolizer.h"

#include "common/linux/dump_symbols.h"
#include "common/module.h"
#include "google_breakpad/processor/code_module.h"
#include "google_breakpad/processor/code_modules.h"
#include "google_breakpad/processor/stack_frame.h"
#include "processor/cfi_frame_info.h"
#include "processor/logging.h"
#include "common/scoped_ptr.h"

namespace google_breakpad {

LocalDebugInfoSymbolizer::~LocalDebugInfoSymbolizer() {
  for (SymbolMap::iterator it = symbols_.begin();
       it != symbols_.end();
       ++it) {
    delete it->second;
  }
}

StackFrameSymbolizer::SymbolizerResult
LocalDebugInfoSymbolizer::FillSourceLineInfo(const CodeModules* modules,
                                             const SystemInfo* system_info,
                                             StackFrame* frame) {
  if (!modules) {
    return ERROR;
  }
  const CodeModule* module = modules->GetModuleForAddress(frame->instruction);
  if (!module) {
    return ERROR;
  }
  frame->module = module;

  Module* debug_info_module = NULL;
  SymbolMap::const_iterator it = symbols_.find(module->code_file());
  if (it == symbols_.end()) {
    if (no_symbol_modules_.find(module->code_file()) !=
        no_symbol_modules_.end()) {
      return NO_ERROR;
    }
    if (!ReadSymbolData(module->code_file(),
                        debug_dirs_,
                        ONLY_CFI,
                        &debug_info_module)) {
      BPLOG(ERROR) << "ReadSymbolData failed for " << module->code_file();
      if (debug_info_module)
        delete debug_info_module;
      no_symbol_modules_.insert(module->code_file());
      return NO_ERROR;
    }

    symbols_[module->code_file()] = debug_info_module;
  } else {
    debug_info_module = it->second;
  }

  u_int64_t address = frame->instruction - frame->module->base_address();
  Module::Function* function =
      debug_info_module->FindFunctionByAddress(address);
  if (function) {
    frame->function_name = function->name;
    //TODO: line info: function->lines
  } else {
    Module::Extern* ex = debug_info_module->FindExternByAddress(address);
    if (ex) {
      frame->function_name = ex->name;
    }
  }

  return NO_ERROR;
}


WindowsFrameInfo* LocalDebugInfoSymbolizer::FindWindowsFrameInfo(
    const StackFrame* frame) {
  // Not currently implemented, would require PDBSourceLineWriter to
  // implement an API to return symbol data.
  return NULL;
}

// Taken wholesale from source_line_resolver_base.cc
bool ParseCFIRuleSet(const string& rule_set, CFIFrameInfo* frame_info) {
  CFIFrameInfoParseHandler handler(frame_info);
  CFIRuleParser parser(&handler);
  return parser.Parse(rule_set);
}

static void ConvertCFI(const UniqueString* name, const Module::Expr& rule,
                       CFIFrameInfo* frame_info) {
  if (name == ustr__ZDcfa()) frame_info->SetCFARule(rule);
  else if (name == ustr__ZDra()) frame_info->SetRARule(rule);
  else frame_info->SetRegisterRule(name, rule);
}


static void ConvertCFI(const Module::RuleMap& rule_map,
                       CFIFrameInfo* frame_info) {
  for (Module::RuleMap::const_iterator it = rule_map.begin();
       it != rule_map.end(); ++it) {
    ConvertCFI(it->first, it->second, frame_info);
  }
}

CFIFrameInfo* LocalDebugInfoSymbolizer::FindCFIFrameInfo(
    const StackFrame* frame) {
  if (!frame || !frame->module) return NULL;

  SymbolMap::const_iterator it = symbols_.find(frame->module->code_file());
  if (it == symbols_.end()) return NULL;

  Module* module = it->second;
  u_int64_t address = frame->instruction - frame->module->base_address();
  Module::StackFrameEntry* entry =
      module->FindStackFrameEntryByAddress(address);
  if (!entry)
    return NULL;

  //TODO: can we cache this data per-address? does that make sense?
  scoped_ptr<CFIFrameInfo> rules(new CFIFrameInfo());
  ConvertCFI(entry->initial_rules, rules.get());
  for (Module::RuleChangeMap::const_iterator delta_it =
           entry->rule_changes.begin();
       delta_it != entry->rule_changes.end() && delta_it->first < address;
       ++delta_it) {
    ConvertCFI(delta_it->second, rules.get());
  }
  return rules.release();
}

}  // namespace google_breakpad
