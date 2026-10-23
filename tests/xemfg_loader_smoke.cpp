// Startup regression: this executable imports Intel before main. Intel imports
// dxgi.dll, which is the real OptiScaler in the isolated test directory. Its
// InitializeASI callback therefore runs before Intel's own DLL initialization.
#include <windows.h>
#include <xefg_swapchain.h>
#include "../src/addons/mfgunlock/xemfg/backend_api.hpp"
#include <iostream>
int main() {
  xefg_swapchain_version_t version{};
  if (xefgSwapChainGetVersion(&version)!=XEFG_SWAPCHAIN_RESULT_SUCCESS) return 2;
  const auto plugin=GetModuleHandleW(L"MFGAdaUnlock.XeMFG.asi");
  if (!plugin) { std::cerr<<"OptiScaler did not load the companion\n"; return 3; }
  auto get=reinterpret_cast<mfgunlock::xemfg::GetStatus>(GetProcAddress(plugin,"MFGAXeGetStatus"));
  if (!get) return 4;
  mfgunlock::xemfg::Status s;
  const auto until=GetTickCount64()+15000;
  do {
    if (!get(&s,sizeof(s))) return 5;
    if (s.hooks) {
      std::cout<<"PASS: real OptiScaler dependency initialization completed; SDK hooks ready after loader exit; "<<s.detail<<'\n';
      return 0;
    }
    Sleep(5);
  } while(GetTickCount64()<until);
  std::cerr<<"Startup did not complete: "<<s.detail<<'\n';
  return 6;
}
