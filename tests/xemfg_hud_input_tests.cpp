#include "../src/addons/mfgunlock/xemfg/hud_inputs.hpp"
#include <iostream>
#include <stdexcept>
using namespace mfgunlock::xemfg::native;
void Check(bool value,const char* reason) {if(!value) throw std::runtime_error(reason);}
int main() {try {
  D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=2560;d.Height=1440;
  d.DepthOrArraySize=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R10G10B10A2_UNORM;
  const auto assess=[&](uint32_t w=2560,uint32_t h=1440,uint32_t x=0) {return CheckHudInput(d,DXGI_FORMAT_R10G10B10A2_UNORM,2560,1440,x,0,w,h);};
  Check(!assess(),"Intel HDR two-bit alpha was incorrectly treated as NVIDIA's prohibited UI format");
  d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; Check(assess()&HudFormat,"mismatched HDR input accepted");d.Format=DXGI_FORMAT_R10G10B10A2_UNORM;
  Check(assess(1920,1080)&HudExtent,"lower resolution HUD accepted as full output");
  Check(assess(2560,1440,UINT32_MAX)&HudExtent,"extent overflow accepted");
  Check(assess(0,1440)&HudExtent,"empty extent accepted");
  d.Flags=D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;Check(assess()&HudUnreadable,"non-readable HUD accepted");d.Flags=D3D12_RESOURCE_FLAG_NONE;
  d.SampleDesc.Count=4;Check(assess()&HudMetadata,"multisample UI accepted");d.SampleDesc.Count=1;
  d.DepthOrArraySize=2;Check(assess()&HudMetadata,"UI array accepted");
  // 1.5.7: same bits under another type are copied into the output format.
  Check(CopyableHudFormat(DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_R8G8B8A8_UNORM),"Cyberpunk TYPELESS HUD-less not copyable");
  Check(CopyableHudFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM),"Death Stranding 2 sRGB UI not copyable");
  Check(CopyableHudFormat(DXGI_FORMAT_B8G8R8A8_TYPELESS,DXGI_FORMAT_B8G8R8A8_UNORM),"BGRA TYPELESS not copyable");
  Check(CopyableHudFormat(DXGI_FORMAT_R10G10B10A2_TYPELESS,DXGI_FORMAT_R10G10B10A2_UNORM),"RGB10A2 TYPELESS not copyable");
  Check(!CopyableHudFormat(DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM),"identical format needs no copy");
  Check(!CopyableHudFormat(DXGI_FORMAT_B8G8R8A8_TYPELESS,DXGI_FORMAT_R10G10B10A2_UNORM),"Mafia BGRA UI over RGB10A2 is a conversion, not a copy");
  Check(!CopyableHudFormat(DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM),"channel order differs: not a copy");
  Check(!CopyableHudFormat(DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R10G10B10A2_UNORM),"FP16 is a conversion, not a copy");
  Check(!CopyableHudFormat(DXGI_FORMAT_R8G8B8A8_UINT,DXGI_FORMAT_R8G8B8A8_UNORM),"integer view is not color");
  const HudTagIdentity a{0x1000,4,1,0,0,2560,1440};
  auto b=a; Check(!ConflictingHudTag({},a) && !ConflictingHudTag(a,b),"first/identical tag rejected");
  b.allocation=0x2000; Check(ConflictingHudTag(a,b),"allocation conflict admitted");
  b=a; b.lifecycle=0; Check(ConflictingHudTag(a,b),"lifecycle conflict admitted");
  b=a; b.x=1; Check(ConflictingHudTag(a,b),"extent conflict admitted");
  b=a; b.state=8; Check(ConflictingHudTag(a,b),"state conflict admitted");
  std::cout<<"PASS: Intel-specific HUD format, extent, alpha precision and shader-read checks\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
