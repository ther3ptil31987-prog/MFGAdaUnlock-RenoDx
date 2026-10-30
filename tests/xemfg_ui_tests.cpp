// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/native_internal.hpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace mfgunlock::xemfg::native;
ControlSettings preferences;
int calls{},failure{};
xefg_swapchain_ui_composition_state_t last{};
void Check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
xefg_swapchain_result_t Set(xefg_swapchain_handle_t,xefg_swapchain_ui_composition_state_t state) {
  ++calls; last=state; return static_cast<xefg_swapchain_result_t>(failure);
}
}
namespace xess_mod { void WriteLog(const char*,const std::string&) noexcept {} }
namespace mfgunlock::xemfg::native {
Bridge& State() { static Bridge value; return value; }
ControlSettings ControlsSettings() { return preferences; }
Output::~Output()=default;
}
int main() {
  try {
    State().api.xefgSwapChainSetUiCompositionState=&Set;
    Output output;
    Check(!preferences.uiComposition,"composition must default off");
    Check(ApplyUiComposition(output,true) && calls==0 && !output.uiCompositionApplied,"default interpolation was overridden");
    preferences.uiComposition=true;
    Check(ApplyUiComposition(output,false) && calls==0,"composition enabled without HUD-less input");
    Check(ApplyUiComposition(output,true) && calls==1 && last==XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_ENABLED,"opt-in not applied");
    Check(ApplyUiComposition(output,true) && calls==1,"identical state resent each frame");
    Check(ApplyUiComposition(output,false) && calls==2 && !output.uiCompositionApplied,"missing HUD-less input did not restore interpolation");
    Check(preferences.uiComposition,"temporary missing input erased preference");
    failure=-4;
    Check(!ApplyUiComposition(output,true) && !output.uiCompositionKnown && !output.uiCompositionApplied,"failed setter published composition as applied");
    failure=0;
    Check(ApplyUiComposition(output,true) && output.uiCompositionKnown && output.uiCompositionApplied,"failed setter was not retried");
    preferences.uiComposition=false;
    Check(ApplyUiComposition(output,true) && !output.uiCompositionApplied,"uncheck did not disable composition");
    output.uiCompositionKnown=false; const auto before=calls;
    Check(ApplyUiComposition(output,true) && calls==before+1 && last==XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_DISABLED,"reset state not reapplied");
    // 1.7 (plan E.2): stabilized frames compose without the UI Composition
    // preference, and their own inputs are not offered as the game's HUD-less.
    Check(ApplyUiComposition(output,true,true) && output.uiCompositionApplied && output.uiStabilized
        && !output.uiCompositionAvailable && last==XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_ENABLED,"stabilized frame not composed");
    Check(ApplyUiComposition(output,false) && !output.uiCompositionApplied && !output.uiStabilized
        && last==XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_DISABLED,"stabilization did not end with its inputs");
    std::cout<<"PASS: default interpolation, opt-in/off, missing inputs, cache, failure/retry, reset and HUD stabilization\n";
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
