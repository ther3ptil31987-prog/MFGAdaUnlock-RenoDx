// SPDX-License-Identifier: MIT
// Independent UI-composition preference, using Intel's public SDK contract.
#include "native_internal.hpp"
namespace mfgunlock::xemfg::native {
// Caller holds Output::io at the source-frame Present boundary. The overlay
// only queues preferences; it never changes SDK state from its render callback.
bool ApplyUiComposition(Output& output,bool hudlessAvailable) {
  output.uiCompositionAvailable=hudlessAvailable;
  const bool requested=ControlsSettings().uiComposition;
  const bool desired=requested && hudlessAvailable;
  if(output.uiCompositionKnown && output.uiCompositionApplied==desired) return true;
  const auto result=State().api.xefgSwapChainSetUiCompositionState(output.fg,
      desired?XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_ENABLED:XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_DISABLED);
  const bool log=output.uiCompositionUpdates==0 || output.uiCompositionResult!=result || result>=0;
  output.uiCompositionResult=result; ++output.uiCompositionUpdates;
  // Failed setters never publish the requested state as effective. Skip this
  // generated batch and retry on the next source frame; keep native Present.
  if(result<0) {
    output.uiCompositionKnown=false;
  } else {
    output.uiCompositionApplied=desired; output.uiCompositionKnown=true;
  }
  if(log) xess_mod::WriteLog("native_ui_composition","\"requested\":"+std::to_string(requested)
      +",\"hudless\":"+std::to_string(hudlessAvailable)+",\"enabled\":"+std::to_string(output.uiCompositionApplied)
      +",\"known\":"+std::to_string(output.uiCompositionKnown)+",\"result\":"+std::to_string(result));
  return result>=0;
}
}
