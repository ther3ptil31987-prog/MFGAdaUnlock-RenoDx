/* SPDX-License-Identifier: MIT
 * Phase-validated CUDA confidence history for Adaptive Quality V3.2/V3.4.
 *
 * This deliberately uses a minimal dynamically-resolved Driver API surface.
 * No CUDA SDK library is linked into the addon. The patched kernel keeps its
 * original geometry/inpaint ABIs and exposes only private module-global
 * control blocks.
 */
#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <include/reshade.hpp>

#include "./framecount.hpp"
#include "./ngx_hook.hpp"

namespace mfgunlock::cudatemporal {

enum class StabilityMode : uint32_t {
  kLocal = 1,
  kTemporal = 2,
};

enum class InpaintMode : uint32_t {
  kV2Compatibility = 0,
  kLocal = 1,
  kTemporal = 2,
};

// Used only when AdaptiveQualityV3InpaintMode has not been configured yet.
// Invalid persisted values still normalize to V2 Compatibility below.
inline constexpr InpaintMode kDefaultInpaintMode = InpaintMode::kTemporal;

inline constexpr InpaintMode NormalizeInpaintMode(uint32_t value) {
  return value <= static_cast<uint32_t>(InpaintMode::kTemporal)
             ? static_cast<InpaintMode>(value)
             : InpaintMode::kV2Compatibility;
}

inline constexpr StabilityMode NormalizeMode(uint32_t value) {
  return value == static_cast<uint32_t>(StabilityMode::kTemporal)
             ? StabilityMode::kTemporal
             : StabilityMode::kLocal;
}

struct Phase {
  uint32_t index = 0;
  uint32_t bucket = 0;
  bool swap = false;
  bool valid = false;
};

inline Phase ResolvePhase(float t, uint32_t multiplier) {
  Phase result{};
  if (!std::isfinite(t) || multiplier < 2 || multiplier > 6) return result;
  const float scaled = t * static_cast<float>(multiplier);
  const long rounded = std::lround(scaled);
  if (rounded <= 0 || rounded >= static_cast<long>(multiplier)) return result;
  const float exact = static_cast<float>(rounded) /
                      static_cast<float>(multiplier);
  if (std::abs(t - exact) > 1.0f / 2048.0f) return result;
  result.index = static_cast<uint32_t>(rounded);
  result.bucket = (std::min)(result.index, multiplier - result.index) - 1u;
  result.swap = result.index > multiplier - result.index;
  result.valid = true;
  return result;
}

inline uint64_t RequiredHistoryBytes(uint32_t width, uint32_t height,
                                     uint32_t multiplier) {
  if (width == 0 || height == 0 || multiplier < 2 || multiplier > 6) return 0;
  return uint64_t{width} * height * 2u * (multiplier / 2u);
}

inline constexpr uint64_t kHistoryLimit = 64ull * 1024ull * 1024ull;
inline constexpr uint64_t kArenaLimit = 96ull * 1024ull * 1024ull;
inline constexpr uint32_t kHistoryMagic = 0x56333148u;
inline constexpr uint32_t kInpaintHistoryMagic = 0x56333449u;

inline uint8_t UpdateHistoryReference(float current, uint8_t previous,
                                      bool symmetric_second) {
  current = std::clamp(current, 0.0f, 1.0f);
  if (previous != 255u) {
    const float old = static_cast<float>(previous) / 254.0f;
    current = (std::min)(current,
                         symmetric_second ? old : old + 0.20f);
  }
  return static_cast<uint8_t>(
      (std::min)(std::lround(current * 254.0f), 254l));
}

inline std::atomic<StabilityMode> g_mode{StabilityMode::kTemporal};
inline std::atomic<InpaintMode> g_inpaint_mode{kDefaultInpaintMode};
inline std::atomic_bool g_provider_authorized{false};
inline std::atomic_bool g_hooked{false};
inline std::atomic_bool g_installing{false};
inline std::atomic_bool g_install_failed{false};
// Non-null only when the addon had to acquire its own system32 reference.
// Keeping it until hook teardown guarantees that every trampoline remains
// backed by the same CUDA Driver module for the lifetime of the hooks.
inline std::atomic<HMODULE> g_cuda_reference{nullptr};
inline std::atomic_bool g_temporal_active{false};
inline std::atomic<uint64_t> g_history_bytes{0};
inline std::atomic<uint64_t> g_inpaint_history_bytes{0};
inline std::atomic<uint64_t> g_arena_bytes{0};
inline std::atomic_bool g_inpaint_temporal_active{false};
inline std::atomic_bool g_inpaint_fallback{false};
inline std::atomic<uint32_t> g_history_resets{0};
inline std::atomic<uint32_t> g_reset_requests{0};
inline std::atomic<uint32_t> g_probe_launches{0};
inline std::atomic<uint32_t> g_probe_phase_mask{0};
inline std::atomic<uint32_t> g_history_width{0};
inline std::atomic<uint32_t> g_history_height{0};
inline std::atomic<uint32_t> g_history_multiplier{0};
inline std::atomic_bool g_fallback{false};
inline std::atomic_bool g_fast_path_ready{false};
inline SRWLOCK g_detail_lock = SRWLOCK_INIT;
inline std::string g_detail{"local stability; CUDA temporal probe not started"};
inline SRWLOCK g_inpaint_detail_lock = SRWLOCK_INIT;
inline std::string g_inpaint_detail{"V2 Compatibility selected"};

inline void SetDetail(std::string detail, bool fallback = false) {
  AcquireSRWLockExclusive(&g_detail_lock);
  g_detail = std::move(detail);
  ReleaseSRWLockExclusive(&g_detail_lock);
  g_fallback.store(fallback, std::memory_order_release);
}

inline std::string Detail() {
  AcquireSRWLockShared(&g_detail_lock);
  std::string result = g_detail;
  ReleaseSRWLockShared(&g_detail_lock);
  return result;
}

inline void SetInpaintDetail(std::string detail, bool fallback = false) {
  AcquireSRWLockExclusive(&g_inpaint_detail_lock);
  g_inpaint_detail = std::move(detail);
  ReleaseSRWLockExclusive(&g_inpaint_detail_lock);
  g_inpaint_fallback.store(fallback, std::memory_order_release);
}

inline std::string InpaintDetail() {
  AcquireSRWLockShared(&g_inpaint_detail_lock);
  std::string result = g_inpaint_detail;
  ReleaseSRWLockShared(&g_inpaint_detail_lock);
  return result;
}

inline bool FastPathReady() {
  return g_fast_path_ready.load(std::memory_order_acquire);
}

inline void Configure(StabilityMode mode,
                      InpaintMode inpaint_mode = kDefaultInpaintMode) {
  g_mode.store(mode, std::memory_order_release);
  g_inpaint_mode.store(inpaint_mode, std::memory_order_release);
  if (mode == StabilityMode::kLocal) {
    g_temporal_active.store(false, std::memory_order_release);
    SetDetail("Local Stable selected; CUDA history disabled");
  }
  if (inpaint_mode != InpaintMode::kTemporal) {
    g_inpaint_temporal_active.store(false, std::memory_order_release);
    SetInpaintDetail(inpaint_mode == InpaintMode::kLocal
                         ? "Local V3 selected; inpaint history disabled"
                         : "V2 Compatibility selected");
  }
}

inline void AuthorizeExactProvider(bool authorized) {
  if (authorized) g_provider_authorized.store(true, std::memory_order_release);
}

inline void RequestHistoryReset() {
  g_reset_requests.fetch_add(1, std::memory_order_acq_rel);
}

namespace internal {

using CUresult = int;
using CUdeviceptr = uint64_t;
using CUfunction = void*;
using CUmodule = void*;
using CUcontext = void*;
using CUstream = void*;
constexpr CUresult kSuccess = 0;

struct CUlaunchConfig {
  unsigned int grid_dim_x;
  unsigned int grid_dim_y;
  unsigned int grid_dim_z;
  unsigned int block_dim_x;
  unsigned int block_dim_y;
  unsigned int block_dim_z;
  unsigned int shared_mem_bytes;
  CUstream stream;
  void* attrs;
  unsigned int num_attrs;
};

using LaunchFn = CUresult(WINAPI*)(
    CUfunction, unsigned int, unsigned int, unsigned int, unsigned int,
    unsigned int, unsigned int, unsigned int, CUstream, void**, void**);
using LaunchExFn = CUresult(WINAPI*)(const CUlaunchConfig*, CUfunction, void**,
                                     void**);
using FuncGetNameFn = CUresult(WINAPI*)(const char**, CUfunction);
using FuncGetModuleFn = CUresult(WINAPI*)(CUmodule*, CUfunction);
using FuncGetParamCountFn = CUresult(WINAPI*)(CUfunction, size_t*);
using FuncGetParamInfoFn = CUresult(WINAPI*)(CUfunction, size_t, size_t*,
                                             size_t*);
using ModuleGetGlobalFn = CUresult(WINAPI*)(CUdeviceptr*, size_t*, CUmodule,
                                            const char*);
using CtxGetCurrentFn = CUresult(WINAPI*)(CUcontext*);
using MemAllocFn = CUresult(WINAPI*)(CUdeviceptr*, size_t);
using MemcpyHtoDAsyncFn = CUresult(WINAPI*)(CUdeviceptr, const void*, size_t,
                                            CUstream);
using MemcpyHtoDFn = CUresult(WINAPI*)(CUdeviceptr, const void*, size_t);
using MemcpyDtoHFn = CUresult(WINAPI*)(void*, CUdeviceptr, size_t);
using MemsetD8AsyncFn = CUresult(WINAPI*)(CUdeviceptr, unsigned char, size_t,
                                          CUstream);

inline LaunchFn g_real_launch = nullptr;
inline LaunchFn g_real_launch_ptsz = nullptr;
inline LaunchExFn g_real_launch_ex = nullptr;
inline LaunchExFn g_real_launch_ex_ptsz = nullptr;
inline FuncGetNameFn g_func_get_name = nullptr;
inline FuncGetModuleFn g_func_get_module = nullptr;
inline FuncGetParamCountFn g_func_get_param_count = nullptr;
inline FuncGetParamInfoFn g_func_get_param_info = nullptr;
inline ModuleGetGlobalFn g_module_get_global = nullptr;
inline CtxGetCurrentFn g_ctx_get_current = nullptr;
inline MemAllocFn g_mem_alloc = nullptr;
inline MemcpyHtoDAsyncFn g_memcpy_htod_async = nullptr;
inline MemcpyHtoDFn g_memcpy_htod = nullptr;
inline MemcpyDtoHFn g_memcpy_dtoh = nullptr;
inline MemsetD8AsyncFn g_memset_d8_async = nullptr;
inline std::vector<hook::HookItem> g_hooks;

#pragma pack(push, 1)
struct KernelParameters {
  uint8_t prefix[32];
  float t;
  uint8_t middle[76];
  uint32_t width;
  uint32_t height;
  uint8_t suffix[24];
};
#pragma pack(pop)
static_assert(sizeof(KernelParameters) == 144);
static_assert(offsetof(KernelParameters, t) == 32);
static_assert(offsetof(KernelParameters, width) == 112);
static_assert(offsetof(KernelParameters, height) == 116);

#pragma pack(push, 1)
struct InpaintKernelParameters {
  uint8_t prefix[136];
  uint32_t width;
  uint32_t height;
  uint8_t suffix[8];
};
#pragma pack(pop)
static_assert(sizeof(InpaintKernelParameters) == 152);
static_assert(offsetof(InpaintKernelParameters, width) == 136);
static_assert(offsetof(InpaintKernelParameters, height) == 140);

enum class LaunchApi : uint32_t {
  kKernel = 1,
  kKernelPtsz = 2,
  kKernelEx = 3,
  kKernelExPtsz = 4,
};

struct alignas(16) Control {
  uint64_t history = 0;
  uint32_t enabled = 0;
  uint32_t reserved = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t multiplier = 0;
  uint32_t pixel_count = 0;
};
static_assert(sizeof(Control) == 32);
static_assert(offsetof(Control, history) == 0);
static_assert(offsetof(Control, enabled) == 8);
static_assert(offsetof(Control, width) == 16);
static_assert(offsetof(Control, pixel_count) == 28);

struct alignas(16) InpaintControl {
  uint64_t history = 0;
  uint32_t enabled = 0;
  uint32_t reserved = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t multiplier = 0;
  uint32_t pixel_count = 0;
  uint32_t bucket = 0;
  uint32_t direction = 0;
  uint32_t generation = 0;
  uint32_t reserved2 = 0;
};
static_assert(sizeof(InpaintControl) == 48);
static_assert(offsetof(InpaintControl, history) == 0);
static_assert(offsetof(InpaintControl, enabled) == 8);
static_assert(offsetof(InpaintControl, width) == 16);
static_assert(offsetof(InpaintControl, bucket) == 32);

struct State {
  SRWLOCK lock = SRWLOCK_INIT;
  CUfunction function = nullptr;
  CUmodule module = nullptr;
  CUcontext context = nullptr;
  CUstream stream = nullptr;
  CUdeviceptr control_device = 0;
  CUdeviceptr history = 0;
  uint64_t arena_capacity = 0;
  CUfunction inpaint_function = nullptr;
  CUdeviceptr inpaint_control_device = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t multiplier = 0;
  uint32_t phase_mask = 0;
  uint32_t valid_launches = 0;
  uint32_t last_phase = 0;
  uint32_t completed_cycles = 0;
  int32_t phase_direction = 0;
  LaunchApi launch_api = LaunchApi::kKernel;
  bool launch_api_seen = false;
  bool active = false;
  bool permanently_local = false;
  bool waiting_multiplier_reported = false;
  bool inpaint_permanently_local = false;
  bool inpaint_initialized = false;
  bool pending_inpaint_phase = false;
  Phase inpaint_phase{};
  uint32_t generation = 0;
  uint32_t reset_request = 0;
  uint32_t quality_reset_request = 0;
  std::vector<std::unique_ptr<Control>> control_snapshots;
  // Pageable HtoD staging is captured by the Driver call. A bounded ring also
  // keeps source addresses stable for integrations that defer the copy while
  // avoiding allocation, logging or container growth on the launch path.
  std::array<InpaintControl, 256> inpaint_controls{};
  uint32_t inpaint_control_cursor = 0;
};
inline State g_state;

struct DispatchTable {
  CUfunction geometry = nullptr;
  CUfunction inpaint = nullptr;
};
inline std::atomic<const DispatchTable*> g_dispatch{nullptr};
inline std::vector<std::unique_ptr<DispatchTable>> g_dispatch_snapshots;

inline void PublishDispatch(State& state) {
  const bool needs_geometry =
      g_mode.load(std::memory_order_acquire) == StabilityMode::kTemporal;
  const bool needs_inpaint =
      g_inpaint_mode.load(std::memory_order_acquire) == InpaintMode::kTemporal;
  const bool complete =
      (!needs_geometry || state.function != nullptr ||
       state.permanently_local) &&
      (!needs_inpaint || state.inpaint_function != nullptr ||
       state.inpaint_permanently_local);
  if (!complete) {
    g_fast_path_ready.store(false, std::memory_order_release);
    return;
  }
  auto table = std::make_unique<DispatchTable>();
  table->geometry = state.function;
  table->inpaint = state.inpaint_function;
  const auto* published = table.get();
  g_dispatch_snapshots.push_back(std::move(table));
  g_dispatch.store(published, std::memory_order_release);
  g_fast_path_ready.store(true, std::memory_order_release);
}
#if defined(MFGUNLOCK_CUDA_TEMPORAL_TESTING)
inline std::atomic<uint64_t> g_test_lock_acquisitions{0};
inline void ResetStateForTests() {
  AcquireSRWLockExclusive(&g_state.lock);
  g_state.function = nullptr;
  g_state.module = nullptr;
  g_state.context = nullptr;
  g_state.stream = nullptr;
  g_state.control_device = 0;
  g_state.history = 0;
  g_state.arena_capacity = 0;
  g_state.inpaint_function = nullptr;
  g_state.inpaint_control_device = 0;
  g_state.width = 0;
  g_state.height = 0;
  g_state.multiplier = 0;
  g_state.phase_mask = 0;
  g_state.valid_launches = 0;
  g_state.last_phase = 0;
  g_state.completed_cycles = 0;
  g_state.phase_direction = 0;
  g_state.launch_api = LaunchApi::kKernel;
  g_state.launch_api_seen = false;
  g_state.active = false;
  g_state.permanently_local = false;
  g_state.waiting_multiplier_reported = false;
  g_state.inpaint_permanently_local = false;
  g_state.inpaint_initialized = false;
  g_state.pending_inpaint_phase = false;
  g_state.inpaint_phase = {};
  g_state.generation = 0;
  g_state.reset_request = 0;
  g_state.quality_reset_request = 0;
  g_state.control_snapshots.clear();
  g_state.inpaint_controls = {};
  g_state.inpaint_control_cursor = 0;
  ReleaseSRWLockExclusive(&g_state.lock);
  g_dispatch.store(nullptr, std::memory_order_release);
  g_dispatch_snapshots.clear();
  g_fast_path_ready.store(false, std::memory_order_release);
}
#endif

inline bool ResolveDriverFunctions(HMODULE cuda) {
  const auto proc = [cuda](const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(cuda, name));
  };
  g_func_get_name = reinterpret_cast<FuncGetNameFn>(proc("cuFuncGetName"));
  g_func_get_module =
      reinterpret_cast<FuncGetModuleFn>(proc("cuFuncGetModule"));
  g_func_get_param_count =
      reinterpret_cast<FuncGetParamCountFn>(proc("cuFuncGetParamCount"));
  g_func_get_param_info =
      reinterpret_cast<FuncGetParamInfoFn>(proc("cuFuncGetParamInfo"));
  g_module_get_global =
      reinterpret_cast<ModuleGetGlobalFn>(proc("cuModuleGetGlobal_v2"));
  g_ctx_get_current =
      reinterpret_cast<CtxGetCurrentFn>(proc("cuCtxGetCurrent"));
  g_mem_alloc = reinterpret_cast<MemAllocFn>(proc("cuMemAlloc_v2"));
  g_memcpy_htod_async = reinterpret_cast<MemcpyHtoDAsyncFn>(
      proc("cuMemcpyHtoDAsync_v2"));
  g_memcpy_htod =
      reinterpret_cast<MemcpyHtoDFn>(proc("cuMemcpyHtoD_v2"));
  g_memcpy_dtoh =
      reinterpret_cast<MemcpyDtoHFn>(proc("cuMemcpyDtoH_v2"));
  g_memset_d8_async =
      reinterpret_cast<MemsetD8AsyncFn>(proc("cuMemsetD8Async"));
  return g_func_get_name && g_func_get_module && g_func_get_param_count &&
         g_func_get_param_info && g_module_get_global && g_ctx_get_current &&
         g_mem_alloc && g_memcpy_htod_async && g_memcpy_htod &&
         g_memcpy_dtoh && g_memset_d8_async;
}

inline const uint8_t* ParameterBuffer(void** kernel_params, void** extra,
                                      size_t expected_size =
                                          sizeof(KernelParameters)) {
  if (kernel_params != nullptr && kernel_params[0] != nullptr)
    return static_cast<const uint8_t*>(kernel_params[0]);
  if (extra == nullptr) return nullptr;
  const void* buffer = nullptr;
  size_t size = 0;
  for (size_t index = 0; index < 16; index += 2) {
    const uintptr_t token = reinterpret_cast<uintptr_t>(extra[index]);
    if (token == 0) break;
    if (extra[index + 1] == nullptr) return nullptr;
    if (token == 1) {
      buffer = extra[index + 1];
    } else if (token == 2) {
      std::memcpy(&size, extra[index + 1], sizeof(size));
    } else {
      return nullptr;
    }
  }
  return buffer != nullptr && size == expected_size
             ? static_cast<const uint8_t*>(buffer)
             : nullptr;
}

inline uint32_t EffectiveMultiplier() {
  if (!framecount::g_effective_request_seen.load(std::memory_order_acquire))
    return 0;
  const uint32_t generated =
      framecount::g_last_effective_generated.load(std::memory_order_relaxed);
  return generated >= 1 && generated <= 5 ? generated + 1 : 0;
}

inline void DisableDeviceHistory(State& state, const char* reason,
                                 bool write_control = true) {
  state.active = false;
  g_temporal_active.store(false, std::memory_order_release);
  g_history_bytes.store(0, std::memory_order_relaxed);
  if (write_control && state.control_device != 0 && g_memcpy_htod != nullptr) {
    Control disabled{};
    g_memcpy_htod(state.control_device, &disabled, sizeof(disabled));
  }
  SetDetail(reason, true);
}

inline void DisableInpaintHistory(State& state, const char* reason,
                                  bool fallback = true,
                                  bool write_control = true) {
  state.inpaint_initialized = false;
  state.pending_inpaint_phase = false;
  g_inpaint_temporal_active.store(false, std::memory_order_release);
  g_inpaint_history_bytes.store(0, std::memory_order_relaxed);
  if (write_control && state.inpaint_control_device != 0 &&
      g_memcpy_htod != nullptr) {
    InpaintControl disabled{};
    g_memcpy_htod(state.inpaint_control_device, &disabled, sizeof(disabled));
  }
  SetInpaintDetail(reason, fallback);
}

enum class TargetKind { kNone, kGeometry, kInpaint };

inline TargetKind IdentifyTargetFunction(CUfunction function) {
  const char* name = nullptr;
  if (g_func_get_name(&name, function) != kSuccess || name == nullptr)
    return TargetKind::kNone;
  if (std::strcmp(name, "Kernel_EstimateIntermMvecsScatter") == 0)
    return TargetKind::kGeometry;
  if (std::strcmp(name, "Kernel_OutputPull") == 0)
    return TargetKind::kInpaint;
  return TargetKind::kNone;
}

inline bool BindTargetFirstTime(State& state, CUfunction function,
                                CUstream stream) {
  CUmodule module = nullptr;
  CUcontext context = nullptr;
  if (g_func_get_module(&module, function) != kSuccess || module == nullptr ||
      g_ctx_get_current(&context) != kSuccess || context == nullptr) {
    state.permanently_local = true;
    SetDetail("Temporal Stable fallback: CUDA function context/module unavailable",
              true);
    return false;
  }
  if ((state.function != nullptr && state.function != function) ||
      (state.context != nullptr && state.context != context) ||
      (state.stream != nullptr && state.stream != stream)) {
    const bool context_changed =
        state.context != nullptr && state.context != context;
    state.permanently_local = true;
    DisableDeviceHistory(
        state,
        "Temporal Stable fallback: multiple CUDA functions, contexts, or streams observed",
        !context_changed);
    return false;
  }
  size_t count = 0, offset = 0, parameter_size = 0;
  if (g_func_get_param_count(function, &count) != kSuccess || count != 1 ||
      g_func_get_param_info(function, 0, &offset, &parameter_size) != kSuccess ||
      offset != 0 || parameter_size != sizeof(KernelParameters)) {
    state.permanently_local = true;
    SetDetail("Temporal Stable fallback: CUDA kernel parameter ABI is not 144 bytes",
              true);
    return false;
  }
  CUdeviceptr magic = 0, control = 0;
  size_t magic_size = 0, control_size = 0;
  if (g_module_get_global(&magic, &magic_size, module,
                          "mfgunlock_v31_history_magic") != kSuccess ||
      magic == 0 || magic_size != sizeof(uint32_t) ||
      g_module_get_global(&control, &control_size, module,
                          "mfgunlock_v31_history_control") != kSuccess ||
      control == 0 || control_size != sizeof(Control)) {
    state.permanently_local = true;
    SetDetail("Temporal Stable fallback: patched CUDA module symbols not found",
              true);
    return false;
  }
  uint32_t magic_value = 0;
  if (g_memcpy_dtoh(&magic_value, magic, sizeof(magic_value)) != kSuccess ||
      magic_value != kHistoryMagic) {
    state.permanently_local = true;
    SetDetail("Temporal Stable fallback: patched CUDA module magic mismatch",
              true);
    return false;
  }
  state.function = function;
  state.module = module;
  state.context = context;
  state.stream = stream;
  state.control_device = control;
  PublishDispatch(state);
  return true;
}

inline bool BindInpaintFirstTime(State& state, CUfunction function,
                                 CUstream stream) {
  CUmodule module = nullptr;
  CUcontext context = nullptr;
  if (g_func_get_module(&module, function) != kSuccess || module == nullptr ||
      g_ctx_get_current(&context) != kSuccess || context == nullptr) {
    state.inpaint_permanently_local = true;
    SetInpaintDetail(
        "Temporal V3 fallback: CUDA function context/module unavailable", true);
    return false;
  }
  if ((state.inpaint_function != nullptr &&
       state.inpaint_function != function) ||
      (state.module != nullptr && state.module != module) ||
      (state.context != nullptr && state.context != context) ||
      (state.stream != nullptr && state.stream != stream)) {
    state.inpaint_permanently_local = true;
    SetInpaintDetail(
        "Temporal V3 fallback: inpaint module, context, or stream differs from geometry",
        true);
    return false;
  }
  size_t count = 0, offset = 0, parameter_size = 0;
  if (g_func_get_param_count(function, &count) != kSuccess || count != 1 ||
      g_func_get_param_info(function, 0, &offset, &parameter_size) != kSuccess ||
      offset != 0 || parameter_size != sizeof(InpaintKernelParameters)) {
    state.inpaint_permanently_local = true;
    SetInpaintDetail(
        "Temporal V3 fallback: CUDA inpaint kernel parameter ABI is not 152 bytes",
        true);
    return false;
  }
  CUdeviceptr magic = 0, control = 0;
  size_t magic_size = 0, control_size = 0;
  if (g_module_get_global(&magic, &magic_size, module,
                          "mfgunlock_v34_inpaint_magic") != kSuccess ||
      magic == 0 || magic_size != sizeof(uint32_t) ||
      g_module_get_global(&control, &control_size, module,
                          "mfgunlock_v34_inpaint_control") != kSuccess ||
      control == 0 || control_size != sizeof(InpaintControl)) {
    state.inpaint_permanently_local = true;
    SetInpaintDetail(
        "Temporal V3 fallback: patched inpaint CUDA symbols not found", true);
    return false;
  }
  uint32_t magic_value = 0;
  if (g_memcpy_dtoh(&magic_value, magic, sizeof(magic_value)) != kSuccess ||
      magic_value != kInpaintHistoryMagic) {
    state.inpaint_permanently_local = true;
    SetInpaintDetail("Temporal V3 fallback: inpaint CUDA magic mismatch", true);
    return false;
  }
  state.inpaint_function = function;
  state.module = module;
  state.context = context;
  state.stream = stream;
  state.inpaint_control_device = control;
  PublishDispatch(state);
  SetInpaintDetail(
      "Temporal V3 kernel and ABI validated; waiting for geometry phase");
  return true;
}

inline bool ValidateBoundTarget(State& state, CUfunction function,
                                CUstream stream) {
  CUcontext context = nullptr;
  if (state.function != function ||
      g_ctx_get_current(&context) != kSuccess || context == nullptr ||
      context != state.context || stream != state.stream) {
    const bool context_changed = state.context != nullptr &&
                                 context != nullptr && context != state.context;
    state.permanently_local = true;
    DisableDeviceHistory(
        state,
        "Temporal Stable fallback: CUDA function, context, or stream changed",
        !context_changed);
    return false;
  }
  return true;
}

inline bool ValidateBoundInpaint(State& state, CUfunction function,
                                 CUstream stream) {
  CUcontext context = nullptr;
  if (state.inpaint_function != function ||
      g_ctx_get_current(&context) != kSuccess || context == nullptr ||
      context != state.context || stream != state.stream) {
    const bool context_changed = state.context != nullptr &&
                                 context != nullptr &&
                                 context != state.context;
    state.inpaint_permanently_local = true;
    DisableInpaintHistory(
        state,
        "Temporal V3 fallback: inpaint function, context, or stream changed",
        true, !context_changed);
    return false;
  }
  return true;
}

inline void ResetProbe(State& state, uint32_t width, uint32_t height,
                       uint32_t multiplier) {
  if (state.active) DisableDeviceHistory(state, "Temporal Stable probe reset");
  state.width = width;
  state.height = height;
  state.multiplier = multiplier;
  state.phase_mask = 0;
  state.valid_launches = 0;
  state.last_phase = 0;
  state.completed_cycles = 0;
  state.phase_direction = 0;
  state.inpaint_initialized = false;
  state.pending_inpaint_phase = false;
  g_inpaint_temporal_active.store(false, std::memory_order_release);
  g_inpaint_history_bytes.store(0, std::memory_order_relaxed);
  if (state.inpaint_control_device != 0 && g_memcpy_htod != nullptr) {
    InpaintControl disabled{};
    g_memcpy_htod(state.inpaint_control_device, &disabled, sizeof(disabled));
  }
  ++state.generation;
  g_history_resets.fetch_add(1, std::memory_order_relaxed);
  g_probe_launches.store(0, std::memory_order_relaxed);
  g_probe_phase_mask.store(0, std::memory_order_relaxed);
  SetDetail("Temporal Stable read-only phase probe in progress");
  if (!state.inpaint_permanently_local &&
      g_inpaint_mode.load(std::memory_order_acquire) == InpaintMode::kTemporal)
    SetInpaintDetail("Temporal V3 phase probe reset; Local V3 effective");
}

inline bool ObservePhaseSequence(State& state, const Phase& phase) {
  const uint32_t phase_count = state.multiplier - 1u;
  if (phase_count == 1u) {
    if (state.last_phase != 0) ++state.completed_cycles;
    state.last_phase = phase.index;
    return true;
  }
  if (state.last_phase == 0) {
    state.last_phase = phase.index;
    return true;
  }
  const uint32_t ascending = state.last_phase == phase_count
                                 ? 1u
                                 : state.last_phase + 1u;
  const uint32_t descending = state.last_phase == 1u
                                  ? phase_count
                                  : state.last_phase - 1u;
  if (state.phase_direction == 0) {
    if (phase.index == ascending)
      state.phase_direction = 1;
    else if (phase.index == descending)
      state.phase_direction = -1;
    else
      return false;
  } else {
    const uint32_t expected = state.phase_direction > 0 ? ascending : descending;
    if (phase.index != expected) return false;
  }
  const bool wrapped = state.phase_direction > 0
                           ? state.last_phase == phase_count && phase.index == 1u
                           : state.last_phase == 1u && phase.index == phase_count;
  if (wrapped) ++state.completed_cycles;
  state.last_phase = phase.index;
  return true;
}

inline bool Activate(State& state) {
  const uint64_t required =
      RequiredHistoryBytes(state.width, state.height, state.multiplier);
  if (required == 0 || required > kHistoryLimit) {
    state.permanently_local = true;
    DisableDeviceHistory(state,
                         "Temporal Stable fallback: 64 MiB history limit exceeded");
    return false;
  }
  uint64_t arena_capacity =
      g_inpaint_mode.load(std::memory_order_acquire) == InpaintMode::kTemporal
          ? kArenaLimit
          : kHistoryLimit;
  if (state.history == 0) {
    CUresult allocation =
        g_mem_alloc(&state.history, static_cast<size_t>(arena_capacity));
    if (allocation != kSuccess && arena_capacity == kArenaLimit) {
      state.history = 0;
      arena_capacity = kHistoryLimit;
      allocation =
          g_mem_alloc(&state.history, static_cast<size_t>(arena_capacity));
      if (allocation == kSuccess) {
        state.inpaint_permanently_local = true;
        SetInpaintDetail(
            "Temporal V3 fallback: 96 MiB arena allocation failed; geometry retained in 64 MiB",
            true);
        PublishDispatch(state);
      }
    }
    if (allocation != kSuccess) {
      state.history = 0;
      state.permanently_local = true;
      DisableDeviceHistory(
          state, "Temporal Stable fallback: CUDA history allocation failed");
      return false;
    }
  }
  if (state.history != 0 && state.arena_capacity == 0)
    state.arena_capacity = arena_capacity;
  if (g_memset_d8_async(state.history, 255, static_cast<size_t>(required),
                        state.stream) != kSuccess) {
    state.permanently_local = true;
    DisableDeviceHistory(state,
                         "Temporal Stable fallback: CUDA history clear failed");
    return false;
  }
  auto control = std::make_unique<Control>();
  control->history = state.history;
  control->pixel_count = state.width * state.height;
  control->width = state.width;
  control->height = state.height;
  control->multiplier = state.multiplier;
  control->enabled = 1;
  if (g_memcpy_htod_async(state.control_device, control.get(), sizeof(Control),
                          state.stream) != kSuccess) {
    state.permanently_local = true;
    DisableDeviceHistory(state,
                         "Temporal Stable fallback: CUDA control upload failed");
    return false;
  }
  state.control_snapshots.push_back(std::move(control));
  state.active = true;
  g_arena_bytes.store(state.arena_capacity, std::memory_order_relaxed);
  g_temporal_active.store(true, std::memory_order_release);
  g_history_bytes.store(required, std::memory_order_relaxed);
  g_history_width.store(state.width, std::memory_order_relaxed);
  g_history_height.store(state.height, std::memory_order_relaxed);
  g_history_multiplier.store(state.multiplier, std::memory_order_relaxed);
  std::ostringstream detail;
  detail << "Temporal Stable active: " << state.width << "x" << state.height
         << ", " << state.multiplier << "x, " << required
         << " history bytes, single validated CUDA stream";
  SetDetail(detail.str());
#if !defined(MFGUNLOCK_CUDA_TEMPORAL_TESTING)
  reshade::log::message(reshade::log::level::info, detail.str().c_str());
#endif
  return true;
}

inline bool PublishInpaintControl(State& state, const Phase& phase) {
  if (g_inpaint_mode.load(std::memory_order_acquire) !=
          InpaintMode::kTemporal ||
      state.inpaint_permanently_local || state.inpaint_function == nullptr ||
      state.inpaint_control_device == 0 || !state.active || state.history == 0)
    return false;
  const uint64_t geometry_bytes =
      RequiredHistoryBytes(state.width, state.height, state.multiplier);
  const uint64_t inpaint_bytes = geometry_bytes;
  if (geometry_bytes == 0 ||
      geometry_bytes + inpaint_bytes > state.arena_capacity) {
    state.inpaint_permanently_local = true;
    DisableInpaintHistory(
        state,
        "Temporal V3 fallback: combined geometry/inpaint history exceeds 96 MiB",
        true);
    return false;
  }
  const CUdeviceptr inpaint_history = state.history + geometry_bytes;
  if (!state.inpaint_initialized) {
    if (g_memset_d8_async(inpaint_history, 255,
                          static_cast<size_t>(inpaint_bytes), state.stream) !=
        kSuccess) {
      state.inpaint_permanently_local = true;
      DisableInpaintHistory(
          state, "Temporal V3 fallback: inpaint history clear failed", true);
      return false;
    }
    state.inpaint_initialized = true;
  }
  InpaintControl& control = state.inpaint_controls[
      state.inpaint_control_cursor++ % state.inpaint_controls.size()];
  control = {};
  control.history = inpaint_history;
  control.enabled = 1;
  control.width = state.width;
  control.height = state.height;
  control.multiplier = state.multiplier;
  control.pixel_count = state.width * state.height;
  control.bucket = phase.bucket;
  control.direction = phase.swap ? 1u : 0u;
  control.generation = state.generation;
  if (g_memcpy_htod_async(state.inpaint_control_device, &control,
                          sizeof(InpaintControl), state.stream) != kSuccess) {
    state.inpaint_permanently_local = true;
    DisableInpaintHistory(
        state, "Temporal V3 fallback: inpaint control upload failed", true);
    return false;
  }
  state.pending_inpaint_phase = true;
  state.inpaint_phase = phase;
  const bool was_active =
      g_inpaint_temporal_active.exchange(true, std::memory_order_acq_rel);
  g_inpaint_history_bytes.store(inpaint_bytes, std::memory_order_relaxed);
  if (!was_active)
    SetInpaintDetail(
        "Temporal V3 active; phase supplied by validated geometry launch");
  return true;
}

inline void PrepareLaunch(LaunchApi launch_api, CUfunction function,
                          CUstream stream,
                          void** kernel_params, void** extra) {
  const DispatchTable* dispatch = g_dispatch.load(std::memory_order_acquire);
  if (dispatch != nullptr &&
      function != dispatch->geometry && function != dispatch->inpaint)
    return;
  const bool geometry_requested =
      g_mode.load(std::memory_order_acquire) == StabilityMode::kTemporal;
  const bool inpaint_requested =
      g_inpaint_mode.load(std::memory_order_acquire) == InpaintMode::kTemporal;
  if ((!geometry_requested && !inpaint_requested) ||
      !g_provider_authorized.load(std::memory_order_acquire)) return;

  TargetKind kind = TargetKind::kNone;
  if (dispatch != nullptr) {
    if (function == dispatch->geometry)
      kind = TargetKind::kGeometry;
    else if (function == dispatch->inpaint)
      kind = TargetKind::kInpaint;
  }
  if (kind == TargetKind::kNone) {
    kind = IdentifyTargetFunction(function);
    if (kind == TargetKind::kNone ||
        (kind == TargetKind::kGeometry && !geometry_requested &&
         !inpaint_requested) ||
        (kind == TargetKind::kInpaint && !inpaint_requested))
      return;
  }
  State& state = g_state;
#if defined(MFGUNLOCK_CUDA_TEMPORAL_TESTING)
  g_test_lock_acquisitions.fetch_add(1, std::memory_order_relaxed);
#endif
  AcquireSRWLockExclusive(&state.lock);
  if ((kind == TargetKind::kGeometry && state.permanently_local &&
       !inpaint_requested) ||
      (kind == TargetKind::kInpaint && state.inpaint_permanently_local)) {
    ReleaseSRWLockExclusive(&state.lock);
    return;
  }
  bool just_bound = false;
  if (kind == TargetKind::kGeometry) {
    if (state.function == nullptr) {
      if (!BindTargetFirstTime(state, function, stream)) {
        PublishDispatch(state);
        ReleaseSRWLockExclusive(&state.lock);
        return;
      }
      just_bound = true;
    } else if (!ValidateBoundTarget(state, function, stream)) {
      ReleaseSRWLockExclusive(&state.lock);
      return;
    }
  } else {
    if (state.inpaint_function == nullptr) {
      if (!BindInpaintFirstTime(state, function, stream)) {
        PublishDispatch(state);
        ReleaseSRWLockExclusive(&state.lock);
        return;
      }
      just_bound = true;
    } else if (!ValidateBoundInpaint(state, function, stream)) {
      ReleaseSRWLockExclusive(&state.lock);
      return;
    }
  }
  if (state.launch_api_seen && state.launch_api != launch_api) {
    state.permanently_local = true;
    DisableDeviceHistory(
        state, "Temporal Stable fallback: multiple CUDA launch APIs observed");
    ReleaseSRWLockExclusive(&state.lock);
    return;
  }
  state.launch_api = launch_api;
  state.launch_api_seen = true;
  const size_t parameter_size = kind == TargetKind::kGeometry
                                    ? sizeof(KernelParameters)
                                    : sizeof(InpaintKernelParameters);
  const uint8_t* buffer = ParameterBuffer(kernel_params, extra,
                                          parameter_size);
  if (buffer == nullptr) {
    if (kind == TargetKind::kGeometry) {
      state.permanently_local = true;
      DisableDeviceHistory(
          state, "Temporal Stable fallback: unsupported CUDA launch argument layout");
    } else {
      state.inpaint_permanently_local = true;
      DisableInpaintHistory(
          state,
          "Temporal V3 fallback: unsupported CUDA launch argument layout", true);
    }
    ReleaseSRWLockExclusive(&state.lock);
    return;
  }
  if (kind == TargetKind::kInpaint) {
    uint32_t width = 0, height = 0;
    std::memcpy(&width,
                buffer + offsetof(InpaintKernelParameters, width),
                sizeof(width));
    std::memcpy(&height,
                buffer + offsetof(InpaintKernelParameters, height),
                sizeof(height));
    if (width == 0 || height == 0 || width != state.width ||
        height != state.height) {
      state.inpaint_permanently_local = true;
      DisableInpaintHistory(
          state,
          "Temporal V3 fallback: inpaint dimensions differ from geometry",
          true);
    } else if (!state.pending_inpaint_phase) {
      if (!just_bound && state.active && state.inpaint_initialized) {
        state.inpaint_permanently_local = true;
        DisableInpaintHistory(
            state,
            "Temporal V3 fallback: inpaint launch has no unique preceding geometry phase",
            true);
      } else {
        SetInpaintDetail(
            "Temporal V3 kernel validated; geometry phase probe in progress");
      }
    } else {
      state.pending_inpaint_phase = false;
    }
    ReleaseSRWLockExclusive(&state.lock);
    return;
  }
  float t = 0.0f;
  uint32_t width = 0;
  uint32_t height = 0;
  std::memcpy(&t, buffer + offsetof(KernelParameters, t), sizeof(t));
  std::memcpy(&width, buffer + offsetof(KernelParameters, width),
              sizeof(width));
  std::memcpy(&height, buffer + offsetof(KernelParameters, height),
              sizeof(height));
  const uint32_t multiplier = EffectiveMultiplier();
  if (multiplier == 0) {
    if (state.active || state.inpaint_initialized) {
      DisableDeviceHistory(
          state, "Temporal Stable disabled: frame generation is not active");
      DisableInpaintHistory(
          state,
          "Temporal V3 local fallback: frame generation is not active", true);
      ++state.generation;
      g_history_resets.fetch_add(1, std::memory_order_relaxed);
    }
    if (!state.waiting_multiplier_reported) {
      SetDetail(
          "Temporal Stable read-only probe waiting for effective multiplier");
      state.waiting_multiplier_reported = true;
    }
    ReleaseSRWLockExclusive(&state.lock);
    return;
  }
  state.waiting_multiplier_reported = false;
  const uint32_t reset_request =
      g_reset_requests.load(std::memory_order_acquire);
  const uint32_t quality_reset_request =
      framecount::g_quality_resets_requested.load(std::memory_order_acquire);
  if (state.reset_request != reset_request ||
      state.quality_reset_request != quality_reset_request) {
    state.reset_request = reset_request;
    state.quality_reset_request = quality_reset_request;
    ResetProbe(state, width, height, multiplier);
  }
  const Phase phase = ResolvePhase(t, multiplier);
  if (!phase.valid || width == 0 || height == 0 ||
      width > 16384 || height > 16384) {
    state.permanently_local = true;
    DisableDeviceHistory(
        state, "Temporal Stable fallback: invalid phase or dimensions observed");
    ReleaseSRWLockExclusive(&state.lock);
    return;
  }
  if (state.width != width || state.height != height ||
      state.multiplier != multiplier) {
    ResetProbe(state, width, height, multiplier);
  }
  if (!ObservePhaseSequence(state, phase)) {
    state.permanently_local = true;
    DisableDeviceHistory(
        state, "Temporal Stable fallback: CUDA phase sequence is ambiguous");
    ReleaseSRWLockExclusive(&state.lock);
    return;
  }
  state.phase_mask |= 1u << phase.index;
  ++state.valid_launches;
  g_probe_launches.store(state.valid_launches, std::memory_order_relaxed);
  g_probe_phase_mask.store(state.phase_mask, std::memory_order_relaxed);
  const uint32_t expected_mask = ((1u << multiplier) - 1u) & ~1u;
  if (!state.active && state.phase_mask == expected_mask &&
      state.completed_cycles >= 2u) {
    Activate(state);
  }
  if (state.pending_inpaint_phase && inpaint_requested &&
      state.inpaint_initialized) {
    state.inpaint_permanently_local = true;
    DisableInpaintHistory(
        state,
        "Temporal V3 fallback: multiple geometry phases preceded one inpaint launch",
        true);
  } else if (!state.inpaint_permanently_local) {
    PublishInpaintControl(state, phase);
  }
  ReleaseSRWLockExclusive(&state.lock);
}

inline CUresult WINAPI HookedLaunch(
    CUfunction function, unsigned int gx, unsigned int gy, unsigned int gz,
    unsigned int bx, unsigned int by, unsigned int bz, unsigned int shared,
    CUstream stream, void** params, void** extra) {
  PrepareLaunch(LaunchApi::kKernel, function, stream, params, extra);
  return g_real_launch(function, gx, gy, gz, bx, by, bz, shared, stream,
                       params, extra);
}

inline CUresult WINAPI HookedLaunchPtsz(
    CUfunction function, unsigned int gx, unsigned int gy, unsigned int gz,
    unsigned int bx, unsigned int by, unsigned int bz, unsigned int shared,
    CUstream stream, void** params, void** extra) {
  PrepareLaunch(LaunchApi::kKernelPtsz, function, stream, params, extra);
  return g_real_launch_ptsz(function, gx, gy, gz, bx, by, bz, shared, stream,
                            params, extra);
}

inline CUresult WINAPI HookedLaunchEx(const CUlaunchConfig* config,
                                      CUfunction function, void** params,
                                      void** extra) {
  PrepareLaunch(LaunchApi::kKernelEx, function,
                config != nullptr ? config->stream : nullptr, params, extra);
  return g_real_launch_ex(config, function, params, extra);
}

inline CUresult WINAPI HookedLaunchExPtsz(const CUlaunchConfig* config,
                                          CUfunction function, void** params,
                                          void** extra) {
  PrepareLaunch(LaunchApi::kKernelExPtsz, function,
                config != nullptr ? config->stream : nullptr, params, extra);
  return g_real_launch_ex_ptsz(config, function, params, extra);
}

}  // namespace internal

