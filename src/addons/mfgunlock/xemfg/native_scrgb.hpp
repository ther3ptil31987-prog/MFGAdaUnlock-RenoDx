// SPDX-License-Identifier: MIT
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace mfgunlock::xemfg::native {
// Plan 4.4: scRGB (FP16) game buffers encoded to HDR10 PQ for XeFG, which
// supports HDR only as R10G10B10A2 in BT.2100. Recorded on an own list with
// own descriptor heaps and submitted on the presentation queue.
class ScrgbEncoder {
 public:
  static constexpr uint32_t kSlots=3;
  static constexpr DXGI_FORMAT kTargetFormat=DXGI_FORMAT_R10G10B10A2_UNORM;
  bool Initialize(ID3D12Device* device);
  // Encodes source (scRGB) into target (same size, kTargetFormat); both are
  // returned to the given states. Submits on queue.
  bool Encode(ID3D12CommandQueue* queue,ID3D12Resource* source,D3D12_RESOURCE_STATES sourceState,
      ID3D12Resource* target,D3D12_RESOURCE_STATES targetState);
  bool Drain(uint32_t timeoutMs);
  HRESULT LastError() const { return error; }
  ~ScrgbEncoder();
 private:
  bool Fail(HRESULT hr) { error=hr; return false; }
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> views, targets;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocators[kSlots];
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  HANDLE event{};
  uint64_t fenceValues[kSlots]{}, nextFence{1};
  uint32_t slot{}, viewSize{}, targetSize{};
  HRESULT error{S_OK};
};
}
