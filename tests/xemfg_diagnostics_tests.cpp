// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/native_diagnostics.hpp"
#include <iostream>
#include <stdexcept>
using namespace mfgunlock::xemfg::native;
namespace {
void Check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void Frame(uint32_t id,int query=0) {
  CpuProbe probe; probe.sample.frame=id; probe.sample.output=1; probe.sample.admitted=true;
  probe.sample.sdkQueryResult=query; probe.sample.sleepUs=100;
  probe.Locked(); probe.Prepared(); probe.Returned(); probe.Finish();
}
}
int main() {
  try {
    Check(!TimingEnabled(),"profiling enabled by default");
    { CpuProbe off; Check(!off.active,"disabled probe active"); off.Locked(); off.Prepared(); off.Returned(); off.Finish(); }
    Check(TimingReport().find("Samples=0")!=std::string::npos,"disabled probe recorded data");
    SetTimingEnabled(true); Frame(1); Frame(2); Frame(7); Frame(8,-4);
    const auto report=TimingReport();
    Check(report.find("Samples=4")!=std::string::npos && report.find("nonconsecutive_or_epoch_pairs=1")!=std::string::npos,"source gaps counted as regular intervals");
    Check(report.find("unavailable_SDK_status=1")!=std::string::npos && report.find("p99.9=")!=std::string::npos,"status domain or percentile missing");
    Check(report.find("Displayed FPS/intervals: unavailable")!=std::string::npos && report.find("physical latency: NOT MEASURED")!=std::string::npos,"CPU timing mislabeled");
    CpuProbe stale; ClearTiming(); stale.Finish();
    Check(TimingReport().find("Samples=0")!=std::string::npos,"clear accepted in-flight old capture");
    for(uint32_t i=1;i<=2100;++i) Frame(i);
    Check(TimingReport().find("Samples=2048")!=std::string::npos,"timing memory unbounded");
    SetTimingEnabled(false);
    std::cout<<"PASS: default-off profiling, bounded ring, reset generation, gaps, SDK status domain and CPU-only labels\n"; return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
