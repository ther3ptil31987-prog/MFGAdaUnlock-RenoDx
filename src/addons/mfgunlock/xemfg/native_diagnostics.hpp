// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
namespace mfgunlock::xemfg::native {
// All timestamps below are CPU QPC observations. No display, GPU duration or
// physical-latency estimate is substituted when those measurements are absent.
struct CpuSample {
  int64_t enter,locked,prepared,returned,done;
  uint64_t epoch,output,sleepUs;
  uint32_t frame,requestedOutputs,sdkOutputs;
  int32_t presentResult,sdkResult,sdkQueryResult;
  bool admitted,sdkEnabled;
};
bool TimingEnabled() noexcept;
void SetTimingEnabled(bool enabled) noexcept;
void ClearTiming() noexcept;
std::string TimingReport();
struct CpuProbe {
  bool active;
  uint64_t generation;
  CpuSample sample;
  CpuProbe() noexcept;
  void Locked() noexcept;
  void Prepared() noexcept;
  void Returned() noexcept;
  void Finish() noexcept;
};
}
