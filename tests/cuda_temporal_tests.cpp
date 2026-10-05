#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <unordered_map>

#include "../src/addons/mfgunlock/cuda_temporal.hpp"

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition    \
                << '\n';                                                       \
      return EXIT_FAILURE;                                                     \
    }                                                                          \
  } while (false)

namespace {
int g_launch_calls = 0;
int g_launch_ex_calls = 0;
int g_name_calls = 0;
int g_module_calls = 0;
int g_param_calls = 0;
int g_global_calls = 0;
int g_context_calls = 0;
int g_alloc_calls = 0;
int g_memset_calls = 0;
int g_async_copy_calls = 0;
bool g_fail_96_mib = false;
mfgunlock::cudatemporal::internal::InpaintControl g_last_inpaint_control{};
void* const kTarget = reinterpret_cast<void*>(uintptr_t{0x1000});
void* const kInpaint = reinterpret_cast<void*>(uintptr_t{0x1800});
void* const kOther = reinterpret_cast<void*>(uintptr_t{0x2000});
void* const kModule = reinterpret_cast<void*>(uintptr_t{0x3000});
void* const kContext = reinterpret_cast<void*>(uintptr_t{0x4000});

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeGetName(
    const char** name,
    mfgunlock::cudatemporal::internal::CUfunction function) {
  ++g_name_calls;
  *name = function == kTarget
              ? "Kernel_EstimateIntermMvecsScatter"
              : function == kInpaint ? "Kernel_OutputPull"
                                     : "SomeOtherKernel";
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeGetModule(
    mfgunlock::cudatemporal::internal::CUmodule* module,
    mfgunlock::cudatemporal::internal::CUfunction) {
  ++g_module_calls;
  *module = kModule;
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeGetParamCount(
    mfgunlock::cudatemporal::internal::CUfunction, size_t* count) {
  ++g_param_calls;
  *count = 1;
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeGetParamInfo(
    mfgunlock::cudatemporal::internal::CUfunction function, size_t, size_t* offset,
    size_t* size) {
  ++g_param_calls;
  *offset = 0;
  *size = function == kInpaint ? 152 : 144;
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeGetGlobal(
    mfgunlock::cudatemporal::internal::CUdeviceptr* pointer, size_t* size,
    mfgunlock::cudatemporal::internal::CUmodule, const char* name) {
  ++g_global_calls;
  if (std::strcmp(name, "mfgunlock_v31_history_magic") == 0) {
    *pointer = 0x5000;
    *size = 4;
  } else if (std::strcmp(name, "mfgunlock_v31_history_control") == 0) {
    *pointer = 0x6000;
    *size = 32;
  } else if (std::strcmp(name, "mfgunlock_v34_inpaint_magic") == 0) {
    *pointer = 0x7000;
    *size = 4;
  } else {
    *pointer = 0x8000;
    *size = 48;
  }
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeGetContext(
    mfgunlock::cudatemporal::internal::CUcontext* context) {
  ++g_context_calls;
  *context = kContext;
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeDtoH(
    void* destination, mfgunlock::cudatemporal::internal::CUdeviceptr source,
    size_t size) {
  const uint32_t magic = source == 0x7000
                             ? mfgunlock::cudatemporal::kInpaintHistoryMagic
                             : mfgunlock::cudatemporal::kHistoryMagic;
  std::memcpy(destination, &magic, (std::min)(size, sizeof(magic)));
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeMemAlloc(
    mfgunlock::cudatemporal::internal::CUdeviceptr* pointer, size_t size) {
  ++g_alloc_calls;
  if (g_fail_96_mib && size == mfgunlock::cudatemporal::kArenaLimit)
    return 2;
  *pointer = 0x100000;
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeMemset(
    mfgunlock::cudatemporal::internal::CUdeviceptr, unsigned char, size_t,
    mfgunlock::cudatemporal::internal::CUstream) {
  ++g_memset_calls;
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeHtoDAsync(
    mfgunlock::cudatemporal::internal::CUdeviceptr destination,
    const void* source, size_t size,
    mfgunlock::cudatemporal::internal::CUstream) {
  ++g_async_copy_calls;
  if (destination == 0x8000 &&
      size == sizeof(mfgunlock::cudatemporal::internal::InpaintControl))
    std::memcpy(&g_last_inpaint_control, source, size);
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeHtoD(
    mfgunlock::cudatemporal::internal::CUdeviceptr destination,
    const void* source, size_t size) {
  if (destination == 0x8000 &&
      size == sizeof(mfgunlock::cudatemporal::internal::InpaintControl))
    std::memcpy(&g_last_inpaint_control, source, size);
  return 0;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeLaunch(
    mfgunlock::cudatemporal::internal::CUfunction, unsigned int, unsigned int,
    unsigned int, unsigned int, unsigned int, unsigned int, unsigned int,
    mfgunlock::cudatemporal::internal::CUstream, void**, void**) {
  ++g_launch_calls;
  return 17;
}

mfgunlock::cudatemporal::internal::CUresult WINAPI FakeLaunchEx(
    const mfgunlock::cudatemporal::internal::CUlaunchConfig*,
    mfgunlock::cudatemporal::internal::CUfunction, void**, void**) {
  ++g_launch_ex_calls;
  return 23;
}
}  // namespace

int main() {
  using namespace mfgunlock::cudatemporal;

  CHECK(NormalizeMode(2) == StabilityMode::kTemporal);
  CHECK(NormalizeMode(1) == StabilityMode::kLocal);
  CHECK(NormalizeMode(0) == StabilityMode::kLocal);
  CHECK(NormalizeMode(99) == StabilityMode::kLocal);
  CHECK(NormalizeInpaintMode(0) == InpaintMode::kV2Compatibility);
  CHECK(NormalizeInpaintMode(1) == InpaintMode::kLocal);
  CHECK(NormalizeInpaintMode(2) == InpaintMode::kTemporal);
  CHECK(NormalizeInpaintMode(99) == InpaintMode::kV2Compatibility);
  CHECK(kDefaultInpaintMode == InpaintMode::kTemporal);

  for (uint32_t multiplier : {2u, 4u, 6u}) {
    for (uint32_t index = 1; index < multiplier; ++index) {
      const Phase phase = ResolvePhase(
          static_cast<float>(index) / static_cast<float>(multiplier),
          multiplier);
      CHECK(phase.valid);
      CHECK(phase.index == index);
      CHECK(phase.bucket ==
            (std::min)(index, multiplier - index) - 1u);
      CHECK(phase.swap == (index > multiplier - index));
    }
  }
  CHECK(!ResolvePhase(0.0f, 4).valid);
  CHECK(!ResolvePhase(1.0f, 4).valid);
  CHECK(!ResolvePhase(0.25f + 1.0f / 1024.0f, 4).valid);
  CHECK(!ResolvePhase(std::numeric_limits<float>::infinity(), 4).valid);

  CHECK(RequiredHistoryBytes(3840, 2160, 6) == 49'766'400ull);
  CHECK(RequiredHistoryBytes(3840, 2160, 6) < kHistoryLimit);
  CHECK(RequiredHistoryBytes(7680, 4320, 6) > kHistoryLimit);
  CHECK(RequiredHistoryBytes(0, 2160, 6) == 0);

  const uint8_t first_confidence = UpdateHistoryReference(0.40f, 255u, false);
  CHECK(std::abs(static_cast<float>(first_confidence) / 254.0f - 0.40f) <
        1.0f / 254.0f);
  const uint8_t recovered =
      UpdateHistoryReference(1.0f, first_confidence, false);
  CHECK(static_cast<float>(recovered - first_confidence) / 254.0f <=
        0.20f + 1.0f / 254.0f);
  CHECK(UpdateHistoryReference(1.0f, recovered, true) == recovered);
  CHECK(UpdateHistoryReference(0.10f, recovered, true) < recovered);

  using namespace mfgunlock::cudatemporal::internal;
  KernelParameters parameters{};
  parameters.t = 0.25f;
  parameters.width = 3840;
  parameters.height = 2160;
  void* kernel_params[] = {&parameters};
  CHECK(ParameterBuffer(kernel_params, nullptr) ==
        reinterpret_cast<const uint8_t*>(&parameters));
  size_t parameter_size = sizeof(parameters);
  void* extra[] = {reinterpret_cast<void*>(1), &parameters,
                   reinterpret_cast<void*>(2), &parameter_size, nullptr};
  CHECK(ParameterBuffer(nullptr, extra) ==
        reinterpret_cast<const uint8_t*>(&parameters));
  parameter_size = sizeof(parameters) - 1;
  CHECK(ParameterBuffer(nullptr, extra) == nullptr);

  State ascending{};
  ascending.multiplier = 4;
  for (uint32_t phase : {1u, 2u, 3u, 1u, 2u, 3u, 1u}) {
    Phase observation{};
    observation.index = phase;
    observation.valid = true;
    CHECK(ObservePhaseSequence(ascending, observation));
  }
  CHECK(ascending.phase_direction == 1);
  CHECK(ascending.completed_cycles == 2);

  State descending{};
  descending.multiplier = 6;
  for (uint32_t phase : {5u, 4u, 3u, 2u, 1u, 5u, 4u, 3u, 2u, 1u, 5u}) {
    Phase observation{};
    observation.index = phase;
    observation.valid = true;
    CHECK(ObservePhaseSequence(descending, observation));
  }
  CHECK(descending.phase_direction == -1);
  CHECK(descending.completed_cycles == 2);

  State ambiguous{};
  ambiguous.multiplier = 6;
  Phase first{};
  first.index = 1;
  first.valid = true;
  CHECK(ObservePhaseSequence(ambiguous, first));
  Phase skipped{};
  skipped.index = 3;
  skipped.valid = true;
  CHECK(!ObservePhaseSequence(ambiguous, skipped));

  CHECK(sizeof(Control) == 32);
  CHECK(alignof(Control) == 16);
  CHECK(offsetof(Control, enabled) == 8);
  CHECK(offsetof(Control, width) == 16);
  CHECK(offsetof(Control, pixel_count) == 28);
  CHECK(sizeof(InpaintControl) == 48);
  CHECK(alignof(InpaintControl) == 16);
  CHECK(offsetof(InpaintControl, bucket) == 32);

  InpaintKernelParameters inpaint_parameters{};
  inpaint_parameters.width = parameters.width;
  inpaint_parameters.height = parameters.height;
  void* inpaint_kernel_params[] = {&inpaint_parameters};

  // Associate the exact target once. Afterwards unrelated kernels must take
  // only the atomic CUfunction comparison: no lock and no Driver API query.
  g_func_get_name = FakeGetName;
  g_func_get_module = FakeGetModule;
  g_func_get_param_count = FakeGetParamCount;
  g_func_get_param_info = FakeGetParamInfo;
  g_module_get_global = FakeGetGlobal;
  g_ctx_get_current = FakeGetContext;
  g_memcpy_dtoh = FakeDtoH;
  g_mem_alloc = FakeMemAlloc;
  g_memset_d8_async = FakeMemset;
  g_memcpy_htod_async = FakeHtoDAsync;
  g_memcpy_htod = FakeHtoD;
  g_mode.store(StabilityMode::kTemporal, std::memory_order_relaxed);
  g_inpaint_mode.store(InpaintMode::kTemporal, std::memory_order_relaxed);
  g_provider_authorized.store(true, std::memory_order_relaxed);
  parameter_size = sizeof(parameters);
  for (const auto api : {LaunchApi::kKernel, LaunchApi::kKernelPtsz,
                         LaunchApi::kKernelEx, LaunchApi::kKernelExPtsz}) {
    ResetStateForTests();
    PrepareLaunch(api, kTarget, nullptr,
                  api == LaunchApi::kKernel ||
                          api == LaunchApi::kKernelPtsz
                      ? kernel_params
                      : nullptr,
                  api == LaunchApi::kKernelEx ||
                          api == LaunchApi::kKernelExPtsz
                      ? extra
                      : nullptr);
    CHECK(g_state.launch_api_seen);
    CHECK(g_state.launch_api == api);
    CHECK(g_state.function == kTarget);
    CHECK(g_dispatch.load(std::memory_order_acquire) == nullptr);
  }
  ResetStateForTests();
  g_name_calls = 0;
  g_module_calls = 0;
  g_param_calls = 0;
  g_global_calls = 0;
  g_context_calls = 0;
  g_test_lock_acquisitions.store(0, std::memory_order_relaxed);
  PrepareLaunch(LaunchApi::kKernel, kTarget, nullptr, kernel_params, nullptr);
  PrepareLaunch(LaunchApi::kKernel, kInpaint, nullptr,
                inpaint_kernel_params, nullptr);
  const auto* dispatch = g_dispatch.load(std::memory_order_acquire);
  CHECK(dispatch != nullptr);
  CHECK(dispatch->geometry == kTarget);
  CHECK(dispatch->inpaint == kInpaint);
  CHECK(FastPathReady());
  CHECK(g_name_calls == 2);
  CHECK(g_module_calls == 2);
  CHECK(g_param_calls == 4);
  CHECK(g_global_calls == 4);
  CHECK(g_context_calls == 2);
  const uint64_t locks_after_binding =
      g_test_lock_acquisitions.load(std::memory_order_relaxed);
  constexpr size_t kLaunches = 1'000'000;
  constexpr size_t kThreads = 4;
  const auto run_fast = [] {
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    for (size_t worker = 0; worker < kThreads; ++worker) {
      workers.emplace_back([] {
        for (size_t index = 0; index < kLaunches / kThreads; ++index)
          PrepareLaunch(LaunchApi::kKernel, kOther, nullptr, nullptr, nullptr);
      });
    }
    for (auto& worker : workers) worker.join();
    return std::chrono::duration<double, std::nano>(
               std::chrono::steady_clock::now() - start).count() /
           kLaunches;
  };
  CHECK(g_test_lock_acquisitions.load(std::memory_order_relaxed) ==
        locks_after_binding);
  CHECK(g_name_calls == 2);
  CHECK(g_module_calls == 2);
  CHECK(g_param_calls == 4);
  CHECK(g_global_calls == 4);
  CHECK(g_context_calls == 2);

  SRWLOCK legacy_lock = SRWLOCK_INIT;
  std::unordered_map<void*, bool> legacy_cache{{kTarget, true},
                                                {kOther, false}};
  const auto run_legacy = [&] {
    std::atomic<size_t> legacy_hits{0};
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    for (size_t worker = 0; worker < kThreads; ++worker) {
      workers.emplace_back([&] {
        size_t local_hits = 0;
        for (size_t index = 0; index < kLaunches / kThreads; ++index) {
          // V3.1 loaded both gates before taking the lock and consulting its map.
          local_hits +=
              g_mode.load(std::memory_order_acquire) == StabilityMode::kTemporal
                  ? 0u
                  : 1u;
          local_hits += g_provider_authorized.load(std::memory_order_acquire)
                            ? 0u
                            : 1u;
          AcquireSRWLockExclusive(&legacy_lock);
          local_hits += legacy_cache.find(kOther)->second ? 1u : 0u;
          ReleaseSRWLockExclusive(&legacy_lock);
        }
        legacy_hits.fetch_add(local_hits, std::memory_order_relaxed);
      });
    }
    for (auto& worker : workers) worker.join();
    return std::chrono::duration<double, std::nano>(
               std::chrono::steady_clock::now() - start).count() /
           kLaunches;
  };
  std::array<double, 5> fast_samples{};
  std::array<double, 5> legacy_samples{};
  std::array<double, 5> ratios{};
  for (size_t sample = 0; sample < ratios.size(); ++sample) {
    fast_samples[sample] = run_fast();
    legacy_samples[sample] = run_legacy();
    ratios[sample] = fast_samples[sample] / legacy_samples[sample];
  }
  std::sort(fast_samples.begin(), fast_samples.end());
  std::sort(legacy_samples.begin(), legacy_samples.end());
  std::sort(ratios.begin(), ratios.end());
  const double fast_ns = fast_samples[fast_samples.size() / 2];
  const double legacy_ns = legacy_samples[legacy_samples.size() / 2];
  const double median_ratio = ratios[ratios.size() / 2];
  std::cout << "non-target PrepareLaunch: " << fast_ns
            << " ns, V3.1 lock/map model: " << legacy_ns << " ns, reduction "
            << (1.0 - median_ratio) * 100.0 << "% (median of 5x1M)\n";
  CHECK(median_ratio <= 0.20);

  // A complete 4x geometry/inpaint sequence activates both histories. Phase
  // comes only from the validated 144-byte geometry launch and is consumed by
  // the following 152-byte OutputPull launch on the same stream.
  ResetStateForTests();
  g_alloc_calls = 0;
  g_memset_calls = 0;
  g_async_copy_calls = 0;
  g_last_inpaint_control = {};
  parameters.width = 16;
  parameters.height = 16;
  inpaint_parameters.width = 16;
  inpaint_parameters.height = 16;
  mfgunlock::framecount::g_effective_request_seen.store(
      true, std::memory_order_relaxed);
  mfgunlock::framecount::g_last_effective_generated.store(
      3, std::memory_order_relaxed);
  for (const uint32_t index : {1u, 2u, 3u, 1u, 2u, 3u, 1u}) {
    parameters.t = static_cast<float>(index) / 4.0f;
    PrepareLaunch(LaunchApi::kKernel, kTarget, nullptr, kernel_params, nullptr);
    PrepareLaunch(LaunchApi::kKernel, kInpaint, nullptr,
                  inpaint_kernel_params, nullptr);
  }
  CHECK(g_state.active);
  CHECK(g_state.inpaint_initialized);
  CHECK(g_temporal_active.load(std::memory_order_acquire));
  CHECK(g_inpaint_temporal_active.load(std::memory_order_acquire));
  CHECK(g_alloc_calls == 1);
  CHECK(g_state.arena_capacity == kArenaLimit);
  CHECK(g_history_bytes.load(std::memory_order_relaxed) == 1024u);
  CHECK(g_inpaint_history_bytes.load(std::memory_order_relaxed) == 1024u);
  CHECK(g_memset_calls == 2);
  CHECK(g_last_inpaint_control.enabled == 1);
  CHECK(g_last_inpaint_control.width == 16);
  CHECK(g_last_inpaint_control.height == 16);
  CHECK(g_last_inpaint_control.multiplier == 4);
  CHECK(g_last_inpaint_control.bucket == 0);
  CHECK(g_last_inpaint_control.direction == 0);

  // Two geometry phases without an intervening OutputPull are ambiguous.
  // The inpaint control is synchronously disabled before the second launch.
  parameters.t = 0.5f;
  PrepareLaunch(LaunchApi::kKernel, kTarget, nullptr, kernel_params, nullptr);
  parameters.t = 0.75f;
  PrepareLaunch(LaunchApi::kKernel, kTarget, nullptr, kernel_params, nullptr);
  CHECK(g_state.inpaint_permanently_local);
  CHECK(!g_inpaint_temporal_active.load(std::memory_order_acquire));
  CHECK(g_last_inpaint_control.enabled == 0);

  // A failed 96 MiB allocation retries the existing 64 MiB geometry budget.
  // Temporal inpaint falls back first while temporal geometry remains active.
  ResetStateForTests();
  g_fail_96_mib = true;
  g_alloc_calls = 0;
  for (const uint32_t index : {1u, 2u, 3u, 1u, 2u, 3u, 1u}) {
    parameters.t = static_cast<float>(index) / 4.0f;
    PrepareLaunch(LaunchApi::kKernel, kTarget, nullptr, kernel_params, nullptr);
    PrepareLaunch(LaunchApi::kKernel, kInpaint, nullptr,
                  inpaint_kernel_params, nullptr);
  }
  CHECK(g_state.active);
  CHECK(g_state.inpaint_permanently_local);
  CHECK(g_state.arena_capacity == kHistoryLimit);
  CHECK(g_alloc_calls == 2);
  CHECK(g_temporal_active.load(std::memory_order_acquire));
  CHECK(!g_inpaint_temporal_active.load(std::memory_order_acquire));
  g_fail_96_mib = false;

  // Every CUDA launch entry point must forward unchanged while Local Stable
  // is selected; no provider or CUDA state is needed for this pass-through.
  g_mode.store(StabilityMode::kLocal, std::memory_order_relaxed);
  g_inpaint_mode.store(InpaintMode::kV2Compatibility,
                       std::memory_order_relaxed);
  g_real_launch = FakeLaunch;
  g_real_launch_ptsz = FakeLaunch;
  g_real_launch_ex = FakeLaunchEx;
  g_real_launch_ex_ptsz = FakeLaunchEx;
  CHECK(HookedLaunch(nullptr, 1, 1, 1, 1, 1, 1, 0, nullptr, nullptr,
                     nullptr) == 17);
  CHECK(HookedLaunchPtsz(nullptr, 1, 1, 1, 1, 1, 1, 0, nullptr, nullptr,
                         nullptr) == 17);
  CUlaunchConfig config{};
  CHECK(HookedLaunchEx(&config, nullptr, nullptr, nullptr) == 23);
  CHECK(HookedLaunchExPtsz(&config, nullptr, nullptr, nullptr) == 23);
  CHECK(g_launch_calls == 2);
  CHECK(g_launch_ex_calls == 2);

  std::cout << "CUDA temporal stability tests passed\n";
  return EXIT_SUCCESS;
}
