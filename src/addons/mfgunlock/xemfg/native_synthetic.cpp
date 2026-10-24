// SPDX-License-Identifier: MIT
#include "native_synthetic.hpp"

namespace mfgunlock::xemfg::native::synthetic {
namespace {
// Lock order: this lock, then Output::io (inside the marker/sleep helpers).
std::recursive_mutex lock;
std::weak_ptr<Output> owned;
// Read during admission with Output::io held, so never behind `lock`.
std::atomic<const Output*> ownedOutput{};
std::atomic<const char*> ownerName{};
std::atomic<bool> active{};
uint32_t building{}, pendingPresent{};
bool started{}, deferredLogged{};
uint32_t sleepLogBudget{};
// A game can create an FSR swapchain it never uses for frame generation while
// running DLSS-G through Streamline (Darktide). A DLSS-G request with recent
// Streamline markers means that route owns the frames; markers without one are
// only Reflex, so FSR frame generation may still take over.
constexpr uint64_t kStreamlineQuietMs=1000;
void StartFrame(const std::shared_ptr<Output>& o,uint32_t id) {
  const auto result=SleepSourceFrame(o,id);
  ForwardSourceMarker(o,id,XELL_SIMULATION_START);
  if(result!=sl::Result::eOk && sleepLogBudget<8) {
    ++sleepLogBudget;
    xess_mod::WriteLog("native_synthetic_sleep","\"adapter\":\""+std::string(ownerName.load())+"\",\"id\":"+std::to_string(id)
        +",\"result\":"+std::to_string(uint32_t(result)));
  }
  started=true; building=id;
}
}
void Begin(const std::shared_ptr<Output>& o,const char* adapter) {
  std::lock_guard guard(lock);
  owned=o; ownedOutput=o.get(); ownerName=adapter; active=false;
  started=false; deferredLogged=false; building=pendingPresent=0; sleepLogBudget=0;
  xess_mod::WriteLog("native_synthetic_frames","\"adapter\":\""+std::string(adapter)+"\",\"output\":"+std::to_string(o->diagnosticOutput)+",\"active\":false");
}
void End(const Output* o) {
  std::lock_guard guard(lock);
  if(ownedOutput.load()!=o) return;
  owned.reset(); ownedOutput=nullptr; ownerName=nullptr; active=false;
}
bool Driving(const char* adapter) noexcept { return active.load() && Owner()==adapter; }
const char* Owner() noexcept {
  const auto* o=ownedOutput.load();
  return o && Active().get()==o?ownerName.load():nullptr;
}
uint32_t SubmitFrame(const std::shared_ptr<Output>& o,const char* adapter) {
  std::lock_guard guard(lock);
  if(owned.lock()!=o || !o || ownerName.load()!=adapter) return 0;
  if(!active) {
    auto& s=State();
    const auto streamline=s.streamlineMarkerTick.load();
    if(s.streamlineGeneration && streamline && GetTickCount64()-streamline<kStreamlineQuietMs) {
      if(!deferredLogged) { deferredLogged=true; xess_mod::WriteLog("native_synthetic_deferred","\"adapter\":\""+std::string(adapter)+"\",\"reason\":\"streamline_active\"");}
      return 0;
    }
    // The adapter's generator is replaced: the Intel output is the only frame
    // generation and XeLL the only wait.
    active=true; s.nativeOff=true; s.reflexOptionsReady=true; s.reflexOff=true;
    xess_mod::WriteLog("native_synthetic_active","\"adapter\":\""+std::string(adapter)+"\",\"output\":"+std::to_string(o->diagnosticOutput));
  }
  if(!started) StartFrame(o,1);
  const auto id=building;
  ForwardSourceMarker(o,id,XELL_SIMULATION_END);
  ForwardSourceMarker(o,id,XELL_RENDERSUBMIT_START);
  State().recording=id;
  pendingPresent=id;
  return id;
}
void BeforeSourcePresent(const std::shared_ptr<Output>& o) {
  std::lock_guard guard(lock);
  if(!active || owned.lock()!=o) return;
  const auto id=pendingPresent;
  State().presenting=id;
  if(!id) return;
  ForwardSourceMarker(o,id,XELL_RENDERSUBMIT_END);
  ForwardSourceMarker(o,id,XELL_PRESENT_START);
}
void AfterSourcePresent(const std::shared_ptr<Output>& o) {
  std::lock_guard guard(lock);
  if(!active || owned.lock()!=o || !pendingPresent) return;
  const auto id=pendingPresent; pendingPresent=0;
  ForwardSourceMarker(o,id,XELL_PRESENT_END);
  StartFrame(o,id+1);
}
}
