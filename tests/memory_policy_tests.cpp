// SPDX-License-Identifier: MIT
#include <cstdlib>
#include <iostream>

#include "../src/addons/mfgunlock/memory_policy.hpp"

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition      \
                << '\n';                                                       \
      return EXIT_FAILURE;                                                      \
    }                                                                           \
  } while (false)

int main() {
  using namespace mfgunlock::memorypolicy;
  CHECK(SupportsDxgiBudget(GraphicsApi::kD3D11));
  CHECK(SupportsDxgiBudget(GraphicsApi::kD3D12));
  CHECK(!SupportsDxgiBudget(GraphicsApi::kVulkan));
  CHECK(!SupportsDxgiBudget(GraphicsApi::kUnknown));
  EstimateInputs input{};
  CHECK(BuildEstimatePlan(input).readiness ==
        EstimateReadiness::kMissingOptions);

  input.options_valid = true;
  input.mode = static_cast<uint32_t>(sl::DLSSGMode::eOn);
  input.generated_frames = 3;
  input.back_buffers = 3;
  input.swapchain = {2560, 1440, 24,
                     static_cast<uint32_t>(sl::eValidUntilPresent), true};
  input.motion = {1708, 960, 34,
                  static_cast<uint32_t>(sl::eValidUntilPresent), true};
  input.depth = {1708, 960, 19,
                 static_cast<uint32_t>(sl::eValidUntilPresent), true};
  auto plan = BuildEstimatePlan(input);
  CHECK(plan.readiness == EstimateReadiness::kReady);
  CHECK(plan.options.colorWidth == 2560 && plan.options.colorHeight == 1440);
  CHECK(plan.options.mvecDepthWidth == 1708 &&
        plan.options.mvecDepthHeight == 960);
  CHECK(plan.options.mvecBufferFormat == 34 &&
        plan.options.depthBufferFormat == 19);
  CHECK(plan.volatile_inputs == 0);

  input.ui_recomposition = 1;
  plan = BuildEstimatePlan(input);
  CHECK(plan.readiness == EstimateReadiness::kMissingUiInputs);
  input.hudless = {2560, 1440, 24,
                   static_cast<uint32_t>(sl::eValidUntilPresent), true};
  input.ui = {2560, 1440, 29,
             static_cast<uint32_t>(sl::eOnlyValidNow), true};
  const auto ui_plan = BuildEstimatePlan(input);
  CHECK(ui_plan.readiness == EstimateReadiness::kReady);
  CHECK(ui_plan.ui_recomposition && ui_plan.volatile_inputs == 1);
  CHECK(ui_plan.signature != plan.signature);

  input.ui_recomposition = 0;
  const auto native_plan = BuildEstimatePlan(input);
  CHECK(native_plan.comparison_signature == ui_plan.comparison_signature);
  CHECK(native_plan.signature != ui_plan.signature);
  CHECK(native_plan.volatile_inputs == 0);

  const uint64_t signature = native_plan.signature;
  input.flags |= static_cast<uint32_t>(sl::DLSSGFlags::eRequestVRAMEstimate);
  CHECK(BuildEstimatePlan(input).signature == signature);

  std::cout << "memory policy tests passed\n";
  return EXIT_SUCCESS;
}
