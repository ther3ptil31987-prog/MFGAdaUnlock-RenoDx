// SPDX-License-Identifier: MIT
#include <cstdlib>
#include <iostream>
#include "../src/addons/mfgunlock/nvapi_status.hpp"
#include "../src/addons/mfgunlock/latency_trace.hpp"
#define CHECK(x) do { if (!(x)) { std::cerr << "failed line " << __LINE__ << ": " #x "\n"; return 1; } } while(0)
using namespace mfgunlock;
void Fill(nvapistatus::LatencyResult& r, uint64_t first, uint64_t scale = 1) {
  r = {};
  for (size_t i=0; i<64; ++i) {
    auto& f = r.frames[i]; f.frame_id = first + i;
    const uint64_t t = (1000000 + f.frame_id * 10000) * scale;
    f.input_sample_time = f.simulation_start_time = t;
    f.simulation_end_time = t + 1000*scale;
    f.render_submit_start_time = t+2000*scale; f.render_submit_end_time=t+2500*scale;
    f.present_start_time=t+3000*scale; f.present_end_time=t+3100*scale;
    f.os_render_queue_start_time=t+4000*scale;
    f.gpu_render_start_time=t+4000*scale; f.gpu_render_end_time=t+8000*scale;
    f.gpu_frame_time_us=10000; f.ai_frame_time_us=1000;
  }
}
int main() {
  CHECK(!nvapistatus::NgxFeatureActive(0));
  CHECK(nvapistatus::NgxFeatureActive(
      nvapistatus::kNgxOverrideCreated));
  CHECK(nvapistatus::NgxFeatureActive(
      nvapistatus::kNgxOverrideEvaluate));
  CHECK(nvapistatus::NgxFeatureConfigured(
      nvapistatus::kNgxOverridePreset));
  CHECK(nvapistatus::NgxFeatureConfigured(
      nvapistatus::kNgxOverrideScalingRatio));
  CHECK(!nvapistatus::NgxFeatureConfigured(
      nvapistatus::kNgxOverrideInitialized));
  nvapistatus::LatencyResult r{}; latency::History h{};
  Fill(r,0);
  auto a = latency::Analyze(r.frames,10000000,1000,1,h);
  CHECK(a.timestamp_units == latency::Units::kMicroseconds && !a.source_timing_confident);
  CHECK((a.source_timing_issue_mask & latency::kTimingNotFresh) != 0);
  Fill(r,50); a=latency::Analyze(r.frames,10000000,1500,1,h);
  CHECK(a.source_timing_confident && a.source_interval_us==10000 && a.median_queue_wait_us==0);
  CHECK(a.queue_timing_confident && a.source_timing_issue_mask == latency::kTimingOk);
  CHECK(a.median_pipeline_latency_us==8000 && a.new_frames==50);
  CHECK(a.p95_pipeline_latency_us==8000 && a.p95_gpu_frame_time_us==10000);
  CHECK(a.median_simulation_cpu_us==1000 && a.median_submit_cpu_us==500);
  a=latency::Analyze(r.frames,10000000,2000,1,h); CHECK(!a.fresh && !a.source_timing_confident);
  Fill(r,100,10); h={}; a=latency::Analyze(r.frames,10000000,1000,2,h);
  Fill(r,150,10); a=latency::Analyze(r.frames,10000000,1500,2,h);
  CHECK(a.timestamp_units==latency::Units::kQpc && a.source_timing_confident);
  CHECK(a.median_pipeline_latency_us==8000);
  Fill(r,200,7); a=latency::Analyze(r.frames,10000000,2000,2,h);
  CHECK(a.timestamp_units==latency::Units::kUnknown && !a.source_timing_confident);
  h = {};
  Fill(r,200); for(auto& f:r.frames) f.gpu_frame_time_us=2500;
  uint64_t current_qpc =
      latency::AsQpcTicks(r.frames[63].gpu_render_end_time, 10000000);
  a=latency::Analyze(r.frames,10000000,2000,4,h,current_qpc);
  Fill(r,250); for(auto& f:r.frames) f.gpu_frame_time_us=2500;
  current_qpc = latency::AsQpcTicks(
      r.frames[63].gpu_render_end_time, 10000000);
  a=latency::Analyze(r.frames,10000000,2500,4,h,current_qpc);
  CHECK(a.source_timing_confident && a.queue_timing_confident);
  Fill(r,300); r.frames[40].frame_id=1;
  a=latency::Analyze(r.frames,10000000,3000,4,h); CHECK(!a.source_timing_confident);
  Fill(r,350); for(size_t i=0;i<4;++i) r.frames[i].os_render_queue_start_time-=3000;
  a=latency::Analyze(r.frames,10000000,3500,4,h); CHECK(a.median_queue_wait_us==0);
  Fill(r,400); a=latency::Analyze(r.frames,10000000,7000,4,h); CHECK(!a.fresh);
  Fill(r,450); a=latency::Analyze(r.frames,10000000,7500,3,h); CHECK(!a.fresh);
  Fill(r,500); for(auto& f:r.frames) f.os_render_queue_start_time=0;
  a=latency::Analyze(r.frames,10000000,8000,3,h);
  CHECK(a.source_timing_confident && !a.queue_timing_confident);

  latency::QueueTrial trial;
  CHECK(trial.Update(0,1,4,true,97,10000,3000,12000)==0);
  for(uint64_t t=30000;t<31500;t+=500) CHECK(trial.Update(t,1,4,true,97,10000,3000,12000)==0);
  CHECK(trial.Update(31500,1,4,true,97,10000,3000,12000)==97);
  CHECK(trial.Update(35500,1,4,true,0,10300,1000,11000)==97);
  CHECK(trial.accepted); // beneficial low queue must not immediately remove its own cap
  CHECK(trial.Update(36000,1,4,true,0,14000,1000,11000)==0); // base-FPS loss rolls back
  CHECK(trial.Update(36500,1,4,true,97,10000,3000,12000)==0); // cooldown
  CHECK(trial.Update(67000,1,4,false,97,10000,3000,12000)==0);

  using latency::MultiplierTrialPhase;
  using latency::ResponsiveTrialReason;
  CHECK(latency::ClassifyResponsiveTrialReason(
            true, 10000, 3000, 0, 10000, 8000, 1000) ==
        ResponsiveTrialReason::kDisplayOversubscription);
  CHECK(latency::ClassifyResponsiveTrialReason(
            true, 10000, 0, 0, 10000, 8000, 1000) ==
        ResponsiveTrialReason::kDisplayOversubscription);
  CHECK(latency::ClassifyResponsiveTrialReason(
            false, 10000, 3000, 0, 10000, 8000, 1000) ==
        ResponsiveTrialReason::kRenderQueue);
  CHECK(latency::ClassifyResponsiveTrialReason(
            false, 20000, 1000, 60000, 30000, 10000, 1000) ==
        ResponsiveTrialReason::kHighInputLatency);
  CHECK(latency::ClassifyResponsiveTrialReason(
            false, 20000, 1000, 0, 60000, 10000, 1000) ==
        ResponsiveTrialReason::kHighPipelineLatency);
  CHECK(latency::ClassifyResponsiveTrialReason(
            false, 40000, 6170, 0, 55630, 39110, 5012) ==
        ResponsiveTrialReason::kGpuSaturatedLatencyProxy);
  CHECK(latency::ClassifyResponsiveTrialReason(
            false, 40000, 6170, 55000, 55630, 39110, 5012) ==
        ResponsiveTrialReason::kNone);
  CHECK(latency::ClassifyResponsiveTrialReason(
            false, 20000, 1000, 0, 59999, 10000, 4500) ==
        ResponsiveTrialReason::kFrameGenerationWorkload);
  CHECK(latency::ClassifyResponsiveTrialReason(
            false, 20000, 1000, 0, 49999, 10000, 2500) ==
        ResponsiveTrialReason::kNone);

  latency::MultiplierTrialSummary stable_baseline{
      10000, 2000, 3000, 14000, 15000, 4000, 8};
  latency::MultiplierTrialSummary faster_candidate{
      8000, 1000, 1500, 12000, 14000, 3000, 8};
  CHECK(latency::MultiplierTrialImproved(stable_baseline,
                                         faster_candidate));

  const auto Sample = [](uint32_t interval, uint32_t queue,
                         uint32_t queue_p95, uint32_t pipeline,
                         uint32_t pipeline_p95, uint32_t ai) {
    return latency::MultiplierTrialSample{
        interval, queue, queue_p95, pipeline, pipeline_p95, ai};
  };
  latency::MultiplierTrial multiplier_trial;
  const auto trigger = ResponsiveTrialReason::kFrameGenerationWorkload;
  uint64_t now = 0;
  for (int index = 0; index < 8; ++index, now += 500) {
    const uint32_t override_value = multiplier_trial.Update(
        now, 1, 6, 6, true, trigger,
        Sample(10000, 3000, 4000, 14000, 15000, 4500));
    CHECK(override_value == (index == 7 ? 5u : 0u));
  }
  CHECK(multiplier_trial.phase == MultiplierTrialPhase::kWaitingForCandidate);
  for (int index = 0; index < 8; ++index, now += 500) {
    CHECK(multiplier_trial.Update(
              now, 1, 6, 5, true, trigger,
              Sample(10000, 1800, 2500, 13000, 14500, 3900)) ==
          (index == 7 ? 4u : 5u));
  }
  CHECK(multiplier_trial.approved_multiplier == 5);
  CHECK(multiplier_trial.accepted);
  for (int index = 0; index < 8; ++index, now += 500) {
    CHECK(multiplier_trial.Update(
              now, 1, 6, 4, true, trigger,
              Sample(10200, 1200, 1800, 12000, 14000, 3400)) ==
          (index == 7 ? 3u : 4u));
  }
  CHECK(multiplier_trial.approved_multiplier == 4);
  for (int index = 0; index < 8; ++index, now += 500) {
    CHECK(multiplier_trial.Update(
              now, 1, 6, 3, true, trigger,
              Sample(10300, 800, 1200, 11000, 13500, 2800)) == 3);
  }
  CHECK(multiplier_trial.approved_multiplier == 3);
  CHECK(multiplier_trial.phase == MultiplierTrialPhase::kAccepted);

  // A p95 regression rejects the current step and restores the last approved
  // multiplier rather than falling all the way back to the saved setting.
  latency::MultiplierTrial partial;
  now = 0;
  for (int index = 0; index < 8; ++index, now += 500)
    partial.Update(now, 2, 5, 5, true, trigger,
                   Sample(10000, 3000, 4000, 14000, 15000, 4500));
  for (int index = 0; index < 8; ++index, now += 500)
    partial.Update(now, 2, 5, 4, true, trigger,
                   Sample(10000, 1600, 2400, 13000, 14500, 3800));
  CHECK(partial.approved_multiplier == 4);
  CHECK(partial.override_multiplier == 3);
  for (int index = 0; index < 8; ++index, now += 500)
    partial.Update(now, 2, 5, 3, true, trigger,
                   Sample(10000, 1000, 1800, 12000, 15000, 3200));
  CHECK(partial.approved_multiplier == 4);
  CHECK(partial.override_multiplier == 4);
  CHECK(partial.phase == MultiplierTrialPhase::kAccepted);

  // More than 8% source-FPS loss rejects even a lower pipeline median.
  latency::MultiplierTrial throughput_loss;
  now = 0;
  for (int index = 0; index < 8; ++index, now += 500)
    throughput_loss.Update(now, 3, 4, 4, true, trigger,
                           Sample(10000, 3000, 4000, 14000, 15000, 4500));
  for (int index = 0; index < 8; ++index, now += 500)
    throughput_loss.Update(now, 3, 4, 3, true, trigger,
                           Sample(10900, 1200, 1800, 12000, 14000, 3200));
  CHECK(throughput_loss.override_multiplier == 0);
  CHECK(throughput_loss.phase == MultiplierTrialPhase::kCooldown);

  // A provider that does not apply the temporary request must time out and
  // restore the saved multiplier without accumulating candidate samples.
  latency::MultiplierTrial apply_timeout;
  now = 0;
  for (int index = 0; index < 8; ++index, now += 500)
    apply_timeout.Update(now, 4, 4, 4, true, trigger,
                         Sample(10000, 3000, 4000, 14000, 15000, 4500));
  CHECK(apply_timeout.override_multiplier == 3);
  CHECK(apply_timeout.phase == MultiplierTrialPhase::kWaitingForCandidate);
  now += latency::MultiplierTrial::kApplyTimeoutMs;
  CHECK(apply_timeout.Update(
            now, 4, 4, 4, true, trigger,
            Sample(10000, 3000, 4000, 14000, 15000, 4500)) == 0);
  CHECK(apply_timeout.phase == MultiplierTrialPhase::kCooldown);

  // An explicit source cap must be confirmed once before the baseline starts.
  // After that initial application it remains ready across multiplier steps.
  latency::MultiplierTrial cap_pending;
  now = 0;
  for (int index = 0; index < 4; ++index, now += 500)
    cap_pending.Update(now, 5, 4, 4, true, trigger,
                       Sample(10000, 3000, 4000, 14000, 15000, 4500), false);
  CHECK(cap_pending.baseline_window.count == 0);
  for (int index = 0; index < 8; ++index, now += 500)
    cap_pending.Update(now, 5, 4, 4, true, trigger,
                       Sample(10000, 3000, 4000, 14000, 15000, 4500), true);
  CHECK(cap_pending.phase == MultiplierTrialPhase::kWaitingForCandidate);

  // A baseline is valid only while the signal that justified it remains
  // continuously present. Switching triggers restarts the eight-sample window.
  latency::MultiplierTrial changing_trigger;
  now = 0;
  for (int index = 0; index < 4; ++index, now += 500)
    changing_trigger.Update(
        now, 6, 4, 4, true, ResponsiveTrialReason::kHighInputLatency,
        Sample(10000, 0, 0, 65000, 67000, 1000));
  CHECK(changing_trigger.baseline_window.count == 4);
  changing_trigger.Update(
      now, 6, 4, 4, true, ResponsiveTrialReason::kHighPipelineLatency,
      Sample(10000, 0, 0, 65000, 67000, 1000));
  CHECK(changing_trigger.baseline_window.count == 1 &&
        changing_trigger.reason ==
            ResponsiveTrialReason::kHighPipelineLatency);

  // 3x and 2x are monitor-only: automatic control never reduces them.
  latency::MultiplierTrial floor_trial;
  for (uint32_t configured : {3u, 2u}) {
    for (int index = 0; index < 12; ++index, now += 500)
      CHECK(floor_trial.Update(
                now, 10 + configured, configured, configured, true, trigger,
                Sample(10000, 3000, 4000, 14000, 15000, 4500)) == 0);
    CHECK(floor_trial.phase == MultiplierTrialPhase::kIdle);
  }

  // An accepted reduction is periodically released so the original setting
  // can be measured again. If pressure has disappeared it remains restored.
  latency::MultiplierTrial reprobe;
  now = 0;
  for (int index = 0; index < 8; ++index, now += 500)
    reprobe.Update(now, 20, 4, 4, true, trigger,
                   Sample(10000, 3000, 4000, 14000, 15000, 4500));
  for (int index = 0; index < 8; ++index, now += 500)
    reprobe.Update(now, 20, 4, 3, true, trigger,
                   Sample(10000, 1200, 1800, 12000, 14000, 3200));
  CHECK(reprobe.phase == MultiplierTrialPhase::kAccepted);
  CHECK(reprobe.override_multiplier == 3);
  now += latency::MultiplierTrial::kAcceptedProbeMs;
  CHECK(reprobe.Update(
            now, 20, 4, 3, true, trigger,
            Sample(10000, 1200, 1800, 12000, 14000, 3200)) == 0);
  CHECK(reprobe.phase == MultiplierTrialPhase::kWaitingForOriginal);
  for (int index = 0; index < 8; ++index, now += 500)
    CHECK(reprobe.Update(
              now, 20, 4, 4, true, ResponsiveTrialReason::kNone,
              Sample(10000, 1000, 1400, 12500, 14200, 3000)) == 0);
  CHECK(reprobe.phase == MultiplierTrialPhase::kIdle);
  CHECK(reprobe.approved_multiplier == 4);
  CHECK(!reprobe.accepted);

  // Disabling automatic control or changing epoch restores the saved choice.
  CHECK(multiplier_trial.Update(now, 4, 6, 3, false, trigger,
                                Sample(10000, 1000, 1500, 11000, 13500, 2800)) == 0);
  CHECK(multiplier_trial.phase == MultiplierTrialPhase::kIdle);
  h = {}; // one hour of synthetic 100-FPS sampling; no real time sleeps
  for (uint64_t i=0; i<7200; ++i) {
    Fill(r,i*50); a=latency::Analyze(r.frames,10000000,500*i,11,h);
    CHECK(a.source_timing_confident == (i != 0));
    CHECK(a.median_queue_wait_us == 0);
  }
  latencytrace::Clear(); latencytrace::recording.store(true);
  for(uint64_t i=0;i<7200;++i) latencytrace::Record({i});
  latencytrace::recording.store(false);
  const auto csv=latencytrace::Csv();
  CHECK(csv.find("\n3600,")!=std::string::npos && csv.find("\n3599,")==std::string::npos);
  CHECK(csv.find("\n7199,")!=std::string::npos && latencytrace::dropped.load()==0);
  std::cout << "latency measurement and trial tests passed\n";
}
