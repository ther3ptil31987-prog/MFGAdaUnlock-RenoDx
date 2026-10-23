// SPDX-License-Identifier: MIT
// Streamline 1.x-shaped interposer: same export names, older signatures and
// no slGetFeatureFunction. The bridge must not install input detours on it.
#include <windows.h>
#include <atomic>
#include <cstdint>
#define EXPORT extern "C" __declspec(dllexport) __declspec(noinline)
static std::atomic<uint32_t> calls{};
EXPORT int slInit(const void*,uint64_t) { ++calls; return 0; }
EXPORT int slShutdown() { ++calls; return 0; }
// 1.x: resource, buffer type, id, extent.
EXPORT int slSetTag(const void* resource,uint32_t type,uint32_t id,const void* extent) {
  calls+=uint32_t(uintptr_t(resource)!=0)+type+id+uint32_t(extent!=nullptr); return 0;
}
// 1.x: constants by value reference, frame index, viewport id.
EXPORT int slSetConstants(const void*,uint32_t frame,uint32_t id) { calls+=frame+id; return 0; }
EXPORT int slSetFeatureConstants(uint32_t feature,const void*,uint32_t frame,uint32_t id) { calls+=feature+frame+id; return 0; }
EXPORT int slEvaluateFeature(void*,uint32_t feature,uint32_t frame,uint32_t id) { calls+=feature+frame+id; return 0; }
EXPORT int slIsFeatureSupported(uint32_t,uint32_t* mask) { if(mask)*mask=1; return 0; }
EXPORT uint32_t FixtureSl1Calls() { return calls; }
