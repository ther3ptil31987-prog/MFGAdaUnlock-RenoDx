// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/module_snapshot.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv) {
  if(argc!=2)return 2;
  using mfgunlock::xemfg::module_snapshot::IsOptiScaler10;
  try {
    if(IsOptiScaler10(nullptr) || IsOptiScaler10(GetModuleHandleW(nullptr)))return 3;
    // The fixture has only our synthetic version metadata; its entrypoint is
    // not run and no SDK or game process is used by this regression.
    HMODULE snapshot=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!snapshot || !IsOptiScaler10(snapshot))return 4;
    if(!FreeLibrary(snapshot))return 5;
    wchar_t path[32768]{};
    if(GetModuleFileNameW(snapshot,path,32768))return 6; // no permanent pin/leak
    // The old unleased ModulePath(snapshot) threw here and aborted Start().
    if(IsOptiScaler10(snapshot))return 7;
    std::cout<<"PASS: live module identified; inspection released its lease; stale inventory entry skipped without aborting discovery\n";
    return 0;
  }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
