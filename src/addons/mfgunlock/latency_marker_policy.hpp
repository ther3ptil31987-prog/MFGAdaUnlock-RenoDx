// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
namespace mfgunlock::latencymarkers {
enum class Decision { Invalid, Duplicate, Forward };
// A wait called after this token's simulation is the boundary BEFORE the
// following simulation. Never attach it retroactively to completed markers.
// Gaps still fail closed: only the exact next token can consume this wait.
inline uint32_t SourceSleepFrame(uint32_t source,uint32_t observed) {
  if(!source) return 0;
  const auto simulation=observed&3u;
  if(simulation==3u) return source+1; // zero on wrap is intentionally inadmissible
  if(simulation) return 0; // a wait inside simulation has no safe translation
  return source;
}
inline Decision Classify(uint32_t mask,uint32_t marker,bool slept,bool inFlight=false) {
  if(marker>6 || !slept || inFlight) return Decision::Invalid;
  if(mask&(1u<<marker)) return Decision::Duplicate;
  if((marker==1 || marker==3 || marker==5) && !(mask&(1u<<(marker-1)))) return Decision::Invalid;
  return Decision::Forward;
}
// Admission is deliberately separate from a pacer/SDK context. It cannot
// authorize a GPU mode change, replace a native sleep or fabricate markers.
// All six source markers must be observed after the same frame's sleep.
class Admission {
  struct Frame { bool used{},slept{},complete{}; uint32_t id{},mask{}; };
  std::array<Frame,16> frames{};
  uint64_t epoch{}; uint32_t thread{},lastComplete{},consecutive{},lastSleep{}; bool haveLast{},haveSleep{};
public:
  void Reset() { frames={}; epoch=0;thread=lastComplete=consecutive=lastSleep=0;haveLast=haveSleep=false; }
  bool Sleep(uint64_t generation,uint32_t owner,uint32_t id) {
    if(!owner) { Reset();return false; }
    if(epoch!=generation || thread!=owner) { Reset();epoch=generation;thread=owner; }
    for(const auto& f:frames) if(f.used && f.id==id) return false; // no duplicate waits
    if(haveSleep && uint32_t(id-lastSleep)>=0x80000000u) { Reset();return false; }
    for(auto& f:frames) {
      const uint32_t age=id-f.id;
      if(!f.used || (age>2 && age<0x80000000u)) {
        f={true,true,false,id,0};lastSleep=id;haveSleep=true;return true;
      }
    }
    Reset(); return false;
  }
  Decision Marker(uint64_t generation,uint32_t owner,uint32_t id,uint32_t marker) {
    if(generation!=epoch || owner!=thread) { Reset();return Decision::Invalid; }
    for(auto& f:frames) if(f.used && f.id==id) {
      const auto decision=Classify(f.mask,marker,f.slept);
      if(decision==Decision::Invalid) { Reset();return decision; }
      if(decision==Decision::Duplicate) return decision;
      f.mask|=1u<<marker;
      if((f.mask&63u)==63u && !f.complete) {
        f.complete=true;
        consecutive=haveLast && uint32_t(id-lastComplete)==1?consecutive+1:1;
        lastComplete=id; haveLast=true;
      }
      return decision;
    }
    Reset();return Decision::Invalid;
  }
  uint32_t Consecutive() const { return consecutive; }
  bool Ready() const { return consecutive>=32; }
};
}
