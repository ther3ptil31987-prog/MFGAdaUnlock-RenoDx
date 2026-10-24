// SPDX-License-Identifier: MIT
// Numerical contracts of the imported pacing estimator; not display timing.
#include "../src/addons/mfgunlock/xemfg/vendor/sdk_pacing_dlssg.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace sdk_pacing::cadence;
namespace {
constexpr int64_t ms=1000000,base=1000000000;
void Check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
}
int main() {
  try {
    BatchIntervalEstimator clock;
    for(uint32_t i=1;i<=8;++i) clock.Observe(i,4,base+i*25*ms);
    Check(clock.Samples()==6 && clock.Typical()==25*ms && clock.BatchTime(4)==25*ms,"stable source cadence");
    BatchIntervalEstimator::Decision plan{};
    const auto start=base+8*25*ms;
    Check(clock.Plan(8,4,1,start,plan) && plan.interval==6250000 && plan.deadline==start+6250000,"4x spacing");
    Check(clock.Plan(8,4,1,start,plan,6*ms) && plan.deadline==start+250000,"present latency not subtracted");
    Check(clock.Plan(8,4,3,start,plan) && plan.capped && plan.deadline==start+6250000,"wait exceeds one output interval");
    MeteredDeadline metered{};
    Check(PlanMetered(start,6250000,2,2500000,start+8*ms,metered) && metered.deadline==start+10*ms,"metered early release");
    Check(PlanMetered(start,6250000,1,20*ms,start,metered) && metered.lead==3125000,"metered lead exceeds half spacing");
    Check(PlanMetered(start,6250000,1,2500000,start+30*ms,metered) && metered.deadline<start+30*ms,"late output given a new future deadline");
    MeteredBatch batch{true,true,8,4,start,25*ms};
    for(unsigned outputs : {2u,3u,4u,5u,6u}) {
      batch.outputs=outputs;
      for(unsigned index=1;index<outputs;++index) Check(batch.CanQueue(8,outputs,index,start+index*ms),"acknowledged batch cannot queue");
      Check(!batch.CanQueue(9,outputs,1,start+ms),"another batch inherited metering");
      Check(!batch.CanQueue(8,outputs,outputs,start+ms),"output beyond configured batch accepted");
    }
    batch.outputs=4;
    Check(!batch.CanQueue(8,6,1,start+ms),"multiplier mismatch queued");
    Check(!batch.CanQueue(8,4,0,start+ms),"first output should configure, not use previous metering");
    Check(!batch.CanQueue(8,4,1,start-1) && !batch.CanQueue(8,4,1,start+51*ms),"invalid/stale timing queued");
    batch.accepted=false; Check(!batch.CanQueue(8,4,1,start+ms),"driver refusal queued");
    batch.accepted=true; batch.queueAhead=false; Check(!batch.CanQueue(8,4,1,start+ms),"VSync/unverified/disabled route queued");
    clock.Observe(9,4,start+100*ms);
    Check(clock.Typical()==25*ms && clock.Excluded()==1 && clock.Mean()>clock.Typical(),"hitch contaminated pacing estimate");
    for(uint32_t i=10;i<=15;++i) clock.Observe(i,4,start+100*ms+(i-9)*50*ms);
    Check(clock.Typical()==50*ms,"persistent slowdown rejected as a hitch");
    const auto samples=clock.Samples();
    clock.Observe(19,4,start+600*ms);
    Check(clock.Samples()==samples && clock.Gap()==4,"short reset lost cadence history");
    clock.Observe(25,4,start+900*ms);
    Check(clock.Samples()==0,"large frame gap preserved stale history");
    for(uint32_t i=1;i<=8;++i) clock.Observe(i,4,base+i*25*ms);
    clock.Observe(9,2,base+9*25*ms);
    Check(clock.BatchTime(2)==12500000 && clock.Typical()==12500000,"multiplier decrease kept old source backpressure");
    clock.Observe(10,6,base+9*25*ms+12500000);
    Check(clock.BatchTime(6)==12500000,"multiplier increase extended batch duration");
    Check(!PlanMetered(0,1,1,0,1,metered),"invalid metering base accepted");
    // flip.2 (Darktide Mourningstar): FG re-enabled in a transition seeds 34 ms batches while the
    // game can produce 20 ms. The driver holds every flip to the configured time, so a batch
    // start is never earlier than it; the configured time must still fall to the source cadence.
    {
      BatchIntervalEstimator seeded;
      for(uint32_t i=1;i<=7;++i) seeded.Observe(i,2,base+i*34*ms);
      Check(seeded.Settled()==6 && !FlipReady(seeded),"metering configured from an unsettled window");
      int64_t start=base+7*34*ms,inEffect=0; uint32_t frame=7; bool limited{};
      while(seeded.Settled()<FlipPolicy::kSettleBatches) { start+=34*ms; seeded.Observe(++frame,2,start); }
      Check(FlipReady(seeded),"settled window not accepted for metering");
      for(int batch=0;batch<200;++batch) {
        inEffect=FlipBatchTime(seeded.BatchTime(2),inEffect,true,limited);
        start+=std::max<int64_t>(20*ms,inEffect); seeded.Observe(++frame,2,start);
      }
      Check(inEffect<=20*ms && inEffect>=19*ms,"flip metering locked at its own slow seed");
      // A source that already runs at the estimate is never held behind it.
      Check(FlipBatchTime(17*ms,0,true,limited)<17*ms && !limited,"metering at the estimate throttles the source");
      // hitch.1 still limits growth over the configuration in effect for the same count.
      Check(FlipBatchTime(40*ms,16*ms,true,limited)==18*ms && limited,"metering growth not limited");
      Check(FlipBatchTime(40*ms,16*ms,false,limited)==40*ms-40*ms/FlipPolicy::kHeadroomDivisor && !limited,"count change limited by another count");
      seeded.Reset(); Check(seeded.Settled()==0 && !FlipReady(seeded),"reset kept metering ready");
    }
    std::cout<<"PASS: source/output spacing, GPU-latency compensation, metered lead, hitch recovery, gaps, multiplier transitions and flip metering recovery from a slow seed; numerical only\n";
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
