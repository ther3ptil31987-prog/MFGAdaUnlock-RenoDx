// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/native_internal.hpp"
#include <condition_variable>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace mfgunlock::xemfg::native;
namespace {
Bridge state;
std::mutex gateMutex;
std::condition_variable gateCv;
bool entered{}, released{};
std::atomic<bool> block{}, modeFail{};
std::atomic<unsigned> sleeps{}, modes{}, markers{}, nativeSleeps{};
xell_sleep_params_t lastMode{};
sl::Result nativeResult=sl::Result::eOk;
sl::Result NativeSleep(const sl::FrameToken&) { ++nativeSleeps; return nativeResult; }
struct Token final : sl::FrameToken { operator uint32_t() const override { return 10; } };
void Require(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
xell_result_t SetMode(xell_context_handle_t,const xell_sleep_params_t* params) { ++modes; lastMode=*params; return modeFail?XELL_RESULT_ERROR_UNKNOWN:XELL_RESULT_SUCCESS; }
xell_result_t Sleep(xell_context_handle_t,uint32_t) {
  ++sleeps;
  if(block) { std::unique_lock lock(gateMutex); entered=true; gateCv.notify_all(); gateCv.wait_for(lock,std::chrono::seconds(2),[]{return released;}); }
  return XELL_RESULT_SUCCESS;
}
xell_result_t Marker(xell_context_handle_t,uint32_t,xell_latency_marker_type_t) {
  ++markers; { std::lock_guard lock(gateMutex); released=true; } gateCv.notify_all(); return XELL_RESULT_SUCCESS;
}
void Arm() { std::lock_guard lock(gateMutex); entered=released=false; block=true; }
void Entered() { std::unique_lock lock(gateMutex); Require(gateCv.wait_for(lock,std::chrono::seconds(1),[]{return entered;}),"sleep did not start"); }
void Release() { {std::lock_guard lock(gateMutex); released=true;} gateCv.notify_all(); }
}
namespace xess_mod { void WriteLog(const char*,const std::string&) noexcept {} }
namespace mfgunlock::xemfg::native { Bridge& State(){return state;} Output::~Output()=default; }
int main() {
  try {
    state.api.xellSetSleepMode=&SetMode; state.api.xellSleep=&Sleep; state.api.xellAddMarkerData=&Marker;
    auto output=std::make_shared<Output>(); output->last=1;
    FindFrame(*output,1)->sleeping=true;
    FindFrame(*output,1)->markers=1u<<XELL_RENDERSUBMIT_START;
    Arm(); auto sleeping=std::async(std::launch::async,[&]{return SleepSourceFrame(output,2);}); Entered();
    Require(SleepSourceFrame(output,3)==sl::Result::eErrorInvalidState,"concurrent sleep accepted");
    auto marker=std::async(std::launch::async,[&]{return ForwardSourceMarker(output,1,XELL_RENDERSUBMIT_END);});
    const bool timely=marker.wait_for(std::chrono::milliseconds(250))==std::future_status::ready;
    Release(); Require(marker.get(),"late marker failed"); Require(sleeping.get()==sl::Result::eOk,"sleep failed");
    Require(timely,"sleep held bookkeeping lock and blocked the marker needed to wake it");
    block=false; const auto count=sleeps.load();
    Require(SleepSourceFrame(output,2)==sl::Result::eOk && sleeps==count,"duplicate frame slept twice");
    Require(SleepSourceFrame(output,3)==sl::Result::eOk && modes==1,"identical mode reset every frame");
    state.limitUs=10000; modeFail=true;
    Require(SleepSourceFrame(output,4)==sl::Result::eErrorReflexAPI && output->sleepInterval==0,"failed mode was cached");
    modeFail=false; Require(SleepSourceFrame(output,4)==sl::Result::eOk && output->sleepInterval==10000,"mode retry missing");
    const auto markerCount=markers.load();
    Require(!ForwardSourceMarker(output,5,XELL_SIMULATION_START) && markers==markerCount,"marker sent before same-frame sleep");
    Require(SleepSourceFrame(output,7)==sl::Result::eOk,"test frame sleep");
    Require(!ForwardSourceMarker(output,7,XELL_PRESENT_END) && output->frames.Find(7)->failed,"END without matching START accepted");
    Require(SleepSourceFrame(output,8)==sl::Result::eOk,"test frame sleep");
    Require(ForwardSourceMarker(output,8,XELL_RENDERSUBMIT_START),"render start rejected");
    Require(ForwardSourceMarker(output,8,XELL_PRESENT_START),"overlapping present start rejected");
    Require(ForwardSourceMarker(output,8,XELL_RENDERSUBMIT_END),"legal overlapping END rejected");
    Arm(); auto stale=std::async(std::launch::async,[&]{return SleepSourceFrame(output,9);}); Entered();
    { std::lock_guard lock(output->io); ++output->epoch; output->frames.clear(); }
    Release(); Require(stale.get()==sl::Result::eErrorInvalidState && output->frames.empty(),"sleep republished an old resource epoch");
    Require(!output->sleepActive,"sleep lease leaked");
    const auto beforeStale=sleeps.load();
    Require(SleepSourceFrame(output,10,nullptr,nullptr,output->epoch-1)==sl::Result::eErrorInvalidState
        && sleeps==beforeStale && !output->frames.Find(10),"boundary wait crossed a resize epoch");
    block=false; state.latencyOutput=LatencyOutput::Reflex; state.gameReflexMode=1; state.limitUs=25000;
    Token token;
    Require(SleepSourceFrame(output,11)==sl::Result::eErrorInvalidState,"Reflex accepted without native function/token");
    const auto before=sleeps.load();
    Require(SleepSourceFrame(output,11,&NativeSleep,&token)==sl::Result::eOk,"Reflex sleep route failed");
    Require(nativeSleeps==1 && sleeps==before+1,"Reflex must call native once and retain Intel frame bookkeeping");
    Require(!lastMode.bLowLatencyMode && lastMode.minimumIntervalUs==0,"XeLL limiter competes with Reflex");
    Require(output->frames.Find(11)->latencyOutput==LatencyOutput::Reflex && state.reflexSleeps==1 && state.reflexOutputTick>0,"Reflex route not observed");
    Require(SleepSourceFrame(output,11,&NativeSleep,&token)==sl::Result::eOk && nativeSleeps==1 && sleeps==before+1,"duplicate Reflex sleep");
    Require(ForwardSourceMarker(output,11,XELL_SIMULATION_START),"Intel bookkeeping markers missing with Reflex");
    nativeResult=sl::Result::eErrorReflexAPI;
    Require(SleepSourceFrame(output,12,&NativeSleep,&token)==nativeResult && sleeps==before+1,"native failure swallowed or Intel sleep called after failure");
    Require(output->frames.Find(12)->failed && !output->sleepActive,"failed native wait retained admission/lease");
    const auto beforeOld=sleeps.load(); nativeResult=sl::Result::eOk;
    Require(SleepSourceFrame(output,3,&NativeSleep,&token)==sl::Result::eErrorInvalidState && sleeps==beforeOld,"wait older than the last dispatched id reached XeLL");
    Require(SleepSourceFrame(output,13,&NativeSleep,&token)==sl::Result::eOk && sleeps==beforeOld+1,"newer wait after a refused old one failed");
    std::cout<<"PASS: late marker during sleep, deduplication, mode cache/retry, same-frame ordering, reset epoch and monotonic XeLL ids\n";
    return 0;
  } catch(const std::exception& e) { Release(); std::cerr<<e.what()<<'\n'; return 1; }
}
