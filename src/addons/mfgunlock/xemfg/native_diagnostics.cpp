// SPDX-License-Identifier: MIT
#include "native_diagnostics.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <cstring>
namespace mfgunlock::xemfg::native {
namespace {
constexpr size_t Capacity=2048;
struct Store {
  std::mutex lock;
  std::array<CpuSample,Capacity> samples{};
  size_t cursor{},count{};
  std::atomic<bool> enabled{false};
  std::atomic<uint64_t> generation{1},dropped{};
};
Store& Data() { static auto* data=new Store; return *data; }
int64_t Now() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
}
bool TimingEnabled() noexcept { return Data().enabled.load(std::memory_order_relaxed); }
void SetTimingEnabled(bool value) noexcept { Data().enabled=value; }
void ClearTiming() noexcept {
  auto& data=Data(); std::lock_guard lock(data.lock); ++data.generation;
  data.count=data.cursor=0; data.dropped=0;
}
CpuProbe::CpuProbe() noexcept:active(TimingEnabled()),generation(0) {
  if(active) { generation=Data().generation; sample={}; sample.enter=Now(); }
}
void CpuProbe::Locked() noexcept { if(active) sample.locked=Now(); }
void CpuProbe::Prepared() noexcept { if(active) sample.prepared=Now(); }
void CpuProbe::Returned() noexcept { if(active) sample.returned=Now(); }
void CpuProbe::Finish() noexcept {
  if(!active) return;
  sample.done=Now(); auto& data=Data();
  std::unique_lock lock(data.lock,std::try_to_lock);
  if(!lock.owns_lock()) { ++data.dropped; return; }
  if(generation!=data.generation) return;
  data.samples[data.cursor]=sample; data.cursor=(data.cursor+1)%Capacity;
  data.count=(std::min)(data.count+1,Capacity);
}
std::string TimingReport() {
  auto& data=Data(); std::vector<CpuSample> samples; uint64_t dropped{};
  { std::lock_guard lock(data.lock); samples.reserve(data.count);
    for(size_t i=0;i<data.count;++i) samples.push_back(data.samples[(data.cursor+Capacity-data.count+i)%Capacity]);
    dropped=data.dropped;
  }
  LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
  const double us=1.e6/double(frequency.QuadPart);
  std::vector<double> interval,wait,prepare,present,finish,sleep;
  size_t gaps{},failed{},bypassed{},unavailable{};
  for(size_t i=0;i<samples.size();++i) {
    const auto& s=samples[i];
    wait.push_back((s.locked-s.enter)*us); prepare.push_back((s.prepared-s.locked)*us);
    present.push_back((s.returned-s.prepared)*us); finish.push_back((s.done-s.returned)*us); sleep.push_back(double(s.sleepUs));
    failed+=s.presentResult<0 || (s.sdkQueryResult==0 && s.sdkResult<0); bypassed+=!s.admitted; unavailable+=s.sdkQueryResult!=0;
    if(i) { const auto& p=samples[i-1];
      if(s.output==p.output && s.epoch==p.epoch && uint32_t(s.frame-p.frame)==1 && s.enter>p.enter)
        interval.push_back((s.enter-p.enter)*us);
      else ++gaps;
    }
  }
  std::ostringstream report;
  report<<"XeMFG CPU timing ring (optional diagnostic; not displayed FPS)\nSamples="<<samples.size()<<" capacity="<<Capacity
      <<" dropped="<<dropped<<" nonconsecutive_or_epoch_pairs="<<gaps<<" failed_statuses="<<failed<<" unavailable_SDK_status="<<unavailable<<" bypassed="<<bypassed<<'\n';
  const auto stats=[&](const char* label,std::vector<double>& values) {
    if(values.empty()) { report<<label<<": unavailable\n"; return; }
    std::sort(values.begin(),values.end()); double sum=0; for(double value:values) sum+=value;
    const auto q=[&](double f){return values[size_t(std::ceil(f*values.size()))-1];};
    report<<label<<" (CPU us): n="<<values.size()<<" mean="<<sum/values.size()<<" p50="<<q(.5)<<" p95="<<q(.95)
        <<" p99="<<q(.99)<<" p99.9="<<q(.999)<<" max="<<values.back()<<'\n';
  };
  stats("Source Present intervals",interval); stats("Present serialization wait",wait); stats("BeforePresent",prepare);
  stats("SDK Present call",present); stats("AfterPresent/status",finish); stats("Source-frame latency wait (Reflex or XeLL)",sleep);
  report<<"Displayed FPS/intervals: unavailable internally; use PresentMon display metrics.\n"
      <<"GPU execution, queue depth/fence waits, physical latency: NOT MEASURED. SDK-reported outputs do not prove scanout.\n"
      <<"Percentiles: nearest rank; last bounded window, excludes nonconsecutive source IDs/epochs. Profiling adds CPU work.\n";
  return report.str();
}
}
extern "C" __declspec(dllexport) void MFGAXeTimingEnable(bool enabled) { mfgunlock::xemfg::native::SetTimingEnabled(enabled); }
extern "C" __declspec(dllexport) uint32_t MFGAXeTimingReport(char* destination,uint32_t capacity) {
  const auto report=mfgunlock::xemfg::native::TimingReport();
  if(destination && capacity>report.size()) memcpy(destination,report.c_str(),report.size()+1);
  return uint32_t(report.size()+1);
}
