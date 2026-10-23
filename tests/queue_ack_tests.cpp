#include <cstdlib>
#include <iostream>
#include "../src/addons/mfgunlock/latency_trial.hpp"
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::abort(); } } while (false)
using namespace mfgunlock::latency;
uint32_t Update(QueueTrial& t,uint64_t now,uint32_t ack,uint64_t first,uint64_t last,
                uint32_t interval=10000,uint32_t queue=1000,uint32_t latency=11000) {
  return t.Update(now,1,4,true,97,interval,queue,latency,ack,first,last);
}
QueueTrial Proposed() {
  QueueTrial t;
  CHECK(t.Update(0,1,4,true,97,10000,3000,12000,0,1,64)==0);
  for(uint64_t now=30000;now<=31500;now+=500)
    t.Update(now,1,4,true,97,10000,3000,12000,0,now,now+63);
  CHECK(t.cap==97 && t.started==0 && !t.accepted);
  return t;
}
int main() {
  auto t=Proposed();
  CHECK(Update(t,32000,UINT32_MAX,100,163)==97); // Unknown setter result is not an ack.
  CHECK(Update(t,33000,0,164,227)==97); // Deferred owner thread.
  CHECK(!t.accepted && t.started==0);
  CHECK(Update(t,33500,97,228,291)==97);
  CHECK(t.phase==QueueTrialPhase::kMeasuring && t.measured_windows==0);
  for(uint64_t now=34000;now<40000;now+=500)CHECK(Update(t,now,97,250,313)==97);
  CHECK(t.measured_windows==0 && !t.accepted); // Every window overlaps pre-ack data.
  CHECK(Update(t,40000,97,292,355)==97);
  CHECK(Update(t,42000,97,356,419)==97);
  CHECK(Update(t,44000,97,420,483)==97 && t.accepted);
  CHECK(Update(t,44500,97,484,547,14000)==0);
  CHECK(t.phase==QueueTrialPhase::kWaitingForRestoration);
  CHECK(Update(t,80000,UINT32_MAX,548,611)==0);
  CHECK(t.phase==QueueTrialPhase::kWaitingForRestoration);
  CHECK(Update(t,81000,0,612,675)==0 && t.phase==QueueTrialPhase::kIdle);
  auto timeout=Proposed();
  CHECK(Update(timeout,34500,0,100,163)==0);
  CHECK(timeout.phase==QueueTrialPhase::kWaitingForRestoration && !timeout.accepted);
  auto changed=Proposed();
  CHECK(changed.Update(32000,2,4,true,97,10000,1000,11000,UINT32_MAX,100,163)==0);
  CHECK(!changed.accepted && changed.started==0);
  auto missing=Proposed();
  CHECK(Update(missing,32000,97,0,0)==97 && missing.started==0);
  MultiplierTrial mt;
  MultiplierTrialSample ms{10000,3000,4000,14000,15000,4500};
  for(uint64_t i=0;i<8;++i)mt.Update(500*i,1,6,6,true,ResponsiveTrialReason::kFrameGenerationWorkload,ms,true,1+64*i,64+64*i);
  CHECK(mt.phase==MultiplierTrialPhase::kWaitingForCandidate);
  mt.Update(4000,1,6,5,true,ResponsiveTrialReason::kFrameGenerationWorkload,ms,true,449,512);
  CHECK(mt.phase==MultiplierTrialPhase::kMeasuringCandidate && mt.candidate_window.count==0);
  for(uint64_t now=4500;now<8000;now+=500)mt.Update(now,1,6,5,true,ResponsiveTrialReason::kFrameGenerationWorkload,ms,true,500,563);
  CHECK(mt.candidate_window.count==0 && !mt.accepted);
  mt.Update(8000,1,6,5,true,ResponsiveTrialReason::kFrameGenerationWorkload,ms,true,513,576);
  CHECK(mt.candidate_window.count==1);
  mt.Update(8500,1,6,5,true,ResponsiveTrialReason::kFrameGenerationWorkload,ms,true,513,576);
  CHECK(mt.candidate_window.count==1);
  std::cout<<"ack delay, timeout, unknown restore, fresh non-overlapping windows and epoch passed\n";
}
