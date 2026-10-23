// SPDX-License-Identifier: MIT
// Plan 5.4: stands in for the NGX driver module (_nvngx.dll). DLSS-SR
// features succeed and evaluations are counted: the adapter must observe
// them without replacing the game's upscaling.
#include <atomic>
#include <cstdint>
namespace { struct Handle { uint32_t id; }; std::atomic<uint32_t> evaluations{}, next{}; }
extern "C" {
__declspec(dllexport) int NVSDK_NGX_D3D12_CreateFeature(void*,int,void*,Handle** out) {
  if(!out) return int(0xBAD00005);
  *out=new Handle{++next}; return 1;
}
__declspec(dllexport) int NVSDK_NGX_D3D12_EvaluateFeature(void*,const Handle* handle,const void* parameters,void*) {
  if(!handle || !parameters) return int(0xBAD00005);
  ++evaluations; return 1;
}
__declspec(dllexport) int NVSDK_NGX_D3D12_ReleaseFeature(Handle* handle) { delete handle; return 1; }
__declspec(dllexport) uint32_t FixtureNgxEvaluations() { return evaluations; }
}
