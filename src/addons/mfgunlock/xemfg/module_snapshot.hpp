// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <stdexcept>
namespace mfgunlock::xemfg::module_snapshot {
// An EnumProcessModules snapshot does not keep its images mapped. Borrow a
// loader reference before inspecting a candidate; do not pin unrelated DLLs.
inline bool IsOptiScaler10(HMODULE candidate) {
  if(!candidate)return false;
  struct Reference {
    HMODULE module{};
    ~Reference() {if(module)FreeLibrary(module);}
  } reference;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
      reinterpret_cast<LPCWSTR>(candidate),&reference.module) || reference.module!=candidate)return false;
  std::wstring path(32768,L'\0');
  const auto count=GetModuleFileNameW(reference.module,path.data(),DWORD(path.size()));
  // An unavailable path of a still-live image is a genuine discovery failure.
  if(!count || count>=path.size())throw std::runtime_error("module_path");
  path.resize(count);DWORD unused{};
  const auto bytes=GetFileVersionInfoSizeW(path.c_str(),&unused);
  if(!bytes || bytes>1'000'000)return false;
  std::vector<unsigned char> info(bytes);
  if(!GetFileVersionInfoW(path.c_str(),0,bytes,info.data()))return false;
  VS_FIXEDFILEINFO* fixed{};UINT length{};
  if(!VerQueryValueW(info.data(),L"\\",reinterpret_cast<void**>(&fixed),&length)
      || length<sizeof(*fixed) || HIWORD(fixed->dwFileVersionMS)!=10)return false;
  struct Translation {WORD language,codepage;}* translations{};
  if(!VerQueryValueW(info.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&translations),&length))return false;
  for(unsigned i=0;i<length/sizeof(Translation);++i) {
    wchar_t key[96]{};swprintf_s(key,L"\\StringFileInfo\\%04x%04x\\ProductName",translations[i].language,translations[i].codepage);
    wchar_t* product{};UINT size{};
    if(VerQueryValueW(info.data(),key,reinterpret_cast<void**>(&product),&size)
        && size && wcscmp(product,L"OptiScaler")==0)return true;
  }
  return false;
}
}
