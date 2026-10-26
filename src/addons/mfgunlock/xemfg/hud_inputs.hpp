// SPDX-License-Identifier: MIT
#pragma once
#include <d3d12.h>
#include <cstdint>
namespace mfgunlock::xemfg::native {
enum HudIssue:uint32_t { HudMetadata=1,HudFormat=2,HudExtent=4,HudUnreadable=8,HudConflict=16 };
struct HudTagIdentity {
  uintptr_t allocation{};
  uint32_t state{},lifecycle{},x{},y{},width{},height{};
  bool operator==(const HudTagIdentity&) const=default;
};
// 1.5.7: XeFG requires HUD-less and UI inputs in the output's exact format.
// A TYPELESS or _SRGB view of the same bits (Cyberpunk 2077 tags HUD-less as
// R8G8B8A8_TYPELESS, Death Stranding 2 its UI as R8G8B8A8_UNORM_SRGB over an
// R8G8B8A8_UNORM output) holds the same encoded values: a CopyTextureRegion
// into a texture in the output format reads them back unchanged.
inline bool CopyableHudFormat(DXGI_FORMAT input,DXGI_FORMAT output) {
  const auto family=[](DXGI_FORMAT f) {
    switch(f) {
      case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 1;
      case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return 2;
      case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: return 3;
      default: return 0;
    }
  };
  return input!=output && family(input) && family(input)==family(output);
}
inline bool ConflictingHudTag(const HudTagIdentity& previous,const HudTagIdentity& next) {
  return previous.allocation && !(previous==next);
}
// Structural checks only. Identical formats do not prove identical pixel
// encoding. Intel permits R10G10B10A2 HDR inputs; do not copy NVIDIA's separate
// two-bit UI-alpha prohibition into Intel's interpolation path.
inline uint32_t CheckHudInput(const D3D12_RESOURCE_DESC& d,DXGI_FORMAT output,
    uint32_t width,uint32_t height,uint32_t x,uint32_t y,uint32_t w,uint32_t h) {
  uint32_t issues{};
  if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.SampleDesc.Count!=1 || d.DepthOrArraySize!=1) issues|=HudMetadata;
  if(output!=DXGI_FORMAT_UNKNOWN && d.Format!=output) issues|=HudFormat;
  if(!w || !h || uint64_t(x)+w>d.Width || uint64_t(y)+h>d.Height
      || (width && w!=width) || (height && h!=height)) issues|=HudExtent;
  if(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) issues|=HudUnreadable;
  return issues;
}
}
