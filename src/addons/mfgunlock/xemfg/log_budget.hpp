// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
namespace mfgunlock::xemfg::native {
// Repeated SDK messages differ only by frame numbers. Group them by pattern
// (digits removed) so one noisy phase cannot exhaust the log for the next.
// Admits the first three of each pattern, then the 16th, 256th, 4096th...
class PatternBudget {
  std::mutex lock;
  std::unordered_map<std::string,uint64_t> counts;
  size_t patterns;
public:
  explicit PatternBudget(size_t maxPatterns):patterns(maxPatterns) {}
  static std::string Pattern(const std::string& text) {
    std::string out; out.reserve(text.size());
    for(const char c:text) {
      if(c>='0' && c<='9') { if(out.empty() || out.back()!='#') out+='#'; }
      else out+=c;
    }
    return out;
  }
  // Returns 0 when suppressed, otherwise the occurrence count to report.
  uint64_t Admit(const std::string& key) {
    std::lock_guard guard(lock);
    auto it=counts.find(key);
    if(it==counts.end()) {
      if(counts.size()>=patterns) return 0;
      it=counts.emplace(key,0).first;
    }
    const auto n=++it->second;
    if(n<=3) return n;
    for(uint64_t power=16;power && power<=n;power*=16) if(power==n) return n;
    return 0;
  }
};
}
