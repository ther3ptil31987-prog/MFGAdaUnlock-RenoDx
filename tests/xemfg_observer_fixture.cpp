// SPDX-License-Identifier: MIT
// Independent consumer: it observes the original plugin, never the game's
// replacement feature state. No Steam implementation is included.
#include <windows.h>
#include <sl.h>
#include <sl_dlss_g.h>
extern "C" __declspec(dllexport) __declspec(noinline)
sl::Result ObserverDisable(HMODULE interposer) {
  auto resolve=reinterpret_cast<PFun_slGetFeatureFunction*>(GetProcAddress(interposer,"slGetFeatureFunction"));
  void* entry{};
  if(!resolve)return sl::Result::eErrorFeatureMissing;
  const auto result=resolve(sl::kFeatureDLSS_G,"slDLSSGSetOptions",entry);
  if(result!=sl::Result::eOk || !entry)return result;
  sl::DLSSGOptions off;off.mode=sl::DLSSGMode::eOff;
  return reinterpret_cast<PFun_slDLSSGSetOptions*>(entry)(sl::ViewportHandle{0},off);
}
extern "C" __declspec(dllexport) __declspec(noinline)
uint32_t ObserverFrames(HMODULE interposer) {
  auto resolve=reinterpret_cast<PFun_slGetFeatureFunction*>(GetProcAddress(interposer,"slGetFeatureFunction"));
  void* entry{};sl::DLSSGState state;
  if(!resolve || resolve(sl::kFeatureDLSS_G,"slDLSSGGetState",entry)!=sl::Result::eOk || !entry)return 0;
  if(reinterpret_cast<PFun_slDLSSGGetState*>(entry)(sl::ViewportHandle{0},state,nullptr)!=sl::Result::eOk)return 0;
  return state.numFramesActuallyPresented;
}
