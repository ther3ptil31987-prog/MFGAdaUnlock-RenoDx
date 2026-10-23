#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <string_view>
#include "config_store.hpp"

namespace mfgunlock::backend {
enum class Kind { DlssG, XeMfg, Invalid };
inline Kind Parse(std::wstring_view value) {
  if (value == L"DLSSG") return Kind::DlssG;
  if (value == L"XeMFG") return Kind::XeMfg;
  return Kind::Invalid;
}
inline std::filesystem::path Directory() {
  std::wstring path(32768, L'\0');
  const auto n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
  if (!n || n >= path.size()) return {};
  path.resize(n);
  return std::filesystem::path(path).parent_path();
}
inline Kind Read() {
  const auto dir = Directory();
  if (dir.empty()) return Kind::Invalid;
  wchar_t value[32]{};
  GetPrivateProfileStringW(L"Backend", L"Selected", L"DLSSG", value, 32,
                          (dir/L"MFGAdaUnlock.ini").c_str());
  return Parse(value);
}
inline bool Select(Kind value) {
  if ((value != Kind::DlssG && value != Kind::XeMfg) || Directory().empty()) return false;
  return config::Save(Directory()/L"MFGAdaUnlock.ini",{{L"Backend",L"Selected",value==Kind::XeMfg?L"XeMFG":L"DLSSG"}});
}
// Latched at process startup. A pending setting never changes a live owner.
inline Kind active = Kind::Invalid;
}
