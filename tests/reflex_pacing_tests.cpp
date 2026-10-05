#include <cstdlib>
#include <iostream>

#include "../src/addons/mfgunlock/reflex_pacing.hpp"

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition \
                << '\n';                                                    \
      return EXIT_FAILURE;                                                  \
    }                                                                       \
  } while (false)

namespace {

struct TestFrameToken final : sl::FrameToken {
  explicit TestFrameToken(uint32_t value) : value_(value) {}
  operator uint32_t() const override { return value_; }
  uint32_t value_;
};

uint32_t g_native_calls = 0;
sl::Result NativeSleep(const sl::FrameToken&) {
  ++g_native_calls;
  return sl::Result::eOk;
}

}  // namespace

int main() {
  using namespace mfgunlock;
  using namespace mfgunlock::reflexpacing;

  g_native_calls = 0;
  g_pacing_method.store(
      static_cast<uint32_t>(pacing::ReflexPacingMethod::kDxgiWaitable));
  g_mfg_multiplier.store(3);
  HANDLE signaled = CreateEventW(nullptr, TRUE, TRUE, nullptr);
  CHECK(signaled != nullptr);
  PublishWaitable(signaled, 7, 8, 3);
  for (uint32_t i = 1; i <= 120; ++i) {
    TestFrameToken token(i);
    CHECK(HandleSleep(token, &NativeSleep) == sl::Result::eOk);
  }
  CHECK(g_native_calls == 120);
  CHECK(g_probe_samples.load() == 120);
  CHECK(g_waitable_state.load() == WaitableState::kActivationPending);
  MarkActivationResult(7, true);
  CHECK(g_waitable_state.load() == WaitableState::kActive);
  CHECK(g_effective_maximum_latency.load() == 1);
  TestFrameToken active_token(121);
  CHECK(HandleSleep(active_token, &NativeSleep) == sl::Result::eOk);
  CHECK(g_native_calls == 120);  // DXGI signal replaced native sleep.
  CHECK(RetireWaitable(FallbackReason::kNotRequested) == signaled);
  CloseHandle(signaled);

  HANDLE duplicate_test = CreateEventW(nullptr, TRUE, TRUE, nullptr);
  CHECK(duplicate_test != nullptr);
  PublishWaitable(duplicate_test, 8, 8, 1);
  TestFrameToken first(42);
  TestFrameToken duplicate(42);
  CHECK(HandleSleep(first, &NativeSleep) == sl::Result::eOk);
  CHECK(HandleSleep(duplicate, &NativeSleep) == sl::Result::eOk);
  CHECK(g_waitable_state.load() == WaitableState::kFallback);
  CHECK(g_fallback_reason.load() == FallbackReason::kTokenDuplicate);
  CHECK(RetireWaitable(FallbackReason::kTokenDuplicate) == duplicate_test);
  CloseHandle(duplicate_test);

  HANDLE timeout_test = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(timeout_test != nullptr);
  PublishWaitable(timeout_test, 9, 8, 1);
  for (uint32_t i = 1; i <= 120; ++i) {
    TestFrameToken token(i);
    CHECK(HandleSleep(token, &NativeSleep) == sl::Result::eOk);
  }
  MarkActivationResult(9, true);
  const uint32_t calls_before_timeout = g_native_calls;
  TestFrameToken timeout_token(121);
  CHECK(HandleSleep(timeout_token, &NativeSleep) == sl::Result::eOk);
  CHECK(g_native_calls == calls_before_timeout + 1);
  CHECK(g_waitable_state.load() == WaitableState::kFallback);
  CHECK(g_fallback_reason.load() == FallbackReason::kWaitTimeout);
  CHECK(g_timeout_count.load() == 1);
  CHECK(RetireWaitable(FallbackReason::kWaitTimeout) == timeout_test);
  CloseHandle(timeout_test);

  SampleRing<8> ring;
  ring.Add(100);
  ring.Add(200);
  ring.Add(400);
  const auto stats = ring.Stats();
  CHECK(stats.count == 3);
  CHECK(stats.median_us == 200);
  CHECK(stats.p95_us == 200);
  CHECK(stats.p99_us == 200);
  CHECK(stats.mean_delta_us == 150);

  std::cout << "reflex pacing tests passed\n";
  return EXIT_SUCCESS;
}
