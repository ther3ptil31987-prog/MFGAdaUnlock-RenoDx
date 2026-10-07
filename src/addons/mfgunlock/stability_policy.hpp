#pragma once
#include <cstdint>

namespace mfgunlock::stability {
// 0 = no successful live submission, 1 = split input, 2 = final color.
// Clearing a tag ends a resource lifetime; it does not prove that an FG
// evaluation consumed a different input path.
inline constexpr bool NeedsInputReset(uint32_t previous, uint32_t current) {
  return previous == 0 ? current == 2 : previous != current;
}
inline constexpr bool FreshLiveMfg(uint32_t multiplier, uint64_t observed,
                                    uint64_t now, bool accepted_fg) {
  return accepted_fg && multiplier >= 2 && multiplier <= 6 && observed != 0 &&
         now >= observed && now - observed <= 1500;
}
}  // namespace mfgunlock::stability
