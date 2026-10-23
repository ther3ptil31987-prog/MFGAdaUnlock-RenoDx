// SPDX-License-Identifier: MIT
// Optional, versioned cooperation with a loaded REFramework. No foreign patches.
#pragma once
#include <windows.h>
#include <unknwn.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace mfgunlock::xemfg::native::reframework {
using PreRetireV1 = uint32_t(WINAPI*)(IUnknown*,void*,HWND);
enum class Result { Legacy, SafeNotTracked, SafeDetached, Blocked, InvalidResult, Exception, DiscoveryFailed, Ambiguous };
struct Info {
  bool present{},v1{},knownMonitorIssue{};
  Result result{Result::Legacy};
  uint32_t rawResult{~0u};
  std::string path,sha256,version;
};
struct Peer {
  // Ref-counted module leases last through retirement/quarantine. Never PIN a
  // foreign module permanently, load a new REFramework, or retain a bare proc.
  std::vector<HMODULE> modules;
  PreRetireV1 callback{};
  Info info;
  ~Peer();
};
using EnumerateModules = BOOL(WINAPI*)(HANDLE,HMODULE*,DWORD,LPDWORD);
std::shared_ptr<Peer> Resolve(const std::shared_ptr<Peer>& previous={},EnumerateModules enumerate=nullptr);
Result Prepare(const std::shared_ptr<Peer>&,IUnknown* publicProxy,void* context,HWND) noexcept;
bool Safe(Result) noexcept;
bool KnownMonitorIssue(const std::string& sha256) noexcept;
void SetKnownMonitorIssues(std::vector<std::string> sha256);
Info Snapshot(); // cached; no enumeration, file I/O, or foreign calls in the UI
const char* Description(const Info&) noexcept;
}
