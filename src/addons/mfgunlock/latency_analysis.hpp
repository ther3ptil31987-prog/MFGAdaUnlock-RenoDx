/* SPDX-License-Identifier: MIT
 * Pure, bounded Reflex report analysis. No API calls or controller side effects.
 */
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace mfgunlock::latency {
enum class Units : uint32_t { kUnknown, kMicroseconds, kQpc };
enum TimingIssue : uint32_t {
  kTimingOk = 0,
  kTimingNotFresh = 1u << 0,
  kTimingUnitsUnknown = 1u << 1,
  kTimingInsufficientConsecutiveFrames = 1u << 2,
  kTimingSimulationCadenceInvalid = 1u << 3,
  kTimingPresentCadenceMissing = 1u << 4,
  kTimingPresentCadenceMismatch = 1u << 5,
  kTimingSimulationCadenceUnstable = 1u << 6,
  kTimingPresentCadenceUnstable = 1u << 7,
};
struct History {
  uint64_t epoch = 0, frame = 0, gpu_end = 0, polled_ms = 0;
  bool seen = false;
};
template <class Frame> struct Report {
  Frame latest{}, previous{};
  uint32_t valid_latency_frames = 0, consecutive_timing_samples = 0;
  uint32_t source_interval_us = 0, simulation_interval_us = 0;
  uint32_t median_queue_wait_us = 0, median_pipeline_latency_us = 0;
  uint32_t p95_queue_wait_us = 0, p95_pipeline_latency_us = 0;
  uint32_t median_input_to_gpu_end_us = 0, median_gpu_frame_time_us = 0;
  uint32_t p95_gpu_frame_time_us = 0, median_gpu_active_us = 0;
  uint32_t median_input_to_simulation_us = 0;
  uint32_t median_simulation_cpu_us = 0, median_submit_cpu_us = 0;
  uint32_t median_ai_frame_time_us = 0, new_frames = 0;
  uint32_t source_timing_issue_mask = kTimingOk;
  Units timestamp_units = Units::kUnknown;
  uint64_t qpc_frequency = 0;
  bool fresh = false, source_timing_confident = false;
  bool queue_timing_confident = false;
};
inline uint64_t ToUs(uint64_t value, Units units, uint64_t frequency) {
  if (units == Units::kMicroseconds) return value;
  if (units != Units::kQpc || frequency == 0 ||
      value > UINT64_MAX / 1000000ull) return 0;
  return value * 1000000ull / frequency;
}
inline bool Near(uint64_t a, uint64_t b, uint64_t percent = 2) {
  const auto delta = a > b ? a - b : b - a;
  return b != 0 && delta <= (std::max)(uint64_t{5}, b * percent / 100);
}
inline uint64_t AsQpcTicks(uint64_t microseconds, uint64_t frequency) {
  if (frequency == 0) return 0;
  const uint64_t whole = microseconds / 1000000ull;
  if (whole > UINT64_MAX / frequency) return 0;
  const uint64_t base = whole * frequency;
  const uint64_t remainder = microseconds % 1000000ull;
  const uint64_t tail = remainder * frequency / 1000000ull;
  return base <= UINT64_MAX - tail ? base + tail : 0;
}
inline bool Within(uint64_t value, uint64_t reference, uint64_t tolerance) {
  return (value > reference ? value - reference : reference - value) <= tolerance;
}
template <class Frame> bool Complete(const Frame& f) {
  return f.simulation_start_time && f.simulation_end_time >= f.simulation_start_time &&
      f.render_submit_start_time && f.render_submit_end_time >= f.render_submit_start_time &&
      f.present_start_time && f.present_end_time >= f.present_start_time &&
      f.gpu_render_start_time && f.gpu_render_end_time >= f.gpu_render_start_time &&
      f.gpu_render_end_time >= f.simulation_start_time;
}
template <size_t N> struct Samples {
  std::array<uint32_t, N> values{};
  size_t count = 0;
  void Add(uint64_t v) { if (v <= UINT32_MAX && count < N) values[count++] = uint32_t(v); }
  void Sort() { std::sort(values.begin(), values.begin() + count); }
  uint32_t P(size_t numerator, size_t denominator = 100) const {
    return count ? values[(count - 1) * numerator / denominator] : 0;
  }
};
template <class Frame, size_t N>
Report<Frame> Analyze(const Frame (&frames)[N], uint64_t frequency,
                      uint64_t now_ms, uint64_t epoch, History& history,
                      uint64_t current_qpc = 0) {
  Report<Frame> out{};
  out.qpc_frequency = frequency;
  size_t pairs = 0, direct_matches = 0, qpc_matches = 0;
  for (size_t i = 1; i < N; ++i) {
    const auto& a = frames[i-1]; const auto& b = frames[i];
    if (!Complete(a) || !Complete(b) || a.frame_id == UINT64_MAX ||
        b.frame_id != a.frame_id + 1 || b.gpu_render_end_time <= a.gpu_render_end_time ||
        b.gpu_frame_time_us < 1000 || b.gpu_frame_time_us > 100000) continue;
    ++pairs;
    const auto delta = b.gpu_render_end_time - a.gpu_render_end_time;
    direct_matches += Near(delta, b.gpu_frame_time_us);
    qpc_matches += Near(ToUs(delta, Units::kQpc, frequency), b.gpu_frame_time_us);
  }
  const bool direct = pairs >= 48 && direct_matches * 100 >= pairs * 95;
  const bool qpc = pairs >= 48 && qpc_matches * 100 >= pairs * 95;
  uint64_t latest_gpu_end = 0;
  for (const auto& frame : frames) {
    if (Complete(frame)) latest_gpu_end = frame.gpu_render_end_time;
  }
  // QueryPerformanceCounter is sampled immediately after the NVAPI call, so
  // one second is intentionally generous while still separating raw QPC ticks
  // from microseconds at common 10 MHz counter frequencies.
  const uint64_t tolerance = frequency;
  const bool raw_matches_qpc = current_qpc != 0 && latest_gpu_end != 0 &&
      Within(latest_gpu_end, current_qpc, tolerance);
  const uint64_t microseconds_as_qpc =
      AsQpcTicks(latest_gpu_end, frequency);
  const bool microseconds_match_qpc = current_qpc != 0 &&
      microseconds_as_qpc != 0 &&
      Within(microseconds_as_qpc, current_qpc, tolerance);
  if (raw_matches_qpc != microseconds_match_qpc) {
    out.timestamp_units = raw_matches_qpc ? Units::kQpc
                                          : Units::kMicroseconds;
  } else if (direct && (!qpc || frequency == 1000000)) {
    out.timestamp_units = Units::kMicroseconds;
  } else if (qpc && !direct) {
    out.timestamp_units = Units::kQpc;
  }

  Samples<N> simulation, present, gpu, gpu_active, queue, pipeline, input,
      input_to_simulation, simulation_cpu, submit_cpu, ai;
  uint32_t run = 0;
  auto duration = [&](uint64_t a, uint64_t b, auto& samples) {
    // Zero duration is valid; missing endpoints are not.
    if (a && b && b >= a) {
      const auto us = ToUs(b - a, out.timestamp_units, frequency);
      if (out.timestamp_units != Units::kUnknown && us <= 1000000) samples.Add(us);
    }
  };
  for (size_t i = 0; i < N; ++i) {
    const auto& f = frames[i];
    if (!Complete(f)) { run = 0; continue; }
    ++out.valid_latency_frames;
    out.previous = out.latest; out.latest = f;
    if (i && Complete(frames[i-1]) && frames[i-1].frame_id != UINT64_MAX &&
        f.frame_id == frames[i-1].frame_id + 1 &&
        f.simulation_start_time > frames[i-1].simulation_start_time &&
        f.present_start_time > frames[i-1].present_start_time &&
        f.gpu_render_end_time > frames[i-1].gpu_render_end_time) {
      ++run;
      duration(frames[i-1].simulation_start_time, f.simulation_start_time, simulation);
      duration(frames[i-1].present_start_time, f.present_start_time, present);
    } else run = 0;
    if (f.gpu_frame_time_us >= 1000 && f.gpu_frame_time_us <= 100000) gpu.Add(f.gpu_frame_time_us);
    if (f.gpu_active_render_time_us && f.gpu_active_render_time_us <= 1000000)
      gpu_active.Add(f.gpu_active_render_time_us);
    duration(f.os_render_queue_start_time, f.gpu_render_start_time, queue);
    duration(f.simulation_start_time, f.gpu_render_end_time, pipeline);
    duration(f.input_sample_time, f.gpu_render_end_time, input);
    duration(f.input_sample_time, f.simulation_start_time, input_to_simulation);
    duration(f.simulation_start_time, f.simulation_end_time, simulation_cpu);
    duration(f.render_submit_start_time, f.render_submit_end_time, submit_cpu);
    if (f.ai_frame_time_us) ai.Add(f.ai_frame_time_us);
  }
  simulation.Sort(); present.Sort(); gpu.Sort(); gpu_active.Sort(); queue.Sort();
  pipeline.Sort(); input.Sort(); input_to_simulation.Sort();
  simulation_cpu.Sort(); submit_cpu.Sort(); ai.Sort();
  out.consecutive_timing_samples = run; // contiguous suffix, not sum across gaps
  out.simulation_interval_us = simulation.P(50);
  out.median_gpu_frame_time_us = gpu.P(50);
  out.p95_gpu_frame_time_us = gpu.P(95);
  out.median_gpu_active_us = gpu_active.P(50);
  out.median_queue_wait_us = queue.P(50);
  out.p95_queue_wait_us = queue.P(95);
  out.median_pipeline_latency_us = pipeline.P(50);
  out.p95_pipeline_latency_us = pipeline.P(95);
  out.median_input_to_gpu_end_us = input.P(50);
  out.median_input_to_simulation_us = input_to_simulation.P(50);
  out.median_simulation_cpu_us = simulation_cpu.P(50);
  out.median_submit_cpu_us = submit_cpu.P(50);
  out.median_ai_frame_time_us = ai.P(50);
  if (history.seen && history.epoch == epoch && now_ms >= history.polled_ms &&
      now_ms - history.polled_ms <= 2000 && out.latest.frame_id > history.frame &&
      out.latest.gpu_render_end_time > history.gpu_end) {
    out.new_frames = uint32_t((std::min)(out.latest.frame_id - history.frame, uint64_t{N}));
    out.fresh = out.new_frames >= 8;
  }
  history = {epoch, out.latest.frame_id, out.latest.gpu_render_end_time, now_ms, true};
  // Source cadence is an application-marker measurement. Corroborate the
  // simulation cadence with Present markers, but do not require NVIDIA's
  // gpu_frame_time_us to mean the same thing: its scope differs across driver
  // and DLSS-G integrations (base GPU work, generated workload or a larger
  // interval). Queue completeness is tracked separately so read-only source
  // FPS can remain useful while automatic actions still fail closed.
  if (!out.fresh) out.source_timing_issue_mask |= kTimingNotFresh;
  if (out.timestamp_units == Units::kUnknown)
    out.source_timing_issue_mask |= kTimingUnitsUnknown;
  if (run < 48)
    out.source_timing_issue_mask |= kTimingInsufficientConsecutiveFrames;
  if (out.simulation_interval_us < 1000 ||
      out.simulation_interval_us > 100000)
    out.source_timing_issue_mask |= kTimingSimulationCadenceInvalid;
  if (present.count < 48)
    out.source_timing_issue_mask |= kTimingPresentCadenceMissing;
  if (present.count >= 48 &&
      !Near(present.P(50), out.simulation_interval_us, 20))
    out.source_timing_issue_mask |= kTimingPresentCadenceMismatch;
  if (simulation.count >= 48 &&
      simulation.P(90) > uint64_t(simulation.P(50)) * 3 / 2)
    out.source_timing_issue_mask |= kTimingSimulationCadenceUnstable;
  if (present.count >= 48 &&
      present.P(90) > uint64_t(present.P(50)) * 3 / 2)
    out.source_timing_issue_mask |= kTimingPresentCadenceUnstable;
  out.source_timing_confident = out.source_timing_issue_mask == kTimingOk;
  out.queue_timing_confident = out.source_timing_confident &&
      queue.count >= 48;
  if (out.source_timing_confident) out.source_interval_us = out.simulation_interval_us;
  return out;
}
} // namespace mfgunlock::latency
