// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace mfgunlock::xemfg::native {
// Bounded association only. Never evict an in-flight/future frame to admit a
// new ID. Same capacity and two-frame late-marker retention as the old map.
template<class T,size_t Capacity=16> class FrameStore {
  std::array<T,Capacity> entries{};
public:
  T* Find(uint32_t id) noexcept {
    if(!id) return nullptr;
    for(auto& item:entries) if(item.id==id) return &item;
    return nullptr;
  }
  T* Acquire(uint32_t id) noexcept {
    if(!id) return nullptr;
    if(auto* found=Find(id)) return found;
    for(auto& item:entries) if(!item.id) { item=T{}; item.id=id; return &item; }
    return nullptr;
  }
  void Prune(uint32_t presented) noexcept {
    for(auto& item:entries) {
      const uint32_t age=presented-item.id;
      if(item.id && age>2 && age<0x80000000u) item=T{};
    }
  }
  void clear() noexcept { entries={}; }
  bool empty() const noexcept { return size()==0; }
  size_t size() const noexcept { size_t n=0; for(const auto& e:entries) n+=e.id!=0; return n; }
};
}
