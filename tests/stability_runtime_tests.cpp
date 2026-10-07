#include <cstdlib>
#include <iostream>
#include <chrono>
#include "../src/addons/mfgunlock/framecount.hpp"
#include "../src/addons/mfgunlock/adaptive_quality_v3.hpp"

// Header-only ReShade logging resolves this test export, never a live addon.
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
#define CHECK(x) do { if (!(x)) { std::cerr << "FAILED " << __LINE__ << ": " #x "\n"; return EXIT_FAILURE; } } while (false)
struct Token : sl::FrameToken {
  uint32_t value;
  explicit Token(uint32_t v) : value(v) {}
  operator uint32_t() const override { return value; }
};
sl::Resource Texture(uint32_t format) {
  sl::Resource resource(sl::ResourceType::eTex2d, reinterpret_cast<void*>(uintptr_t{1}), 0);
  resource.width = 2560;
  resource.height = 1440;
  resource.nativeFormat = format;
  return resource;
}
bool reject_constants = false;
bool reject_options = false;
sl::Boolean received_reset = sl::Boolean::eInvalid;
sl::Result FakeConstants(const sl::Constants& values, const sl::FrameToken&, const sl::ViewportHandle&) {
  received_reset = values.reset;
  return reject_constants ? sl::Result::eErrorInvalidParameter : sl::Result::eOk;
}
sl::Result FakeOptions(const sl::ViewportHandle&, const sl::DLSSGOptions&) {
  return reject_options ? sl::Result::eErrorInvalidParameter : sl::Result::eOk;
}
int main() {
  using namespace mfgunlock;
  namespace fc = framecount;
  namespace aq = adaptivequalityv3;
  reshade::internal::get_reshade_module_handle(GetModuleHandleW(nullptr));
  fc::g_addon_enabled = true;
  fc::g_hdr_compatibility_mode = static_cast<unsigned>(fc::HdrCompatibilityMode::kAutomaticHybrid);
  fc::g_hdr_active = false;
  fc::g_format_api = qualityguard::FormatApi::kDxgi;
  const sl::ViewportHandle viewport{0u};
  auto* state = fc::internal::GetQualityState(viewport);
  CHECK(state != nullptr);
  sl::DLSSGOptions options{};
  options.mode = sl::DLSSGMode::eOn;
  options.colorWidth = 2560;
  options.colorHeight = 1440;
  options.colorBufferFormat = 28;
  fc::internal::ObserveOptionsTransition(viewport, options, 2);
  const uint64_t revision = state->options_revision.load();
  // Identical options must not wait behind a tag update or mutate the snapshot.
  AcquireSRWLockExclusive(&state->lock);
  fc::internal::ObserveOptionsTransition(viewport, options, 2);
  ReleaseSRWLockExclusive(&state->lock);
  CHECK(state->options_revision == revision);
  const auto begin = std::chrono::steady_clock::now();
  for (unsigned i = 0; i < 1000000; ++i)
    fc::internal::ObserveOptionsTransition(viewport, options, 2);
  CHECK(state->options_revision == revision);
  std::cout << "1M unchanged option observations: "
      << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count()
      << " us; no writer revisions\n";
  options.colorWidth = 1920;
  const auto changed_resets = state->reset_requested.load();
  fc::internal::ObserveOptionsTransition(viewport, options, 2);
  CHECK(state->options_revision == revision + 2 && state->reset_requested == changed_resets + 1);
  options.colorWidth = 2560;
  fc::internal::ObserveOptionsTransition(viewport, options, 2);

  auto hud = Texture(28);
  auto ui = Texture(61);
  sl::ResourceTag live[] = {{&hud, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent},
      {&ui, sl::kBufferTypeUIAlpha, sl::ResourceLifecycle::eValidUntilPresent}};
  sl::ResourceTag clear[] = {{nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent},
      {nullptr, sl::kBufferTypeUIAlpha, sl::ResourceLifecycle::eValidUntilPresent}};
  bool suppressed = false;
  sl::Result next_result = sl::Result::eOk;
  auto submit = [&](const sl::ResourceTag* actual) {
    suppressed = actual[0].resource == nullptr && actual[1].resource == nullptr;
    return next_result;
  };
  Token t1{1}, t2{2}, t3{3};
  const auto resets = state->reset_requested.load();
  CHECK(fc::internal::FilterHudSeparationTags(viewport, live, 2, submit, &t1) == sl::Result::eOk);
  CHECK(!suppressed && state->last_forwarded_input_path == 1);
  CHECK(fc::internal::FilterHudSeparationTags(viewport, clear, 2, submit, &t1) == sl::Result::eOk);
  CHECK(state->reset_requested == resets);
  CHECK(fc::internal::FilterHudSeparationTags(viewport, live, 2, submit, &t2) == sl::Result::eOk);
  CHECK(!suppressed && state->reset_requested == resets);

  // Contention changes the actual submitted path, even if state metadata was
  // not writable. Both transitions must invalidate history exactly once.
  AcquireSRWLockExclusive(&state->lock);
  const auto contention_result = fc::internal::FilterHudSeparationTags(viewport, live, 2, submit, &t2);
  ReleaseSRWLockExclusive(&state->lock);
  CHECK(contention_result == sl::Result::eOk && suppressed);
  CHECK(state->last_forwarded_input_path == 2 && state->reset_requested == resets + 1);
  CHECK(fc::internal::FilterHudSeparationTags(viewport, live, 2, submit, &t3) == sl::Result::eOk);
  CHECK(!suppressed && state->reset_requested == resets + 2);

  // Failed APIs cannot certify a new forwarded path. A later successful full
  // pair is allowed to revalidate it without using caller-owned descriptors.
  fc::g_hdr_active = true;
  next_result = sl::Result::eErrorInvalidParameter;
  CHECK(fc::internal::FilterHudSeparationTags(viewport, live, 2, submit, &t3) == next_result);
  CHECK(state->last_forwarded_input_path == 1 && state->reset_requested == resets + 2);
  next_result = sl::Result::eOk;
  CHECK(fc::internal::FilterHudSeparationTags(viewport, live, 2, submit, &t3) == sl::Result::eOk);
  CHECK(state->last_forwarded_input_path == 2 && state->reset_requested == resets + 3);
  CHECK(fc::internal::FilterHudSeparationTags(viewport, clear, 2, submit, &t3) == sl::Result::eOk);
  CHECK(state->reset_requested == resets + 3);
  fc::g_hdr_compatibility_mode = static_cast<unsigned>(fc::HdrCompatibilityMode::kNative);
  CHECK(fc::internal::FilterHudSeparationTags(viewport, live, 2, submit, &t3) == sl::Result::eOk);
  CHECK(!suppressed && state->reset_requested == resets + 3);

  fc::internal::g_real_set_constants = FakeConstants;
  sl::Constants constants{};
  constants.reset = sl::Boolean::eFalse;
  const auto pending = state->reset_requested.load();
  reject_constants = true;
  CHECK(fc::internal::HookedSetConstants(constants, t3, viewport) == sl::Result::eErrorInvalidParameter);
  CHECK(received_reset == sl::Boolean::eTrue && state->reset_applied != pending);
  reject_constants = false;
  CHECK(fc::internal::HookedSetConstants(constants, t3, viewport) == sl::Result::eOk);
  CHECK(received_reset == sl::Boolean::eTrue && state->reset_applied == pending);
  CHECK(constants.reset == sl::Boolean::eFalse);
  CHECK(fc::internal::HookedSetConstants(constants, t3, viewport) == sl::Result::eOk);
  CHECK(received_reset == sl::Boolean::eFalse);
  fc::internal::g_real_set_options = FakeOptions;
  const auto committed_revision = state->options_revision.load();
  const auto committed_resets = state->reset_requested.load();
  options.colorWidth = 1920;
  reject_options = true;
  CHECK(fc::internal::CallSetOptions(viewport, options) == sl::Result::eErrorInvalidParameter);
  CHECK(state->options_revision == committed_revision && state->reset_requested == committed_resets);
  reject_options = false;
  CHECK(fc::internal::CallSetOptions(viewport, options) == sl::Result::eOk);
  CHECK(state->color_width == 1920 && state->options_revision == committed_revision + 2);

  fc::g_force_multiplier = 0;
  fc::g_dynamic_applied = false;
  fc::g_reflex_source_fps_cap = 120;
  fc::g_effective_request_seen = true;
  fc::g_cap_driver_multiplier = 4;
  fc::g_cap_driver_observed_ms = GetTickCount64();
  CHECK(fc::internal::UserSourceCapRequested());
  fc::g_cap_driver_observed_ms = GetTickCount64() - 2000;
  CHECK(!fc::internal::UserSourceCapRequested());
  fc::g_cap_driver_observed_ms = GetTickCount64();
  fc::g_effective_request_seen = false;
  CHECK(!fc::internal::UserSourceCapRequested());
  fc::g_cap_driver_multiplier = 1;
  fc::g_effective_request_seen = true;
  CHECK(!fc::internal::UserSourceCapRequested());
  fc::g_force_multiplier = 4;
  CHECK(fc::internal::UserSourceCapRequested());
  reflexpacing::g_mode_override = static_cast<unsigned>(pacing::ReflexModeOverride::kOff);
  CHECK(fc::internal::ResolveUserSourceCapState().status == fc::internal::UserSourceCapStatus::kSleepNotNative);
  reflexpacing::g_mode_override = 0;
  reflexpacing::g_waitable_state = reflexpacing::WaitableState::kActive;
  CHECK(fc::internal::ResolveUserSourceCapState().status == fc::internal::UserSourceCapStatus::kSleepNotNative);
  reflexpacing::g_waitable_state = reflexpacing::WaitableState::kNative;
  fc::g_reflex_source_fps_cap = 0;
  CHECK(fc::internal::ResolveUserSourceCapState().status == fc::internal::UserSourceCapStatus::kOff);

  // Slow motion crossing 0.5 px at all four edges has no confidence jump.
  for (int edge = 0; edge < 4; ++edge) {
    auto taper = [&](float motion) {
      float u = 0.5f, v = 0.5f, cu = u, cv = v;
      if (edge < 2) { u = edge == 0 ? 0.0015f : 0.9985f; cu = u + (edge == 0 ? -motion : motion) / 1000.f; }
      else { v = edge == 2 ? 0.0015f : 0.9985f; cv = v + (edge == 2 ? -motion : motion) / 1000.f; }
      return aq::BorderConfidence(aq::ContinuousDirectionalBorderDistance(u, v, cu, cv, 1000, 1000));
    };
    CHECK(std::abs(taper(0.4999f) - taper(0.5001f)) < 0.0002f);
  }
  for (float motion : {0.f, 0.49f, 0.5f, 0.51f, 1.f, 1.5f, 4.f}) {
    const auto distance = aq::ContinuousDirectionalBorderDistance(.0015f, .5f,
        .0015f - motion / 1000.f, .5f, 1000, 1000);
    const auto weight = aq::TaperAddedWeight(.3f, .9f, aq::BorderConfidence(distance));
    CHECK(weight >= .3f && weight <= .9f);
  }
  std::cout << "tag lifetime, actual forwarding, failed API, cap and subpixel tests passed\n";
}