inline void TryInstall() {
  if (g_hooked.load(std::memory_order_acquire) ||
      g_install_failed.load(std::memory_order_acquire) ||
      !g_provider_authorized.load(std::memory_order_acquire) ||
      (g_mode.load(std::memory_order_acquire) != StabilityMode::kTemporal &&
       g_inpaint_mode.load(std::memory_order_acquire) !=
           InpaintMode::kTemporal))
    return;
  bool expected = false;
  if (!g_installing.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel))
    return;
  const auto finish = []() {
    g_installing.store(false, std::memory_order_release);
  };
  HMODULE cuda = GetModuleHandleW(L"nvcuda.dll");
  if (cuda == nullptr) {
    // Some Streamline integrations delay-load the CUDA Driver until after the
    // provider cubin has already been installed. Waiting passively in that
    // case creates a deadlock: no launch can be observed until the hook is
    // installed, and no hook can be installed until the driver is loaded.
    // Exact-provider authorization and Temporal mode have both been checked
    // above, so acquire the genuine system driver here, outside loader lock.
    cuda = LoadLibraryExW(L"nvcuda.dll", nullptr,
                          LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (cuda == nullptr) {
      std::ostringstream detail;
      detail << "Temporal Stable fallback: system nvcuda.dll load failed (Win32 "
             << GetLastError() << ")";
      SetDetail(detail.str(), true);
      if (g_inpaint_mode.load(std::memory_order_acquire) ==
          InpaintMode::kTemporal)
        SetInpaintDetail(detail.str(), true);
      g_install_failed.store(true, std::memory_order_release);
      finish();
      return;
    }
    g_cuda_reference.store(cuda, std::memory_order_release);
  }
  if (!internal::ResolveDriverFunctions(cuda)) {
    SetDetail("Temporal Stable fallback: required CUDA Driver APIs are absent",
              true);
    if (g_inpaint_mode.load(std::memory_order_acquire) ==
        InpaintMode::kTemporal)
      SetInpaintDetail(
          "Temporal V3 fallback: required CUDA Driver APIs are absent", true);
    g_install_failed.store(true, std::memory_order_release);
    finish();
    return;
  }
  internal::g_hooks.clear();
  const auto add = [&](const char* name, void** real, void* replacement) {
    if (GetProcAddress(cuda, name) != nullptr)
      internal::g_hooks.emplace_back(name, real, replacement);
  };
  add("cuLaunchKernel", reinterpret_cast<void**>(&internal::g_real_launch),
      reinterpret_cast<void*>(&internal::HookedLaunch));
  add("cuLaunchKernel_ptsz",
      reinterpret_cast<void**>(&internal::g_real_launch_ptsz),
      reinterpret_cast<void*>(&internal::HookedLaunchPtsz));
  add("cuLaunchKernelEx", reinterpret_cast<void**>(&internal::g_real_launch_ex),
      reinterpret_cast<void*>(&internal::HookedLaunchEx));
  add("cuLaunchKernelEx_ptsz",
      reinterpret_cast<void**>(&internal::g_real_launch_ex_ptsz),
      reinterpret_cast<void*>(&internal::HookedLaunchExPtsz));
  if (internal::g_hooks.size() != 4u ||
      !hook::Install(cuda, internal::g_hooks, "nvcuda.dll")) {
    SetDetail("Temporal Stable fallback: CUDA launch hooks were not installed",
              true);
    if (g_inpaint_mode.load(std::memory_order_acquire) ==
        InpaintMode::kTemporal)
      SetInpaintDetail(
          "Temporal V3 fallback: CUDA launch hooks were not installed", true);
    g_install_failed.store(true, std::memory_order_release);
    finish();
    return;
  }
  g_hooked.store(true, std::memory_order_release);
  SetDetail("Temporal Stable CUDA hooks installed; read-only phase probe pending");
  if (g_inpaint_mode.load(std::memory_order_acquire) ==
      InpaintMode::kTemporal)
    SetInpaintDetail(
        "Temporal V3 CUDA hooks installed; kernel/ABI/phase association pending");
  finish();
}

inline void Uninstall() {
  const bool hooked = g_hooked.exchange(false, std::memory_order_acq_rel);
  if (hooked) hook::Uninstall(internal::g_hooks);
  // Device allocations are context-owned and intentionally left for CUDA
  // context teardown. Freeing while provider worker streams may still execute
  // would be less safe than bounded process-lifetime retention.
  g_temporal_active.store(false, std::memory_order_release);
  g_inpaint_temporal_active.store(false, std::memory_order_release);
  g_fast_path_ready.store(false, std::memory_order_release);
  internal::g_dispatch.store(nullptr, std::memory_order_release);
  if (HMODULE cuda = g_cuda_reference.exchange(nullptr,
                                                std::memory_order_acq_rel);
      cuda != nullptr)
    FreeLibrary(cuda);
}

}  // namespace mfgunlock::cudatemporal
