// SPDX-License-Identifier: MIT
// Own adapter for REFramework_XeFG_PreRetireSwapchainV1, not REF internals.
#include "native_reframework.hpp"
#include <algorithm>
#include <mutex>
#include <vector>
#include "vendor/sha256_util.h"
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <array>
#include <cstring>
namespace mfgunlock::xemfg::native::reframework {
namespace {
std::mutex infoLock;
Info latest;
void Publish(const Info& value) { std::lock_guard lock(infoLock);latest=value; }
std::string Utf8(const std::wstring& text) {
  if(text.empty())return {};
  const auto n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
  std::string result(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),result.data(),n,nullptr,nullptr);return result;
}
std::filesystem::path Path(HMODULE module) {
  std::wstring path(32768,L'\0');const auto n=GetModuleFileNameW(module,path.data(),DWORD(path.size()));
  if(!n || n>=path.size())return {};path.resize(n);return path;
}
std::string Hash(const std::filesystem::path& path) {
  std::ifstream file(path,std::ios::binary);if(!file)return {};
  sha256_util::Hasher h;std::array<char,65536> bytes{};
  while(file) {file.read(bytes.data(),bytes.size());if(file.gcount() && !h.Update(bytes.data(),size_t(file.gcount())))return {};}
  sha256_util::Digest digest{};if(file.bad() || !h.Finish(digest))return {};return sha256_util::LowerHex(digest);
}
std::wstring VersionField(const std::filesystem::path& path,const wchar_t* field) {
  DWORD unused{};const auto count=GetFileVersionInfoSizeW(path.c_str(),&unused);if(!count)return {};
  std::vector<char> data(count);if(!GetFileVersionInfoW(path.c_str(),0,count,data.data()))return {};
  struct Translation {WORD language,codepage;};Translation* languages{};UINT size{};
  if(!VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&languages),&size))return {};
  for(UINT i=0;i<size/sizeof(Translation);++i) {
    wchar_t key[160]{};swprintf_s(key,L"\\StringFileInfo\\%04x%04x\\%s",languages[i].language,languages[i].codepage,field);
    wchar_t* text{};UINT length{};
    if(VerQueryValueW(data.data(),key,reinterpret_cast<void**>(&text),&length) && text && length)return text;
  }return {};
}
// Keep SEH in a leaf with no C++ objects requiring unwinding. A failed or
// unrecognized call never authorizes release. This does not impose a timeout
// on foreign code or try to recover a corrupted process.
bool Invoke(PreRetireV1 fn,IUnknown* proxy,void* context,HWND hwnd,uint32_t* value) noexcept {
  __try {*value=fn(proxy,context,hwnd);return true;}
  __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
Peer::~Peer() {for(auto module:modules)FreeLibrary(module);}
// Builds with the defective XeFG hook monitor come from profiles.json
// (known_bad_reframework_sha256), never from a literal in code.
namespace { std::mutex knownLock; std::vector<std::string> knownMonitorIssues; }
void SetKnownMonitorIssues(std::vector<std::string> sha256) { std::lock_guard lock(knownLock); knownMonitorIssues=std::move(sha256); }
bool KnownMonitorIssue(const std::string& sha) noexcept {
  std::lock_guard lock(knownLock);
  return !sha.empty() && std::find(knownMonitorIssues.begin(),knownMonitorIssues.end(),sha)!=knownMonitorIssues.end();
}
bool Safe(Result r) noexcept {return r==Result::Legacy || r==Result::SafeNotTracked || r==Result::SafeDetached;}
Info Snapshot() {std::lock_guard lock(infoLock);return latest;}
std::shared_ptr<Peer> Resolve(const std::shared_ptr<Peer>& previous,EnumerateModules enumerate) {
  auto peer=std::make_shared<Peer>();
  std::vector<HMODULE> mapped(256);DWORD needed{};
  bool enumerated=false;
  if(!enumerate)enumerate=&EnumProcessModules;
  for(unsigned attempt=0;attempt<3;++attempt) {
    if(!enumerate(GetCurrentProcess(),mapped.data(),DWORD(mapped.size()*sizeof(HMODULE)),&needed))break;
    if(needed<=mapped.size()*sizeof(HMODULE)) {mapped.resize(needed/sizeof(HMODULE));enumerated=true;break;}
    mapped.resize(needed/sizeof(HMODULE)+16);
  }
  if(!enumerated) {
    // A failed re-probe must not drop the old module lease while its proxy
    // and hooks still exist. Quarantine keeps this exact peer alive.
    if(previous)peer=previous;
    peer->info.result=Result::DiscoveryFailed;Publish(peer->info);return peer;
  }
  for(auto mappedModule:mapped) {
    // Acquire before inspection; a concurrently unloading module is skipped.
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(mappedModule),&module))continue;
    auto callback=reinterpret_cast<PreRetireV1>(GetProcAddress(module,"REFramework_XeFG_PreRetireSwapchainV1"));
    const auto path=Path(module);const auto name=path.filename().wstring();
    const bool named=_wcsicmp(name.c_str(),L"dinput8.dll")==0 || name.starts_with(L"reframework") || name.starts_with(L"REFramework");
    Info info;
    if(previous && previous->modules.size()==1 && previous->modules[0]==module)info=previous->info;
    else if(callback || named) {
      info.path=Utf8(path.wstring());info.sha256=Hash(path);info.knownMonitorIssue=KnownMonitorIssue(info.sha256);
      info.present=callback || info.knownMonitorIssue || VersionField(path,L"ProductName")==L"REFramework";
      info.version=Utf8(VersionField(path,L"FileVersion"));
    }
    if(!info.present) {FreeLibrary(module);continue;}
    // Reject a forwarded/mismatched export; do not call into an unidentified DLL.
    bool valid=true;
    if(callback) {
      HMODULE codeOwner{};
      valid=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(callback),&codeOwner)!=FALSE;
      if(valid) {valid=codeOwner==module;FreeLibrary(codeOwner);}
    }
    peer->modules.push_back(module);
    if(peer->modules.size()==1) {
      peer->info=info;peer->info.v1=callback && valid;peer->info.rawResult=~0u;
      peer->info.result=valid?Result::Legacy:Result::DiscoveryFailed;peer->callback=valid?callback:nullptr;
    } else {peer->info.result=Result::Ambiguous;peer->callback=nullptr;}
  }
  Publish(peer->info);return peer;
}
Result Prepare(const std::shared_ptr<Peer>& peer,IUnknown* proxy,void* context,HWND hwnd) noexcept {
  if(!peer)return Result::DiscoveryFailed;
  auto& info=peer->info;
  if(info.result==Result::DiscoveryFailed || info.result==Result::Ambiguous)return info.result;
  if(!peer->callback) {info.result=Result::Legacy;Publish(info);return info.result;}
  if(!proxy || !context) {info.result=Result::Blocked;Publish(info);return info.result;}
  uint32_t raw=~0u;
  const bool returned=Invoke(peer->callback,proxy,context,hwnd,&raw);
  info.rawResult=raw;
  info.result=!returned?Result::Exception:raw==0?Result::SafeNotTracked:raw==1?Result::SafeDetached:raw==2?Result::Blocked:Result::InvalidResult;
  Publish(info);return info.result;
}
const char* Description(const Info& info) noexcept {
  if(!Safe(info.result))return "Output retirement blocked - restart required";
  if(!info.present)return "Not detected";
  if(info.knownMonitorIssue)return "Known XeFG hook-monitor conflict - update recommended";
  if(info.v1)return "Cooperative swapchain retirement available";
  return "Legacy integration - compatibility unverified";
}
}
