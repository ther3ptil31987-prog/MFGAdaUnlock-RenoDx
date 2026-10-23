// SPDX-License-Identifier: MIT
#include "native_internal.hpp"
#include "native_profiles.hpp"
#include "../backend_selection.hpp"
#include <fstream>
#include <mutex>
#include <sstream>
namespace mfgunlock::xemfg::native {
namespace {
std::once_flag once;
profiles::Rules rules;
std::string overrideError;
bool overridePresent{};
std::string EmbeddedText() {
  HMODULE self{};
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&EmbeddedText),&self);
  const auto resource=FindResourceW(self,MAKEINTRESOURCEW(9012),RT_RCDATA);
  if(!resource) return {};
  const auto size=SizeofResource(self,resource); const auto bytes=LockResource(LoadResource(self,resource));
  return bytes && size?std::string(static_cast<const char*>(bytes),size):std::string{};
}
void Load() {
  std::string error;
  profiles::Rules embedded;
  if(!profiles::Parse(EmbeddedText(),"embedded",embedded,error)) embedded.clear();
  rules=std::move(embedded);
  const auto path=backend::Directory()/L"MFGAdaUnlock.profiles.json";
  std::ifstream file(path,std::ios::binary);
  if(!file) return;
  overridePresent=true;
  std::stringstream text; text<<file.rdbuf();
  profiles::Rules local;
  if(profiles::Parse(text.str(),"override",local,overrideError)) rules.insert(rules.end(),local.begin(),local.end());
}
std::wstring Executable() {
  wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,DWORD(std::size(path)));
  return std::filesystem::path(path).filename().wstring();
}
}
const profiles::Rules& ProfileRules() { std::call_once(once,Load); return rules; }
profiles::Policy CurrentPolicy() {
  const auto& all=ProfileRules();
  profiles::Context context{Executable(),{}};
  // Only modules named by a rule are checked; this stays cheap off the hot path.
  for(const auto& rule:all) if(!rule.module.empty() && GetModuleHandleW(rule.module.c_str())) context.modules.push_back(rule.module);
  return profiles::Resolve(all,context);
}
void LogProfiles() noexcept {
  try {
    const auto policy=CurrentPolicy();
    std::string applied;
    for(const auto& id:policy.applied) applied+=std::string(applied.empty()?"":",")+"\""+escape(id)+"\"";
    xess_mod::WriteLog("native_profiles","\"rules\":"+std::to_string(ProfileRules().size())+",\"applied\":["+applied
        +"],\"override\":"+(overridePresent?"true":"false")+",\"override_error\":\""+escape(overrideError)+"\"");
  } catch(...) {}
}
}
