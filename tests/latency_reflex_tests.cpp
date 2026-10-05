// SPDX-License-Identifier: MIT
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <vector>
#include "../src/addons/mfgunlock/framecount.hpp"
#define CHECK(x) do { if (!(x)) { std::cerr << "failed " << __LINE__ << ": " #x "\n"; return 1; } } while(0)
namespace fc = mfgunlock::framecount;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
std::vector<uint32_t> calls;
std::vector<uint32_t> mode_calls;
bool reject_override = false;
bool reject_mode_override = false;
sl::Result Reflex(const sl::ReflexOptions& o) {
  calls.push_back(o.frameLimitUs);
  mode_calls.push_back(static_cast<uint32_t>(o.mode));
  return (reject_override && o.frameLimitUs != 0) ||
                 (reject_mode_override &&
                  o.mode != sl::ReflexMode::eOff)
             ? sl::Result::eErrorInvalidState
             : sl::Result::eOk;
}
sl::Result Reentrant(const sl::ReflexOptions& o) { return fc::internal::HookedReflexSetOptions(o); }
uint32_t native_sleep_calls = 0;
sl::Result NativeFrameSleep(const sl::FrameToken&) {
  ++native_sleep_calls;
  return sl::Result::eOk;
}
struct Frame : sl::FrameToken {
  uint32_t value;
  explicit Frame(uint32_t v):value(v) {}
  operator uint32_t() const override { return value; }
};
uint32_t nonnull = 0;
sl::Result Tags(const sl::FrameToken&, const sl::ViewportHandle&, const sl::ResourceTag* t,
                 uint32_t n, sl::CommandBuffer*) {
  nonnull=0; for(uint32_t i=0;i<n;++i) nonnull += t[i].resource != nullptr;
  return sl::Result::eOk;
}
int main() {
  reshade::internal::get_reshade_module_handle(GetModuleHandleW(nullptr));
  fc::internal::g_real_reflex_set_options.store(&Reflex);
  fc::g_latency_guard_mode.store(1);
  sl::ReflexOptions native;
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk);
  CHECK(calls.size()==1);
  fc::internal::RefreshReflexTarget(); fc::internal::RefreshReflexTarget(); CHECK(calls.size()==1);
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk); CHECK(calls.size()==2);
  fc::internal::g_reflex_owner_thread.store(GetCurrentThreadId()+1);
  fc::internal::RefreshReflexTarget(); CHECK(calls.size()==2 && fc::g_latency_guard_refresh_pending.load());
  fc::internal::g_reflex_owner_thread.store(GetCurrentThreadId());
  fc::g_latency_guard_mode.store(2); fc::g_latency_guard_auto_cap_ready.store(true);
  fc::g_latency_guard_live_multiplier.store(4);
  fc::g_latency_guard_active_source_cap_fps.store(97);
  reject_override=true; calls.clear();
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk);
  CHECK(calls.size()==2 && calls[0]==2577 && calls[1]==0);
  CHECK(!fc::g_reflex_limit_applied.load() && !fc::g_latency_guard_auto_cap_ready.load());

  // The user value is the final/output FPS ceiling understood by Reflex and
  // DLSS-G. It is not multiplied again by the selected MFG multiplier, and a
  // stricter native game limit is never relaxed.
  reject_override=false; calls.clear();
  fc::g_force_multiplier.store(4);
  fc::g_latency_guard_live_multiplier.store(4);
  fc::g_effective_request_seen.store(true);
  fc::g_last_effective_generated.store(3);
  fc::g_dynamic_applied.store(false);
  fc::g_dynamic_mfg_enabled.store(false);
  fc::g_reflex_source_fps_cap.store(120);
  native.frameLimitUs=0;
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk);
  CHECK(calls.size()==1 && calls[0]==8333);
  CHECK(fc::g_reflex_limit_source.load()==1 &&
        fc::internal::UserSourceCapReady());
  CHECK(fc::internal::ResolveUserSourceCapState().status ==
        fc::internal::UserSourceCapStatus::kActive);

  calls.clear(); native.frameLimitUs=10000;
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk);
  CHECK(calls.size()==1 && calls[0]==10000);
  CHECK(fc::g_reflex_limit_source.load()==1 &&
        fc::internal::UserSourceCapReady());
  CHECK(fc::internal::ResolveUserSourceCapState().status ==
        fc::internal::UserSourceCapStatus::kNativeLimitStricter);

  // The complete 6x -> 5x -> 4x -> 3x trial keeps the same final/output target.
  // Refresh therefore detects identical forwarded options and does not call
  // Reflex again for any multiplier step.
  calls.clear(); native.frameLimitUs=0;
  fc::g_force_multiplier.store(6);
  fc::g_latency_guard_live_multiplier.store(6);
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk);
  CHECK(calls.size()==1 && calls[0]==8333);
  for (uint32_t multiplier : {5u, 4u, 3u}) {
    fc::g_latency_guard_live_multiplier.store(multiplier);
    fc::internal::RefreshReflexTarget();
    CHECK(calls.size()==1);
  }

  // Game-controlled fixed MFG is intentionally excluded; active Dynamic uses
  // the same final/output cap independently of its scheduler target.
  calls.clear();
  fc::g_force_multiplier.store(0);
  fc::g_dynamic_mfg_enabled.store(false);
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk);
  CHECK(calls.size()==1 && calls[0]==0 &&
        fc::g_reflex_limit_source.load()==0);
  CHECK(fc::internal::ResolveUserSourceCapState().status ==
        fc::internal::UserSourceCapStatus::kInactiveGameControlled);
  calls.clear();
  fc::g_dynamic_mfg_enabled.store(true);
  CHECK(fc::internal::ResolveUserSourceCapState().status ==
        fc::internal::UserSourceCapStatus::kWaitingForDynamic);
  fc::g_dynamic_applied.store(true);
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk);
  CHECK(calls.size()==1 && calls[0]==8333 &&
        fc::g_reflex_limit_source.load()==1);

  // Rejection retries the native options and remains visible in the shared
  // status resolver instead of being mislabeled as merely pending.
  calls.clear(); reject_override=true;
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk);
  CHECK(calls.size()==2 && calls[0]==8333 && calls[1]==0);
  CHECK(fc::internal::ResolveUserSourceCapState().status ==
        fc::internal::UserSourceCapStatus::kRejected);
  reject_override=false;
  fc::g_dynamic_applied.store(false);
  fc::g_dynamic_mfg_enabled.store(false);
  fc::g_reflex_source_fps_cap.store(0);

  // Mode override changes only ReflexOptions::mode. Every other native field
  // survives byte-for-byte, and rejection retries the exact native options.
  native = {};
  native.mode = sl::ReflexMode::eOff;
  native.frameLimitUs = 0;
  native.useMarkersToOptimize = true;
  native.virtualKey = VK_F13;
  native.idThread = 0x12345678u;
  mfgunlock::reflexpacing::g_mode_override.store(
      static_cast<uint32_t>(
          mfgunlock::pacing::ReflexModeOverride::kOnBoost));
  calls.clear(); mode_calls.clear();
  CHECK(fc::internal::HookedReflexSetOptions(native) == sl::Result::eOk);
  CHECK(calls.size() == 1 && calls[0] == 0);
  CHECK(mode_calls.size() == 1 &&
        mode_calls[0] ==
            static_cast<uint32_t>(sl::ReflexMode::eLowLatencyWithBoost));
  CHECK(fc::internal::g_last_accepted_reflex_options.useMarkersToOptimize ==
        native.useMarkersToOptimize);
  CHECK(fc::internal::g_last_accepted_reflex_options.virtualKey ==
        native.virtualKey);
  CHECK(fc::internal::g_last_accepted_reflex_options.idThread ==
        native.idThread);
  CHECK(mfgunlock::reflexpacing::g_mode_applied.load());
  reject_mode_override = true;
  mfgunlock::reflexpacing::g_mode_override.store(
      static_cast<uint32_t>(mfgunlock::pacing::ReflexModeOverride::kOn));
  calls.clear(); mode_calls.clear();
  CHECK(fc::internal::HookedReflexSetOptions(native) == sl::Result::eOk);
  CHECK(mode_calls.size() == 2);
  CHECK(mode_calls[0] ==
        static_cast<uint32_t>(sl::ReflexMode::eLowLatency));
  CHECK(mode_calls[1] == static_cast<uint32_t>(sl::ReflexMode::eOff));
  CHECK(mfgunlock::reflexpacing::g_mode_rejected.load());
  reject_mode_override = false;

  // "Off" must not submit Reflex mode Off: doing that disables DLSS-G in
  // several integrations. Keep LowLatency as the internal dependency and
  // bypass only slReflexSleep.
  mfgunlock::reflexpacing::g_mode_override.store(
      static_cast<uint32_t>(mfgunlock::pacing::ReflexModeOverride::kOff));
  calls.clear(); mode_calls.clear();
  CHECK(fc::internal::HookedReflexSetOptions(native) == sl::Result::eOk);
  CHECK(mode_calls.size() == 1);
  CHECK(mode_calls[0] ==
        static_cast<uint32_t>(sl::ReflexMode::eLowLatency));
  fc::internal::g_real_reflex_sleep.store(&NativeFrameSleep);
  mfgunlock::reflexpacing::g_sleep_dispatch.store(
      mfgunlock::reflexpacing::SleepDispatch::kBypass);
  Frame bypass_frame(98);
  native_sleep_calls = 0;
  CHECK(fc::internal::HookedReflexSleep(bypass_frame) == sl::Result::eOk);
  CHECK(native_sleep_calls == 0);

  mfgunlock::reflexpacing::g_mode_override.store(
      static_cast<uint32_t>(mfgunlock::pacing::ReflexModeOverride::kGame));

  // Native/default slReflexSleep dispatch remains a single atomic branch and
  // trampoline: no timing query, lock, allocation or waitable-object call.
  fc::internal::g_real_reflex_sleep.store(&NativeFrameSleep);
  mfgunlock::reflexpacing::g_sleep_dispatch.store(
      mfgunlock::reflexpacing::SleepDispatch::kNative);
  std::array<double, 21> native_ns{};
  volatile uint32_t sleep_checksum = 0;
  Frame benchmark_frame(99);
  for (double& sample : native_ns) {
    const auto begin = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < 100000; ++i)
      sleep_checksum += static_cast<uint32_t>(
          fc::internal::HookedReflexSleep(benchmark_frame));
    const auto end = std::chrono::steady_clock::now();
    sample = std::chrono::duration<double, std::nano>(end - begin).count() /
             100000.0;
  }
  std::sort(native_ns.begin(), native_ns.end());
  std::cout << "native slReflexSleep wrapper: median " << native_ns[10]
            << " ns, p95 " << native_ns[19] << " ns\n";
  CHECK(native_ns[10] <= 50.0);
  CHECK(native_ns[19] <= 100.0);
  (void)sleep_checksum;

  // Unknown extension must reach native exactly once, never be retained/replayed.
  sl::ReflexOptions extension; native.next=&extension; calls.clear();
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eOk);
  fc::internal::RefreshReflexTarget(); CHECK(calls.size()==1 && !fc::g_reflex_options_seen.load());
  native.next=nullptr; fc::internal::g_real_reflex_set_options.store(&Reentrant);
  CHECK(fc::internal::HookedReflexSetOptions(native)==sl::Result::eErrorInvalidState);

  fc::g_hdr_compatibility_mode.store(unsigned(fc::HdrCompatibilityMode::kAutomaticHybrid));
  fc::g_format_api.store(mfgunlock::qualityguard::FormatApi::kDxgi);
  fc::g_hdr_active.store(false);
  fc::internal::g_real_set_tag_for_frame=&Tags;
  sl::Resource color(sl::ResourceType::eTex2d, reinterpret_cast<void*>(1), 0);
  color.width=1920; color.height=1080; color.nativeFormat=28;
  sl::ResourceTag pair[] = {
    {&color,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent},
    {&color,sl::kBufferTypeUIColorAndAlpha,sl::ResourceLifecycle::eValidUntilPresent}};
  sl::ViewportHandle viewport(3); Frame f1(1), f2(2), f3(3), f4(4);
  CHECK(fc::internal::HookedSetTagForFrame(f1,viewport,pair,2,nullptr)==sl::Result::eOk && nonnull==2);
  const auto resets=fc::g_quality_resets_requested.load();
  CHECK(fc::internal::HookedSetTagForFrame(f2,viewport,pair,2,nullptr)==sl::Result::eOk && nonnull==2);
  CHECK(fc::g_quality_resets_requested.load()==resets); // no per-frame history reset
  CHECK(fc::internal::HookedSetTagForFrame(f3,viewport,pair,1,nullptr)==sl::Result::eOk && nonnull==0);
  CHECK(fc::internal::HookedSetTagForFrame(f4,viewport,pair+1,1,nullptr)==sl::Result::eOk && nonnull==0);

  // A simultaneous state transition must never block the tag submission
  // thread. Optional HUD inputs fail closed while required inputs are left
  // untouched, and the fallback is visible in diagnostics.
  auto* quality_state = fc::internal::GetQualityState(viewport);
  CHECK(quality_state != nullptr);
  sl::ResourceTag contended_tags[] = {
    pair[0], pair[1],
    {&color, sl::kBufferTypeMotionVectors,
     sl::ResourceLifecycle::eValidUntilPresent}};
  const auto contentions = fc::g_quality_tag_lock_contentions.load();
  AcquireSRWLockExclusive(&quality_state->lock);
  CHECK(fc::internal::HookedSetTagForFrame(
            f4, viewport, contended_tags, 3, nullptr) == sl::Result::eOk &&
        nonnull == 1);
  ReleaseSRWLockExclusive(&quality_state->lock);
  CHECK(fc::g_quality_tag_lock_contentions.load() > contentions);

  // More than the old eight-viewport limit must be tracked. The new bound is
  // still finite and fails closed once genuinely exhausted.
  for (uint32_t key = 100; key < 109; ++key)
    CHECK(fc::internal::GetQualityState(sl::ViewportHandle(key)) != nullptr);
  CHECK(fc::g_quality_viewport_count.load() > 8);
  uint32_t next_key = 1000;
  while (fc::g_quality_viewport_count.load() <
         fc::kMaxQualityViewports) {
    CHECK(fc::internal::GetQualityState(sl::ViewportHandle(next_key++)) !=
          nullptr);
  }
  CHECK(fc::internal::GetQualityState(sl::ViewportHandle(next_key)) == nullptr);
  CHECK(fc::g_quality_viewport_capacity_exhausted.load());
  std::cout << "Reflex replay and HUD frame-lifetime tests passed\n";
}
