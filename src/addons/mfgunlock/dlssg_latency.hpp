// SPDX-License-Identifier: MIT
#pragma once
#include "panel_ui.hpp"
#include "latency_marker_policy.hpp"
namespace mfgunlock::dlssglatency {
// The shared marker admission policy is tested, but does not establish safe
// activation of XeLL on NVIDIA's live queues. Never equate admission with SDK
// quiescence or with validation of replacing slReflexSleep.
inline constexpr bool XeLLRuntimeValidated=false;
inline void Draw() {
  static std::wstring preference=[] {
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"DLSSG.Latency",L"Output",L"Reflex",text,32,(backend::Directory()/L"MFGAdaUnlock.ini").c_str());
    return std::wstring(text);
  }();
  static bool saveFailed{};
  ui::Card card("##dlssg_latency_route");
  ui::Readout("##dlssg_latency_current","Active latency output","NVIDIA Reflex",
      "Preserves the game's native Reflex options, markers and sleep. XeMFG's own XeLL route does not take over the DLSS-G swapchain.",ui::Accent);
  ui::Setting("##dlssg_latency_requested","Latency output",
      "Experimental XeLL is unavailable in this candidate. Its marker policy is tested, but switching its SDK mode requires verified GPU quiescence and native DLSS-G runtime validation.",[&] {
    if(ImGui::BeginCombo("##dlssg_latency_choice","NVIDIA Reflex (recommended)")) {
      if(ImGui::Selectable("NVIDIA Reflex",true)) {
        saveFailed=!config::Save(backend::Directory()/L"MFGAdaUnlock.ini",{{L"DLSSG.Latency",L"Output",L"Reflex"}});
        if(!saveFailed) preference=L"Reflex";
      }
      ImGui::BeginDisabled(); ImGui::Selectable("XeLL (unavailable)",false); ImGui::EndDisabled();
      ui::Tooltip("No safe mode-switch/quiescence contract has been demonstrated for the NVIDIA-owned queues. Native Reflex remains active.");
      ImGui::EndCombo();
    }
  });
  if(_wcsicmp(preference.c_str(),L"Reflex")) ui::Notice("The requested latency route is unavailable. Native Reflex remains active.");
  if(saveFailed) ui::Notice("The latency preference could not be saved. Native Reflex remains active.");
}
}
