// SPDX-License-Identifier: MIT
#pragma once
#include "xemfg/native_window.hpp"
#include "backend_selection.hpp"
#include "panel_ui.hpp"
#include <dxgi.h>
#include <include/reshade.hpp>
namespace mfgunlock::dlssgdisplay {
using xemfg::native::WindowControl;
using xemfg::native::WindowStatus;
using xemfg::native::DisplayMode;
inline std::mutex mutex;
inline std::shared_ptr<WindowControl> controller;
inline const void* owner{};
inline std::atomic<bool> refresh{true};
inline std::atomic<uint64_t> nextCheck{};
inline bool LoadedPreference() {
  static const bool value=[] {
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"DLSSG",L"ForceBorderless",L"0",text,32,(backend::Directory()/L"MFGAdaUnlock.ini").c_str());
    return !_wcsicmp(text,L"1") || !_wcsicmp(text,L"true") || !_wcsicmp(text,L"on");
  }();
  return value;
}
inline std::atomic<bool>& Preference() { static std::atomic<bool> value{LoadedPreference()};return value; }
inline void Retire(const void* expected=nullptr) {
  std::shared_ptr<WindowControl> previous;
  { std::lock_guard guard(mutex); if(expected && owner!=expected) return; previous=std::move(controller);owner=nullptr; }
  if(previous) previous->Retire();
  refresh=true;
}
// Called only with the swapchain supplied by ReShade's own live callback. Do
// not retain its COM proxy and thereby prevent the game's final Release.
inline void Observe(reshade::api::swapchain* swapchain) {
  if(backend::active!=backend::Kind::DlssG || !swapchain || !swapchain->get_device()
      || (swapchain->get_device()->get_api()!=reshade::api::device_api::d3d12
          && swapchain->get_device()->get_api()!=reshade::api::device_api::d3d11)) return;
  const bool requested=Preference();
  // 1.5.2: no window hook while Force Borderless is off and nothing is
  // managed; the presentation path then matches 1.4.5. A controller bound while
  // it was on stays until it has restored the game's window.
  bool bound{}; { std::lock_guard guard(mutex); bound=controller!=nullptr; }
  if(!requested && !bound) { refresh=false; return; }
  if(!requested && !refresh.load()) return;
  const auto now=GetTickCount64();
  const bool explicitRequest=refresh.exchange(false);
  if(!explicitRequest && now<nextCheck.load()) return;
  nextCheck=now+1000;
  auto* native=reinterpret_cast<IUnknown*>(uintptr_t(swapchain->get_native()));
  if(!native) return;
  IDXGISwapChain* dxgi{};
  if(FAILED(native->QueryInterface(IID_PPV_ARGS(&dxgi)))) return;
  BOOL exclusive{}; const bool known=SUCCEEDED(dxgi->GetFullscreenState(&exclusive,nullptr));
  DXGI_SWAP_CHAIN_DESC desc{}; const bool valid=SUCCEEDED(dxgi->GetDesc(&desc)); dxgi->Release();
  if(!valid || !desc.OutputWindow) return;
  std::shared_ptr<WindowControl> value;
  { std::lock_guard guard(mutex); if(owner==swapchain) value=controller; }
  if(!value) {
    Retire(); value=WindowControl::Bind(desc.OutputWindow,false,known,exclusive!=FALSE);
    std::lock_guard guard(mutex); owner=swapchain;controller=value;
  } else value->ObserveFullscreen(known,exclusive!=FALSE);
  if(value) {
    const auto state=value->Snapshot();
    if(requested || state.managed) value->Request(requested,explicitRequest);
  }
}
// 1.7: Force Borderless is no longer offered in the panel; [DLSSG]
// ForceBorderless in MFGAdaUnlock.ini still applies, read once at startup.
inline void Draw() {
  const bool requested=Preference();
  std::shared_ptr<WindowControl> value; { std::lock_guard guard(mutex); value=controller; }
  const auto state=value?value->Snapshot():WindowStatus{};
  const char* text=!value && !requested?"Game controlled":state.pending?"Change pending":state.mode==DisplayMode::NativeBorderless?"Borderless (game controlled)":
      state.mode==DisplayMode::ManagedBorderless?"Borderless (addon controlled)":state.mode==DisplayMode::Exclusive?"Exclusive - use the game's display menu":
      state.mode==DisplayMode::Windowed?"Windowed":"Unknown - no presentation change";
  ui::Readout("##dlssg_display_state","Display mode",text);
  if(state.error==ERROR_BUSY) ui::Notice("The game or another addon changed the window; that change was preserved.");
}
}
