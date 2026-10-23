// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>
namespace mfgunlock::config {
struct Entry { const wchar_t* section; const wchar_t* key; const wchar_t* value; };
inline std::mutex writeLock;
inline bool Bytes(const std::filesystem::path& path,std::vector<char>& value) {
  std::ifstream in(path,std::ios::binary);
  if(!in) return false;
  value.assign(std::istreambuf_iterator<char>(in),{}); return !in.bad();
}
// Stage all keys together. A failed save never publishes a partially updated
// live preference. Do not modify unrelated sections, comments or game files.
inline bool Save(const std::filesystem::path& path,std::initializer_list<Entry> entries) {
  std::lock_guard guard(writeLock);
  if(path.empty() || !path.is_absolute()) return false;
  const auto temp=path.wstring()+L".mfg-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetCurrentThreadId())+L".tmp";
  const auto file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE) return false;
  CloseHandle(file);
  struct Cleanup { const wchar_t* path; ~Cleanup(){SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL);DeleteFileW(path);} } cleanup{temp.c_str()};
  const bool existed=GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;
  std::vector<char> before,now;
  if(existed && (!Bytes(path,before) || !CopyFileW(path.c_str(),temp.c_str(),FALSE))) return false;
  for(const auto& e:entries) if(!WritePrivateProfileStringW(e.section,e.key,e.value,temp.c_str())) return false;
  WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
  const auto staged=CreateFileW(temp.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  if(staged==INVALID_HANDLE_VALUE) return false;
  const bool flushed=FlushFileBuffers(staged)!=FALSE; CloseHandle(staged);
  if(!flushed) return false;
  if(existed ? (!Bytes(path,now) || before!=now) : GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES) return false;
  const bool result=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
  if(result) WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
  return result;
}
}
