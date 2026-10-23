// SPDX-License-Identifier: MIT
#pragma once
#include "vendor/runtime_layout.h"
#include "vendor/sha256_util.h"
#include <span>

namespace mfgunlock::xemfg::native::reflex_profile {
// Private external-latency selector, witnessed in XeFG 1.3.1.78. The setter
// recognizes this value BEFORE treating its argument as a XeLL pointer, selects
// the provider's external-latency path and returns success. No provider code or
// object field is written by the bridge. This is NOT a public Intel SDK contract.
// EnsureModel separately admits the complete file's exact SHA-256. Recheck both
// loaded bodies here, so an intervening modification cannot authorize this call.
inline bool SetterMatches(std::span<const unsigned char> body) noexcept {
  return body.size()==342 && sha256_util::EqualsUpperHex(body.data(),body.size(),
      "9841B679B4BAB43F2942B939F075A2193F5BC5143C29DB6A536C17EEBCC1BFC9",342);
}
inline bool Available(HMODULE module) noexcept {
  try {
    const xess_layout::Image image(module);
    std::array<unsigned char,342> setter{};
    std::array<unsigned char,139> enabled{};
    return image.Range(0x20e620,setter.size(),1) && image.Read(0x20e620,setter.data(),setter.size())
        && SetterMatches(setter) && image.Range(0x20aee0,enabled.size(),1)
        && image.Read(0x20aee0,enabled.data(),enabled.size())
        && sha256_util::EqualsUpperHex(enabled.data(),enabled.size(),
          "92ECDE8CCB74390F83AD08FECE5C524DB283D312B6BAEFDE03F5943AE30C6F8A",139);
  } catch(...) { return false; }
}
inline void* ExternalSelector() noexcept { return reinterpret_cast<void*>(uintptr_t{0x80deab86beaf04}); }
}
