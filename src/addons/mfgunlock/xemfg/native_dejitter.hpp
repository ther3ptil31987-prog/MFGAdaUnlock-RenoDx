// SPDX-License-Identifier: MIT
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace mfgunlock::xemfg::native {
// Jittered motion vectors (plan 4.2). A static point rendered with pixel
// jitter j moves by j_prev - j_cur between frames; the vector points from the
// current sample to the previous one and buffer units are pixels divided by
// mvecScale * size (Streamline sl_consts.h, the same cancellation as
// FidelityFX FSR2's JITTERED_MOTION_VECTORS option). Returns false for a
// scale that cannot be inverted.
struct DejitterVector { float x,y; };
inline bool DejitterDelta(DejitterVector previous,DejitterVector current,DejitterVector mvecScale,
    uint32_t width,uint32_t height,DejitterVector& delta) {
  const float sx=mvecScale.x*float(width),sy=mvecScale.y*float(height);
  if(!(sx<0 || sx>0) || !(sy<0 || sy>0)) return false;
  delta={(previous.x-current.x)/sx,(previous.y-current.y)/sy};
  return true;
}
struct DejitterRegion { uint32_t x{},y{},width{},height{}; };

// Converts one motion texture per frame into an owned, unjittered texture.
// The conversion is recorded on its own list (own descriptor heap, so the
// game's bound heaps are never touched) and runs on the presentation queue
// before Present. Volatile tags are first copied on the producer's list.
class MotionDejitter {
 public:
  static constexpr uint32_t kSlots=3;
  bool Initialize(ID3D12Device* device);
  // Copies a volatile source region into this slot's staging texture on the
  // producer list, restoring the source state. Leaves staging in COMMON.
  bool Capture(ID3D12GraphicsCommandList* producer,ID3D12Resource* source,D3D12_RESOURCE_STATES state,
      const DejitterRegion& region,uint32_t slot,Microsoft::WRL::ComPtr<ID3D12Resource>& staged);
  // Opens the internal list and records source - delta into the output.
  // The output is left in NON_PIXEL_SHADER_RESOURCE; tags may be recorded
  // on the returned list before Submit.
  ID3D12GraphicsCommandList* Convert(ID3D12Resource* source,D3D12_RESOURCE_STATES state,
      const DejitterRegion& region,DejitterVector delta,ID3D12Resource** output);
  bool Submit(ID3D12CommandQueue* queue);
  // Waits for every submitted conversion (retirement and tests).
  bool Drain(uint32_t timeoutMs);
  HRESULT LastError() const { return error; }
 private:
  bool Fail(HRESULT hr) { error=hr; return false; }
  bool EnsureTexture(Microsoft::WRL::ComPtr<ID3D12Resource>& texture,DXGI_FORMAT format,uint32_t width,uint32_t height,
      D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES initial);
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocators[kSlots];
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  Microsoft::WRL::ComPtr<ID3D12Resource> outputs[kSlots], staging[kSlots];
  HANDLE event{};
  uint64_t fenceValues[kSlots]{}, nextFence{1};
  uint32_t slot{}, descriptorSize{};
  bool open{};
  HRESULT error{S_OK};
 public:
  ~MotionDejitter();
};
}
