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
uint32_t building{}, pendingPresent{};
bool started{};
uint32_t sleepLogBudget{};
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
  owned=o; ownedOutput=o.get(); ownerName=adapter;
  started=false; building=pendingPresent=0; sleepLogBudget=0;
  xess_mod::WriteLog("native_synthetic_frames","\"adapter\":\""+std::string(adapter)+"\",\"output\":"+std::to_string(o->diagnosticOutput));
}
void End(const Output* o) {
  std::lock_guard guard(lock);
  if(ownedOutput.load()!=o) return;
  owned.reset(); ownedOutput=nullptr; ownerName=nullptr;
}
const char* Owner() noexcept {
  const auto* o=ownedOutput.load();
  return o && Active().get()==o?ownerName.load():nullptr;
}
uint32_t SubmitFrame(const std::shared_ptr<Output>& o,const char* adapter) {
  std::lock_guard guard(lock);
  if(owned.lock()!=o || !o || ownerName.load()!=adapter) return 0;
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
  if(owned.lock()!=o) return;
  const auto id=pendingPresent;
  State().presenting=id;
  if(!id) return;
  ForwardSourceMarker(o,id,XELL_RENDERSUBMIT_END);
  ForwardSourceMarker(o,id,XELL_PRESENT_START);
}
void AfterSourcePresent(const std::shared_ptr<Output>& o) {
  std::lock_guard guard(lock);
  if(owned.lock()!=o || !pendingPresent) return;
  const auto id=pendingPresent; pendingPresent=0;
  ForwardSourceMarker(o,id,XELL_PRESENT_END);
  StartFrame(o,id+1);
}
}
