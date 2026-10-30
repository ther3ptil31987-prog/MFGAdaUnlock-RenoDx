// SPDX-License-Identifier: MIT
// Independent UI-composition preference, using Intel's public SDK contract.
#include "native_internal.hpp"
namespace mfgunlock::xemfg::native {
// Caller holds Output::io at the source-frame Present boundary. The overlay
// only queues preferences; it never changes SDK state from its render callback.
bool ApplyUiComposition(Output& output,bool hudlessAvailable,bool stabilized) {
  // Stabilized frames carry the addon's own HUD-less and UI inputs; they are
  // not the game's HUD-less color, so UI Composition stays a game-input option.
  output.uiCompositionAvailable=hudlessAvailable && !stabilized;
  output.uiStabilized=stabilized;
  const bool requested=ControlsSettings().uiComposition;
  const bool desired=stabilized || (requested && hudlessAvailable);
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
bool StabilizeHud(Output& o,Frame& frame,uint32_t id) {
  if(o.uiMaskFailed || !UiMask::SupportedFormat(o.format)) return false;
  if(!o.uiMask) {
    auto mask=std::make_unique<UiMask>();
    if(!mask->Initialize(o.device.Get(),o.format)) {
      o.uiMaskFailed=true;
      xess_mod::WriteLog("native_ui_stabilization","\"initialized\":false,\"format\":"+std::to_string(uint32_t(o.format))
          +",\"hr\":"+std::to_string(int32_t(mask->LastError())));
      return false;
    }
    o.uiMask=std::move(mask);
  }
  auto& a=State().api;
  ComPtr<IDXGISwapChain4> chain;
  ComPtr<ID3D12Resource> backbuffer;
  if(a.xefgSwapChainD3D12GetSwapChainPtr(o.fg,IID_PPV_ARGS(&chain))!=0
      || FAILED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&backbuffer)))) return false;
  ID3D12Resource* hudless{}; ID3D12Resource* ui{};
  const auto& held=frame.heldHudless;
  auto* list=o.uiMask->Record(backbuffer.Get(),D3D12_RESOURCE_STATE_PRESENT,held.source.Get(),held.state,held.region,
      &hudless,&ui);
  int tags[2]{XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT,XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT};
  bool ok=false;
  if(list) {
    const auto region=o.backbufferRegion;
    const xefg_swapchain_resource_type_t types[2]{XEFG_SWAPCHAIN_RES_HUDLESS_COLOR,XEFG_SWAPCHAIN_RES_UI};
    ID3D12Resource* resources[2]{hudless,ui};
    for(int i=0;i<2;++i) {
      xefg_swapchain_d3d12_resource_data_t data{};
      data.type=types[i]; data.pResource=resources[i]; data.validity=XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
      data.incomingState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      data.resourceSize={o.width,o.height};
      if(region.width) { data.resourceBase={region.x,region.y}; data.resourceSize={region.width,region.height}; }
      RuntimeCall trace("xefg_tag_resource",o.diagnosticOutput,id);
      tags[i]=a.xefgSwapChainD3D12TagFrameResource(o.fg,list,id,&data); trace.Result(tags[i]);
    }
    ok=o.uiMask->Submit(o.queue.Get()) && tags[0]>=0 && tags[1]>=0; // the open list is always submitted
  }
  if(!ok) o.uiMask->Reset();
  if(o.uiMaskLogBudget<4 || (!ok && o.uiMaskLogBudget<12)) {
    ++o.uiMaskLogBudget;
    xess_mod::WriteLog("native_ui_stabilization","\"id\":"+std::to_string(id)+",\"ok\":"+(ok?"true":"false")
        +",\"hudless_tag\":"+std::to_string(tags[0])+",\"ui_tag\":"+std::to_string(tags[1])
        +",\"reference\":"+(o.uiMask->UsedReference()?"true":"false")
        +",\"hr\":"+std::to_string(int32_t(o.uiMask->LastError())));
  }
  return ok;
}
}
