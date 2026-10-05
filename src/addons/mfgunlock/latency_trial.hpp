/* SPDX-License-Identifier: MIT */
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace mfgunlock::latency {

// Conservative bounded source-rate trial, not a replacement for NVIDIA's
// frame scheduler.
struct QueueTrial {
  uint64_t epoch = 0, started = 0, cooldown_until = 0;
  uint32_t cap = 0, candidate = 0, stable = 0, multiplier = 0;
  uint32_t baseline_interval = 0, baseline_queue = 0, baseline_latency = 0;
  bool accepted = false;
  void Stop(uint64_t now) {
    cap = candidate = stable = 0;
    accepted = false;
    cooldown_until = now + 30000;
  }
  uint32_t Update(uint64_t now, uint64_t new_epoch, uint32_t multi,
                  bool safe, uint32_t recommended, uint32_t interval,
                  uint32_t queue, uint32_t pipeline) {
    if (new_epoch != epoch || multi != multiplier) {
      Stop(now);
      epoch = new_epoch;
      multiplier = multi;
    }
    if (!safe) {
      if (cap) Stop(now);
      stable = 0;
      return 0;
    }
    if (cap) {
      if (uint64_t(interval) * 100 > uint64_t(baseline_interval) * 108 ||
          uint64_t(interval) * 100 < uint64_t(baseline_interval) * 85 ||
          pipeline > baseline_latency + 1000) {
        Stop(now);
        return 0;
      }
      if (!accepted && now - started >= 4000) {
        accepted = uint64_t(queue) * 100 <= uint64_t(baseline_queue) * 80 &&
                   pipeline + 250 < baseline_latency;
        if (!accepted) {
          Stop(now);
          return 0;
        }
      }
      if (now - started >= 30000) {
        Stop(now);
        return 0;
      }
      return cap;
    }
    if (now < cooldown_until || recommended == 0) {
      stable = 0;
      return 0;
    }
    const auto difference = recommended > candidate
                                ? recommended - candidate
                                : candidate - recommended;
    stable = candidate && difference <= 1 ? stable + 1 : 1;
    candidate = recommended;
    if (stable >= 4) {
      cap = candidate;
      started = now;
      accepted = false;
      baseline_interval = interval;
      baseline_queue = queue;
      baseline_latency = pipeline;
    }
    return cap;
  }
};

enum class ResponsiveTrialReason : uint32_t {
  kNone = 0,
  kDisplayOversubscription,
  kRenderQueue,
  kHighInputLatency,
  kHighPipelineLatency,
  kGpuSaturatedLatencyProxy,
  kFrameGenerationWorkload,
};

enum class ResponsiveTrialBlocker : uint32_t {
  kNone = 0,
  kMonitorOnly,
  kReflexUnhealthy,
  kSourceTimingUnverified,
  kDynamicActive,
  kFixedMultiplierRequired,
  kLiveMultiplierUnconfirmed,
  kReflexOptionsUnavailable,
  kUserCapPending,
  kUserCapRejected,
  kNoTrigger,
};

inline constexpr uint32_t kHighPipelineLatencyUs = 60000;
inline constexpr uint32_t kGpuProxyPipelineLatencyUs = 50000;

inline constexpr ResponsiveTrialReason ClassifyResponsiveTrialReason(
    bool output_oversubscribed, uint32_t source_interval_us,
    uint32_t queue_p95_us, uint32_t input_to_gpu_us,
    uint32_t pipeline_us, uint32_t gpu_active_us, uint32_t ai_us) {
  const bool queue_pressure = source_interval_us != 0 &&
      queue_p95_us >= 1500 &&
      uint64_t(queue_p95_us) * 4 >= source_interval_us;
  if (output_oversubscribed)
    return ResponsiveTrialReason::kDisplayOversubscription;
  if (queue_pressure) return ResponsiveTrialReason::kRenderQueue;
  if (input_to_gpu_us >= kHighPipelineLatencyUs)
    return ResponsiveTrialReason::kHighInputLatency;
  // Marker-to-GPU is not end-to-end display latency. Crossing the threshold
  // only starts a measured trial; it never makes a reduction permanent
  // without a better median, a non-regressing p95 and stable FPS.
  if (pipeline_us >= kHighPipelineLatencyUs)
    return ResponsiveTrialReason::kHighPipelineLatency;
  // When a game does not emit input markers, a saturated base-GPU frame plus
  // an already-long marker pipeline is a conservative responsiveness proxy.
  // The measured before/after acceptance still decides whether any reduction
  // survives the trial.
  if (input_to_gpu_us == 0 && source_interval_us != 0 &&
      pipeline_us >= kGpuProxyPipelineLatencyUs &&
      uint64_t(gpu_active_us) * 100 >=
          uint64_t(source_interval_us) * 80) {
    return ResponsiveTrialReason::kGpuSaturatedLatencyProxy;
  }
  // Reuse the established 3-ms significance floor, but also require the FG
  // workload to consume at least one fifth of a real-frame interval.
  if (source_interval_us != 0 && ai_us >= 3000 &&
      uint64_t(ai_us) * 5 >= source_interval_us) {
    return ResponsiveTrialReason::kFrameGenerationWorkload;
  }
  return ResponsiveTrialReason::kNone;
}

enum class MultiplierTrialPhase : uint32_t {
  kIdle = 0,
  kCollectingBaseline,
  kWaitingForCandidate,
  kMeasuringCandidate,
  kAccepted,
  kWaitingForOriginal,
  kMeasuringOriginal,
  kCooldown,
};

struct MultiplierTrialSample {
  uint32_t source_interval_us = 0;
  uint32_t queue_us = 0;
  uint32_t queue_p95_us = 0;
  uint32_t pipeline_us = 0;
  uint32_t pipeline_p95_us = 0;
  uint32_t ai_us = 0;
};

struct MultiplierTrialSummary {
  uint32_t source_interval_us = 0;
  uint32_t queue_us = 0;
  uint32_t queue_p95_us = 0;
  uint32_t pipeline_us = 0;
  uint32_t pipeline_p95_us = 0;
  uint32_t ai_us = 0;
  uint32_t samples = 0;
};

// Eight 500-ms observations form one four-second measurement window. Storage
// is fixed and entirely stack/static owned; the Present hot path never allocates.
struct MultiplierTrialWindow {
  static constexpr size_t kCapacity = 8;
  std::array<MultiplierTrialSample, kCapacity> values{};
  size_t count = 0;

  void Clear() { count = 0; }
  bool Full() const { return count == kCapacity; }

  bool Add(const MultiplierTrialSample& sample) {
    if (sample.source_interval_us == 0 || sample.pipeline_us == 0 ||
        sample.pipeline_p95_us == 0 || count >= kCapacity) {
      return false;
    }
    values[count++] = sample;
    return true;
  }

  template <typename Getter>
  uint32_t Median(Getter getter) const {
    if (count == 0) return 0;
    std::array<uint32_t, kCapacity> ordered{};
    for (size_t index = 0; index < count; ++index)
      ordered[index] = getter(values[index]);
    std::sort(ordered.begin(), ordered.begin() + count);
    return ordered[count / 2];
  }

  MultiplierTrialSummary Summarize() const {
    MultiplierTrialSummary result{};
    result.samples = static_cast<uint32_t>(count);
    result.source_interval_us = Median(
        [](const auto& value) { return value.source_interval_us; });
    result.queue_us = Median([](const auto& value) { return value.queue_us; });
    result.queue_p95_us =
        Median([](const auto& value) { return value.queue_p95_us; });
    result.pipeline_us =
        Median([](const auto& value) { return value.pipeline_us; });
    result.pipeline_p95_us =
        Median([](const auto& value) { return value.pipeline_p95_us; });
    result.ai_us = Median([](const auto& value) { return value.ai_us; });
    return result;
  }
};

inline constexpr bool MultiplierTrialImproved(
    const MultiplierTrialSummary& baseline,
    const MultiplierTrialSummary& candidate) {
  if (baseline.samples < MultiplierTrialWindow::kCapacity ||
      candidate.samples < MultiplierTrialWindow::kCapacity ||
      baseline.source_interval_us == 0 || baseline.pipeline_us == 0 ||
      baseline.pipeline_p95_us == 0 || candidate.source_interval_us == 0 ||
      candidate.pipeline_us == 0 || candidate.pipeline_p95_us == 0) {
    return false;
  }
  const bool source_stable =
      uint64_t(candidate.source_interval_us) * 100 <=
          uint64_t(baseline.source_interval_us) * 108;
  const bool median_better =
      candidate.pipeline_us + 250 < baseline.pipeline_us;
  const bool tail_not_worse =
      candidate.pipeline_p95_us <= baseline.pipeline_p95_us;
  return source_stable && median_better && tail_not_worse;
}

// Progressively tests 6x->5x->4x->3x. Each step uses an eight-sample baseline
// and candidate window. The saved selection is never changed: zero means use
// the configured multiplier, otherwise the returned value is a runtime-only
// override picked up by the next normal game slDLSSGSetOptions submission.
struct MultiplierTrial {
  static constexpr uint64_t kApplyTimeoutMs = 3000;
  static constexpr uint64_t kAcceptedProbeMs = 30000;
  static constexpr uint64_t kCooldownMs = 30000;

  uint64_t epoch = 0;
  uint64_t phase_started = 0;
  uint64_t accepted_started = 0;
  uint64_t cooldown_until = 0;
  uint32_t configured = 0;
  uint32_t approved_multiplier = 0;
  uint32_t candidate_multiplier = 0;
  uint32_t override_multiplier = 0;
  ResponsiveTrialReason reason = ResponsiveTrialReason::kNone;
  MultiplierTrialPhase phase = MultiplierTrialPhase::kIdle;
  MultiplierTrialWindow baseline_window{};
  MultiplierTrialWindow candidate_window{};
  MultiplierTrialSummary baseline{};
  MultiplierTrialSummary last_compared_baseline{};
  MultiplierTrialSummary latest_trial{};
  bool accepted = false;
  bool attempted = false;

  void Reset(uint64_t now, uint64_t new_epoch, uint32_t configured_value,
             bool cooldown = false) {
    epoch = new_epoch;
    configured = configured_value;
    approved_multiplier = configured_value;
    candidate_multiplier = 0;
    override_multiplier = 0;
    reason = ResponsiveTrialReason::kNone;
    phase = cooldown ? MultiplierTrialPhase::kCooldown
                     : MultiplierTrialPhase::kIdle;
    phase_started = now;
    accepted_started = 0;
    cooldown_until = cooldown ? now + kCooldownMs : 0;
    baseline_window.Clear();
    candidate_window.Clear();
    baseline = {};
    last_compared_baseline = {};
    latest_trial = {};
    accepted = false;
    attempted = false;
  }

  void StartBaseline(uint64_t now, ResponsiveTrialReason new_reason) {
    reason = new_reason;
    phase = MultiplierTrialPhase::kCollectingBaseline;
    phase_started = now;
    baseline_window.Clear();
    candidate_window.Clear();
    last_compared_baseline = {};
    latest_trial = {};
  }

  void StartNextCandidate(uint64_t now) {
    candidate_multiplier = approved_multiplier > 3
                               ? approved_multiplier - 1
                               : 0;
    if (candidate_multiplier < 3) {
      candidate_multiplier = 0;
      phase = MultiplierTrialPhase::kAccepted;
      accepted_started = now;
      return;
    }
    override_multiplier = candidate_multiplier;
    candidate_window.Clear();
    phase = MultiplierTrialPhase::kWaitingForCandidate;
    phase_started = now;
    attempted = true;
  }

  void HoldApproved(uint64_t now, bool use_cooldown) {
    candidate_multiplier = 0;
    override_multiplier = approved_multiplier < configured
                              ? approved_multiplier
                              : 0;
    accepted = approved_multiplier < configured;
    accepted_started = now;
    if (accepted) {
      phase = MultiplierTrialPhase::kAccepted;
    } else {
      phase = use_cooldown ? MultiplierTrialPhase::kCooldown
                           : MultiplierTrialPhase::kIdle;
      cooldown_until = use_cooldown ? now + kCooldownMs : 0;
    }
    phase_started = now;
  }

  uint32_t Update(uint64_t now, uint64_t new_epoch,
                  uint32_t configured_value, uint32_t live_multiplier,
                  bool safe, ResponsiveTrialReason trigger_reason,
                  const MultiplierTrialSample& sample,
                  bool sample_ready = true) {
    if (new_epoch != epoch || configured_value != configured)
      Reset(now, new_epoch, configured_value);
    if (!safe || configured < 4 || configured > 6) {
      if (override_multiplier != 0 || phase != MultiplierTrialPhase::kIdle)
        Reset(now, new_epoch, configured_value);
      return 0;
    }

    if (phase == MultiplierTrialPhase::kCooldown) {
      if (now < cooldown_until) return 0;
      phase = MultiplierTrialPhase::kIdle;
      attempted = false;
    }

    if (phase == MultiplierTrialPhase::kIdle) {
      if (trigger_reason == ResponsiveTrialReason::kNone ||
          live_multiplier != configured) {
        return 0;
      }
      approved_multiplier = configured;
      StartBaseline(now, trigger_reason);
    }

    if (phase == MultiplierTrialPhase::kCollectingBaseline) {
      if (trigger_reason == ResponsiveTrialReason::kNone ||
          live_multiplier != approved_multiplier) {
        HoldApproved(now, false);
        return override_multiplier;
      }
      if (trigger_reason != reason) StartBaseline(now, trigger_reason);
      if (!sample_ready) {
        baseline_window.Clear();
        return override_multiplier;
      }
      if (!baseline_window.Add(sample)) {
        HoldApproved(now, false);
        return override_multiplier;
      }
      if (baseline_window.Full()) {
        baseline = baseline_window.Summarize();
        StartNextCandidate(now);
      }
      return override_multiplier;
    }

    if (phase == MultiplierTrialPhase::kWaitingForCandidate) {
      if (live_multiplier == candidate_multiplier && sample_ready) {
        phase = MultiplierTrialPhase::kMeasuringCandidate;
        phase_started = now;
        candidate_window.Clear();
        if (!candidate_window.Add(sample)) {
          HoldApproved(now, true);
          return override_multiplier;
        }
      } else if (now - phase_started >= kApplyTimeoutMs) {
        HoldApproved(now, true);
      }
      return override_multiplier;
    }

    if (phase == MultiplierTrialPhase::kMeasuringCandidate) {
      if (!sample_ready) {
        candidate_window.Clear();
        phase = MultiplierTrialPhase::kWaitingForCandidate;
        phase_started = now;
        return override_multiplier;
      }
      if (live_multiplier != candidate_multiplier ||
          !candidate_window.Add(sample)) {
        HoldApproved(now, true);
        return override_multiplier;
      }
      if (!candidate_window.Full()) return override_multiplier;

      last_compared_baseline = baseline;
      latest_trial = candidate_window.Summarize();
      if (!MultiplierTrialImproved(baseline, latest_trial)) {
        HoldApproved(now, true);
        return override_multiplier;
      }

      approved_multiplier = candidate_multiplier;
      accepted = true;
      accepted_started = now;
      baseline = latest_trial;
      baseline_window = candidate_window;
      StartNextCandidate(now);
      return override_multiplier;
    }

    if (phase == MultiplierTrialPhase::kAccepted) {
      override_multiplier = approved_multiplier < configured
                                ? approved_multiplier
                                : 0;
      if (accepted && now - accepted_started >= kAcceptedProbeMs) {
        override_multiplier = 0;
        candidate_multiplier = configured;
        candidate_window.Clear();
        phase = MultiplierTrialPhase::kWaitingForOriginal;
        phase_started = now;
      }
      return override_multiplier;
    }

    if (phase == MultiplierTrialPhase::kWaitingForOriginal) {
      if (live_multiplier == configured && sample_ready) {
        phase = MultiplierTrialPhase::kMeasuringOriginal;
        phase_started = now;
        candidate_window.Clear();
        if (!candidate_window.Add(sample)) {
          HoldApproved(now, false);
          return override_multiplier;
        }
      } else if (now - phase_started >= kApplyTimeoutMs) {
        HoldApproved(now, false);
      }
      return override_multiplier;
    }

    if (phase == MultiplierTrialPhase::kMeasuringOriginal) {
      if (!sample_ready) {
        candidate_window.Clear();
        phase = MultiplierTrialPhase::kWaitingForOriginal;
        phase_started = now;
        return override_multiplier;
      }
      if (live_multiplier != configured || !candidate_window.Add(sample)) {
        HoldApproved(now, false);
        return override_multiplier;
      }
      if (!candidate_window.Full()) return override_multiplier;

      approved_multiplier = configured;
      accepted = false;
      override_multiplier = 0;
      baseline = candidate_window.Summarize();
      if (trigger_reason != ResponsiveTrialReason::kNone) {
        reason = trigger_reason;
        StartNextCandidate(now);
      } else {
        phase = MultiplierTrialPhase::kIdle;
        candidate_multiplier = 0;
        attempted = false;
      }
      return override_multiplier;
    }

    return override_multiplier;
  }
};

}  // namespace mfgunlock::latency
