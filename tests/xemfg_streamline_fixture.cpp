// SPDX-License-Identifier: MIT
// Streamline ABI fixture only. Rendering and XeFG/XeLL use the real D3D12 SDK.
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <atomic>
#include <cstring>
#include <windows.h>
#include <intrin.h>
static std::atomic<uint32_t> fgMode{}, reflexMode{}, calls{}, reflexLimit{}, reflexSleeps{},lastReflexSleepToken{};
static std::atomic<bool> holdSleep{},sleepEntered{};
static std::atomic<bool> rejectTags{};
static std::atomic<bool> holdConstants{},constantsEntered{};
static std::atomic<uint32_t> fgStatus{};
static std::atomic<bool> initialized{},fgLoaded{true},reflexLoaded{true};
static std::atomic<uint32_t> queriesAfterShutdown{};
static std::atomic<uintptr_t> lateQueryCaller{};
static std::atomic<uint32_t> markerRoute{},modernMarkers{},historicalMarkers{};
#define EXPORT extern "C" __declspec(dllexport) __declspec(noinline)
EXPORT sl::Result slInit(const sl::Preferences&,uint64_t) {
  initialized=true; fgLoaded=reflexLoaded=true; return sl::Result::eOk;
}
EXPORT sl::Result slSetD3DDevice(void*) { return sl::Result::eOk; }
EXPORT sl::Result slShutdown() { initialized=false; return sl::Result::eOk; }
EXPORT sl::Result slIsFeatureLoaded(sl::Feature feature,bool& enabled) {
  if(!initialized) return sl::Result::eErrorNotInitialized;
  enabled=feature==sl::kFeatureDLSS_G?fgLoaded.load():reflexLoaded.load(); return sl::Result::eOk;
}
EXPORT sl::Result slSetFeatureLoaded(sl::Feature feature,bool enabled) {
  if(!initialized) return sl::Result::eErrorNotInitialized;
  if(feature==sl::kFeatureDLSS_G) fgLoaded=enabled; else reflexLoaded=enabled;
  return sl::Result::eOk;
}
EXPORT uint32_t FixturePhysicalFgLoaded() { return fgLoaded; }
static std::atomic<uint32_t> supportQueries{};
// RTX 20/30-like adapter: Streamline refuses DLSS-G and its plugin exposes no
// DLSS-G entry points; Reflex and PCL remain available.
static std::atomic<bool> unsupportedDlssg{};
EXPORT void FixtureUnsupportedDlssg(bool value) { unsupportedDlssg=value; }
EXPORT sl::Result slIsFeatureSupported(sl::Feature feature,const sl::AdapterInfo&) {
  ++supportQueries;
  if(feature==sl::kFeatureDLSS_G && unsupportedDlssg) return sl::Result::eErrorAdapterNotSupported;
  return feature==sl::kFeatureDLSS_G || feature==sl::kFeatureReflex || feature==sl::kFeaturePCL?sl::Result::eOk:sl::Result::eErrorFeatureNotSupported;
}
EXPORT sl::Result slGetFeatureRequirements(sl::Feature,sl::FeatureRequirements&) { ++supportQueries; return sl::Result::eOk; }
EXPORT uint32_t FixtureSupportQueries() { return supportQueries; }
EXPORT uint32_t FixtureQueriesAfterShutdown() { return queriesAfterShutdown; }
EXPORT uintptr_t FixtureLateQueryCaller() { return lateQueryCaller; }
// The DLSS-G backend fixture (plan 6.1) reads what reached the provider:
// the generated-frame count of the last options and the state's advertised maximum.
static std::atomic<uint32_t> lastGenerated{UINT32_MAX},lastOptionsVersion{},stateMax{1};
static __declspec(noinline) sl::Result Options(const sl::ViewportHandle&,const sl::DLSSGOptions& o) {
  ++calls; fgMode=uint32_t(o.mode); lastGenerated=o.numFramesToGenerate; lastOptionsVersion=uint32_t(o.structVersion); return sl::Result::eOk;
}
static __declspec(noinline) sl::Result State(const sl::ViewportHandle&,sl::DLSSGState& s,const sl::DLSSGOptions*) {
  ++calls; s.numFramesActuallyPresented=1; s.status=sl::DLSSGStatus(fgStatus.load());
  if(s.structVersion>=sl::kStructVersion2) s.numFramesToGenerateMax=stateMax;
  return sl::Result::eOk;
}
// 1.6 (plan B): the DLSS Super Resolution options that reached the provider.
static std::atomic<uint32_t> dlssMode{UINT32_MAX},dlssWidth{},dlssVersion{},dlssPresets[6]{};
static __declspec(noinline) sl::Result DlssOptions(const sl::ViewportHandle&,const sl::DLSSOptions& o) {
  ++calls; dlssMode=uint32_t(o.mode); dlssWidth=o.outputWidth; dlssVersion=uint32_t(o.structVersion);
  const sl::DLSSPreset presets[]={o.dlaaPreset,o.qualityPreset,o.balancedPreset,o.performancePreset,o.ultraPerformancePreset,o.ultraQualityPreset};
  for(int i=0;i<6;++i) dlssPresets[i]=uint32_t(presets[i]);
  return sl::Result::eOk;
}
// Mode, output width and version, and the preset of one mode slot (0 DLAA .. 5 ultra quality).
EXPORT uint32_t FixtureDlssMode() { return dlssMode; }
EXPORT uint32_t FixtureDlssWidth() { return dlssWidth; }
EXPORT uint32_t FixtureDlssVersion() { return dlssVersion; }
EXPORT uint32_t FixtureDlssPreset(uint32_t slot) { return slot<6?dlssPresets[slot].load():UINT32_MAX; }
// 1.6 (plan B.6): the DLSS Ray Reconstruction options that reached the provider.
static std::atomic<uint32_t> rrMode{UINT32_MAX},rrVersion{},rrPresets[6]{};
static __declspec(noinline) sl::Result DlssdOptions(const sl::ViewportHandle&,const sl::DLSSDOptions& o) {
  ++calls; rrMode=uint32_t(o.mode); rrVersion=uint32_t(o.structVersion);
  const sl::DLSSDPreset presets[]={o.dlaaPreset,o.qualityPreset,o.balancedPreset,o.performancePreset,o.ultraPerformancePreset,o.ultraQualityPreset};
  for(int i=0;i<6;++i) rrPresets[i]=uint32_t(presets[i]);
  return sl::Result::eOk;
}
EXPORT uint32_t FixtureRrMode() { return rrMode; }
EXPORT uint32_t FixtureRrVersion() { return rrVersion; }
EXPORT uint32_t FixtureRrPreset(uint32_t slot) { return slot<6?rrPresets[slot].load():UINT32_MAX; }
EXPORT uint32_t FixtureLastGenerated() { return lastGenerated; }
EXPORT uint32_t FixtureLastOptionsVersion() { return lastOptionsVersion; }
EXPORT void FixtureStateMax(uint32_t value) { stateMax=value; }
static __declspec(noinline) sl::Result Reflex(const sl::ReflexOptions& o) {
  ++calls; reflexMode=uint32_t(o.mode); reflexLimit=o.frameLimitUs; return sl::Result::eOk;
}
static __declspec(noinline) sl::Result ReflexSleep(const sl::FrameToken& f) {
  ++reflexSleeps; lastReflexSleepToken=uint32_t(f);calls+=uint32_t(f)+1;
  if(holdSleep) {
    sleepEntered=true;
    const auto deadline=GetTickCount64()+10000;
    while(holdSleep && GetTickCount64()<deadline) ::Sleep(1);
  }
  return sl::Result::eOk;
}
EXPORT void FixtureHoldSleep(bool hold) { holdSleep=hold; if(hold) sleepEntered=false; }
EXPORT bool FixtureSleepEntered() { return sleepEntered; }
static __declspec(noinline) sl::Result Marker(sl::PCLMarker m,const sl::FrameToken& f) {
  ++modernMarkers;
  calls+=uint32_t(f)+uint32_t(m)+1; return sl::Result::eOk;
}
static __declspec(noinline) sl::Result HistoricalMarker(uint32_t m,const sl::FrameToken& f) {
  ++historicalMarkers; calls+=uint32_t(f)+m+2; return sl::Result::eOk;
}
EXPORT void FixtureMarkerRoute(uint32_t mode) { markerRoute=mode; }
EXPORT uint32_t FixtureMarkerCalls() { return modernMarkers; }
EXPORT uint32_t FixtureHistoricalMarkerCalls() { return historicalMarkers; }
EXPORT sl::Result slGetFeatureFunction(sl::Feature,const char* name,void*& fn) {
  if(!initialized) { ++queriesAfterShutdown; lateQueryCaller=uintptr_t(_ReturnAddress()); fn=nullptr; return sl::Result::eErrorNotInitialized; }
  ++calls;
  if(unsupportedDlssg && !strncmp(name,"slDLSSG",7)) { fn=nullptr; return sl::Result::eErrorFeatureMissing; }
  if(!strcmp(name,"slDLSSSetOptions")) fn=reinterpret_cast<void*>(&DlssOptions);
  else if(!strcmp(name,"slDLSSDSetOptions")) fn=reinterpret_cast<void*>(&DlssdOptions);
  else if(!strcmp(name,"slDLSSGSetOptions")) fn=reinterpret_cast<void*>(&Options);
  else if(!strcmp(name,"slDLSSGGetState")) fn=reinterpret_cast<void*>(&State);
  else if(!strcmp(name,"slReflexSetOptions")) fn=reinterpret_cast<void*>(&Reflex);
  else if(!strcmp(name,"slReflexSleep")) fn=reinterpret_cast<void*>(&ReflexSleep);
  else if(!strcmp(name,"slPCLSetMarker") && markerRoute!=1) fn=reinterpret_cast<void*>(&Marker);
  else if(!strcmp(name,"slReflexSetMarker")) fn=markerRoute==2?reinterpret_cast<void*>(&Marker):reinterpret_cast<void*>(&HistoricalMarker);
  else { fn=nullptr; return sl::Result::eErrorFeatureMissing; }
  return sl::Result::eOk;
}
EXPORT sl::Result slSetTag(const sl::ViewportHandle& v,const sl::ResourceTag*,uint32_t n,sl::CommandBuffer*) {
  calls+=uint32_t(v)+n+1; return rejectTags?sl::Result::eErrorInvalidState:sl::Result::eOk;
}
EXPORT sl::Result slSetTagForFrame(const sl::FrameToken& f,const sl::ViewportHandle&,const sl::ResourceTag*,uint32_t n,sl::CommandBuffer*) {
  calls+=uint32_t(f)+n+1; return rejectTags?sl::Result::eErrorInvalidState:sl::Result::eOk;
}
EXPORT void FixtureRejectTags(bool value) { rejectTags=value; }
EXPORT sl::Result slSetConstants(const sl::Constants&,const sl::FrameToken& f,const sl::ViewportHandle&) {
  if(holdConstants) {constantsEntered=true;while(holdConstants)::Sleep(1);}
  calls+=uint32_t(f)+1; return sl::Result::eOk;
}
EXPORT void FixtureHoldConstants(bool value) {holdConstants=value;if(value)constantsEntered=false;}
EXPORT bool FixtureConstantsEntered() {return constantsEntered;}
EXPORT uint32_t FixtureModes() { return fgMode|(reflexMode<<8); }
EXPORT void FixtureFgStatus(uint32_t value) { fgStatus=value; }

EXPORT uint32_t FixtureReflexSleeps() { return reflexSleeps; }
EXPORT uint32_t FixtureLastReflexSleepToken() { return lastReflexSleepToken; }
EXPORT uint32_t FixtureReflexLimit() { return reflexLimit; }
