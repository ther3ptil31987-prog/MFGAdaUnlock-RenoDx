/*
 * Read-only NVIDIA runtime status used by the addon UI.
 * SPDX-License-Identifier: MIT
 *
 * This is the minimal public NVAPI ABI required for three observations:
 * - the current Reflex/VSync/VRR/Dynamic-FG sleep state;
 * - the game's public Reflex latency markers and render-queue timestamps;
 * - the current NGX Frame Generation preset override state.
 *
 * It deliberately exposes no setters. General UI state is sampled only while
 * the ReShade panel is visible. Latency Guard uses a separate bounded sampler
 * (twice per second) so Automatic mode can react without touching every frame.
 */

#pragma once

#include <windows.h>
#include <unknwn.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include "latency_analysis.hpp"
#include "latency_trial.hpp"

namespace mfgunlock::nvapistatus {

using Status = int32_t;
constexpr Status kOk = 0;

template <typename T, uint32_t Version>
constexpr uint32_t StructVersion() {
  return static_cast<uint32_t>(sizeof(T)) | (Version << 16);
}

struct SleepStatus {
  uint32_t version;
  uint8_t low_latency_mode;
  uint8_t fullscreen_vrr;
  uint8_t control_panel_vsync;
  uint32_t sleep_interval_us;
  uint8_t game_sleep;
  uint8_t fullscreen_independent_flip;
  uint8_t frame_generation_multiplier;
  uint8_t dynamic_frame_generation_control;
  uint32_t dynamic_frame_time_target_us;
  uint8_t reserved[114];
};
static_assert(sizeof(SleepStatus) == 136);

struct LatencyFrame {
  uint64_t frame_id;
  uint64_t input_sample_time;
  uint64_t simulation_start_time;
  uint64_t simulation_end_time;
  uint64_t render_submit_start_time;
  uint64_t render_submit_end_time;
  uint64_t present_start_time;
  uint64_t present_end_time;
  uint64_t driver_start_time;
  uint64_t driver_end_time;
  uint64_t os_render_queue_start_time;
  uint64_t os_render_queue_end_time;
  uint64_t gpu_render_start_time;
  uint64_t gpu_render_end_time;
  uint32_t gpu_active_render_time_us;
  uint32_t gpu_frame_time_us;
  uint64_t camera_constructed_time;
  uint32_t cross_adapter_copy_time_us;
  uint32_t ai_frame_time_us;
  uint8_t reserved[104];
};
static_assert(sizeof(LatencyFrame) == 240);

struct LatencyResult {
  uint32_t version;
  uint32_t alignment_padding;
  LatencyFrame frames[64];
  uint8_t reserved[32];
};
static_assert(sizeof(LatencyResult) == 15400);

struct NgxOverrideState {
  uint32_t version;
  uint32_t process_id;
  uint64_t feedback_super_resolution;
  uint64_t feedback_ray_reconstruction;
  uint64_t feedback_frame_generation;
  float scaling_ratio;
  uint32_t performance_mode;
  uint32_t render_preset;
  uint32_t frame_generation_count;
  uint32_t frame_generation_preset;
  uint32_t frame_generation_mode;
  uint64_t reserved0;
  uint32_t reserved1;
  uint32_t reserved[7];
};
static_assert(sizeof(NgxOverrideState) == 96);

struct Snapshot {
  bool library_loaded = false;
  Status initialize_status = INT32_MIN;
  Status sleep_status = INT32_MIN;
  Status ngx_override_status = INT32_MIN;
  SleepStatus sleep{};
  NgxOverrideState ngx{};
};

struct GuardObservation : latency::Report<LatencyFrame> {
  bool library_loaded = false;
  Status initialize_status = INT32_MIN;
  Status sleep_status = INT32_MIN;
  Status latency_status = INT32_MIN;
  SleepStatus sleep{};
};

using QueryInterfaceFn = void*(__cdecl*)(uint32_t);
using InitializeFn = Status(__cdecl*)();
using GetSleepStatusFn = Status(__cdecl*)(IUnknown*, SleepStatus*);
using GetLatencyFn = Status(__cdecl*)(IUnknown*, LatencyResult*);
using GetNgxOverrideStateFn = Status(__cdecl*)(NgxOverrideState*);

// Public interface IDs from NVIDIA's NVAPI SDK.
constexpr uint32_t kInitializeId = 0x0150E828;
constexpr uint32_t kGetSleepStatusId = 0xAEF96CA1;
constexpr uint32_t kGetLatencyId = 0x1A587F9C;
constexpr uint32_t kGetNgxOverrideStateId = 0x3FD96FBA;

// Public NV_NGX_DLSS_OVERRIDE_BITFIELD values. Keep the local ABI declarations
// small, but name every bit consumed by the read-only UI so it never infers an
// active DLSS feature from a stale value in the shared SR/RR fields.
constexpr uint64_t kNgxOverrideInitialized = 1ull << 0;
constexpr uint64_t kNgxOverrideEnabled = 1ull << 1;
constexpr uint64_t kNgxOverrideDllExists = 1ull << 2;
constexpr uint64_t kNgxOverrideDllLoaded = 1ull << 3;
constexpr uint64_t kNgxOverrideDllSelected = 1ull << 4;
constexpr uint64_t kNgxOverridePreset = 1ull << 5;
constexpr uint64_t kNgxOverridePerformanceMode = 1ull << 6;
constexpr uint64_t kNgxOverrideScalingRatio = 1ull << 7;
constexpr uint64_t kNgxOverrideCreated = 1ull << 9;
constexpr uint64_t kNgxOverrideEvaluate = 1ull << 10;
constexpr uint64_t kNgxOverrideFrameGenerationMode = 1ull << 13;
constexpr uint64_t kNgxOverrideMultiFrame = 1ull << 15;
constexpr uint64_t kFrameGenerationPresetFeedback = kNgxOverridePreset;

inline bool NgxFeatureActive(uint64_t feedback) {
  return (feedback & (kNgxOverrideCreated | kNgxOverrideEvaluate)) != 0;
}

inline bool NgxFeatureConfigured(uint64_t feedback) {
  return (feedback &
          (kNgxOverrideEnabled | kNgxOverrideDllExists |
           kNgxOverrideDllLoaded | kNgxOverrideDllSelected |
           kNgxOverridePreset | kNgxOverridePerformanceMode |
           kNgxOverrideScalingRatio | kNgxOverrideFrameGenerationMode |
           kNgxOverrideMultiFrame)) != 0;
}

inline HMODULE Module() {
  static const HMODULE module =
      LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  return module;
}

inline QueryInterfaceFn QueryInterface() {
  static const auto query =
      Module() == nullptr
          ? nullptr
          : reinterpret_cast<QueryInterfaceFn>(
                GetProcAddress(Module(), "nvapi_QueryInterface"));
  return query;
}

inline Status Initialize() {
  static const Status status = [] {
    const auto query = QueryInterface();
    if (query == nullptr) return INT32_MIN;
    const auto initialize =
        reinterpret_cast<InitializeFn>(query(kInitializeId));
    return initialize == nullptr ? INT32_MIN : initialize();
  }();
  return status;
}

inline GetSleepStatusFn GetSleepStatus() {
  static const auto function = [] {
    const auto query = QueryInterface();
    return query == nullptr
               ? nullptr
               : reinterpret_cast<GetSleepStatusFn>(query(kGetSleepStatusId));
  }();
  return function;
}

inline GetLatencyFn GetLatency() {
  static const auto function = [] {
    const auto query = QueryInterface();
    return query == nullptr
               ? nullptr
               : reinterpret_cast<GetLatencyFn>(query(kGetLatencyId));
  }();
  return function;
}

inline GetNgxOverrideStateFn GetNgxOverrideState() {
  static const auto function = [] {
    const auto query = QueryInterface();
    return query == nullptr
               ? nullptr
               : reinterpret_cast<GetNgxOverrideStateFn>(
                     query(kGetNgxOverrideStateId));
  }();
  return function;
}

inline uint64_t QpcFrequency() {
  static const uint64_t frequency = [] {
    LARGE_INTEGER value{};
    return QueryPerformanceFrequency(&value)
               ? static_cast<uint64_t>(value.QuadPart)
               : 0ull;
  }();
  return frequency;
}

inline uint32_t QpcTicksToUs(uint64_t begin, uint64_t end) {
  if (begin == 0 || end <= begin) return 0;
  const uint64_t frequency = QpcFrequency();
  if (frequency == 0) return 0;
  const uint64_t ticks = end - begin;
  const uint64_t microseconds =
      ticks > (UINT64_MAX / 1000000ull)
          ? UINT64_MAX
          : (ticks * 1000000ull) / frequency;
  return microseconds > UINT32_MAX ? UINT32_MAX
                                   : static_cast<uint32_t>(microseconds);
}

template <size_t N>
inline uint32_t Percentile(std::array<uint32_t, N>& values, size_t count,
                           size_t numerator, size_t denominator) {
  if (count == 0 || denominator == 0) return 0;
  std::sort(values.begin(), values.begin() + count);
  const size_t index = ((count - 1) * numerator) / denominator;
  return values[index];
}

inline Snapshot Observe(IUnknown* device, uint32_t process_id) {
  Snapshot result{};
  if (Module() == nullptr) return result;
  result.library_loaded = true;

  const auto query = QueryInterface();
  if (query == nullptr) return result;
  result.initialize_status = Initialize();

  if (device != nullptr) {
    result.sleep.version = StructVersion<SleepStatus, 1>();
    if (const auto get_sleep = GetSleepStatus();
        get_sleep != nullptr) {
      result.sleep_status = get_sleep(device, &result.sleep);
    }

  }

  result.ngx.version = StructVersion<NgxOverrideState, 2>();
  result.ngx.process_id = process_id;
  if (const auto get_ngx = GetNgxOverrideState();
      get_ngx != nullptr) {
    result.ngx_override_status = get_ngx(&result.ngx);
  }
  return result;
}

// Lightweight bounded sample used by Latency Guard. It deliberately has no
// setter and does not insert or repair markers. The full 64-frame result stays
// thread-local so the presentation callback does not allocate a 15 KiB object
// on its stack every time it samples.
inline GuardObservation ObserveGuard(IUnknown* device, uint64_t epoch = 0) {
  GuardObservation result{};
  if (Module() == nullptr) return result;
  result.library_loaded = true;
  const auto query = QueryInterface();
  if (query == nullptr || device == nullptr) return result;
  result.initialize_status = Initialize();

  result.sleep.version = StructVersion<SleepStatus, 1>();
  if (const auto get_sleep = GetSleepStatus();
      get_sleep != nullptr) {
    result.sleep_status = get_sleep(device, &result.sleep);
  }

  thread_local LatencyResult latency{};
  latency = {};
  latency.version = StructVersion<LatencyResult, 1>();
  if (const auto get_latency = GetLatency();
      get_latency != nullptr) {
    result.latency_status = get_latency(device, &latency);
    if (result.latency_status == kOk) {
      thread_local latency::History history{};
      LARGE_INTEGER current_counter{};
      QueryPerformanceCounter(&current_counter);
      static_cast<latency::Report<LatencyFrame>&>(result) =
          latency::Analyze(latency.frames, QpcFrequency(),
                           GetTickCount64(), reinterpret_cast<uintptr_t>(device) ^ epoch,
                           history, static_cast<uint64_t>(current_counter.QuadPart));
    }
  }
  return result;
}

}  // namespace mfgunlock::nvapistatus
