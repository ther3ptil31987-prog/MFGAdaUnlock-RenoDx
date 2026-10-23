// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/config_store.hpp"
#include <iostream>
#include <stdexcept>
using namespace mfgunlock;
void Check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv) { try {
  Check(argc==2,"test directory required");
  const auto root=std::filesystem::absolute(argv[1])/std::to_string(GetCurrentProcessId());
  std::filesystem::create_directories(root); const auto path=root/L"config.ini";
  Check(config::Save(path,{{L"Backend",L"Selected",L"XeMFG"},{L"Untouched",L"Key",L"preserved"}}),"first atomic save");
  Check(config::Save(path,{{L"Backend",L"Selected",L"DLSSG"}}),"second atomic save");
  wchar_t text[64]{}; GetPrivateProfileStringW(L"Untouched",L"Key",L"",text,64,path.c_str());
  Check(std::wstring(text)==L"preserved","unrelated section lost");
  std::vector<char> before,after; Check(config::Bytes(path,before),"read before");
  HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  Check(locked!=INVALID_HANDLE_VALUE,"lock fixture");
  Check(!config::Save(path,{{L"Backend",L"Selected",L"XeMFG"}}),"locked destination replaced");
  CloseHandle(locked); Check(config::Bytes(path,after) && before==after,"failed transaction changed original");
  Check(!config::Save(root/L"missing"/L"config.ini",{{L"a",L"b",L"c"}}),"missing directory save succeeded");
  Check(!config::Save(L"relative.ini",{{L"a",L"b",L"c"}}),"relative write accepted");
  for(const auto& p:std::filesystem::directory_iterator(root)) Check(p.path()==path,"temporary staging leaked");
  std::cout<<"PASS: atomic multi-key config, unrelated settings, locked/missing destination, cleanup\n";
  return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
