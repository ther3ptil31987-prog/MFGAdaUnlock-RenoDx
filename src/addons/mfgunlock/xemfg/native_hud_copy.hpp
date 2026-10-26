// SPDX-License-Identifier: MIT
#pragma once
#include "native_dejitter.hpp"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace mfgunlock::xemfg::native {
// HUD-less and UI inputs whose format differs from the output's only in type
// (CopyableHudFormat, 1.5.7) are tagged through an owned copy in the output
// format. Volatile inputs are copied on the producer's list at the tag; inputs
// valid until Present are copied on this class's own list on the presentation
// queue at the source Present. Copies only: no descriptor heap or pipeline
// state is set on the game's list.
class HudCopy {
 public:
  static constexpr uint32_t kSlots=3;
  bool Initialize(ID3D12Device* device);
  // Records the source region into an owned texture (output format, region
  // size) on `list`, restoring the source state. The copy is left in COMMON.
  // kind: 0 HUD-less, 1 UI. Returns nullptr on failure.
  ID3D12Resource* Record(ID3D12GraphicsCommandList* list,ID3D12Resource* source,D3D12_RESOURCE_STATES state,
      const DejitterRegion& region,DXGI_FORMAT format,unsigned kind,uint32_t frame);
  // An owned texture for a conversion target (kind, frame slot), created in
  // COMMON with the given flags. Returns nullptr on failure.
  ID3D12Resource* Target(unsigned kind,uint32_t frame,DXGI_FORMAT format,uint32_t width,uint32_t height,D3D12_RESOURCE_FLAGS flags);
  // Opens the own list; tags may be recorded on it before Submit.
  ID3D12GraphicsCommandList* Begin();
  bool Submit(ID3D12CommandQueue* queue);
  // Waits for every submitted copy (retirement).
  bool Drain(uint32_t timeoutMs);
  HRESULT LastError() const { return error; }
  ~HudCopy();
 private:
  bool Fail(HRESULT hr) { error=hr; return false; }
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocators[kSlots];
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  Microsoft::WRL::ComPtr<ID3D12Resource> textures[2][kSlots], targets[2][kSlots];
  bool Ensure(Microsoft::WRL::ComPtr<ID3D12Resource>& texture,DXGI_FORMAT format,uint32_t width,uint32_t height,D3D12_RESOURCE_FLAGS flags);
  HANDLE event{};
  uint64_t fenceValues[kSlots]{}, nextFence{1};
  uint32_t slot{};
  bool open{};
  HRESULT error{S_OK};
};
}
