// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace mfgunlock::xemfg::native {
// 1.5.7, [XeFG] DisplayCap (off by default): limit the source frame rate to
// the display refresh divided by the multiplier, a small margin below it so a
// variable-refresh display stays inside its range. Without it, a high source
// rate times the multiplier presents generated frames faster than the display
// shows them; they are dropped or queued, and motion becomes uneven.
inline constexpr uint32_t kDisplayCapMarginPermille=30; // 3%

// Minimum interval between presented frames in microseconds, or 0 when the
// refresh or the multiplier is unknown (no cap). XeLL bound to XeFG applies
// its minimum interval to every presented frame, generated ones included
// (Plague Tale field capture: an interval of refresh/multiplier held the
// output, not the game, at that rate), so the game then runs at the refresh
// over the multiplier.
inline constexpr uint32_t DisplayCapIntervalUs(uint32_t refreshMilliHz,uint32_t multiplier,
    uint32_t marginPermille=kDisplayCapMarginPermille) {
  if(refreshMilliHz<10000 || refreshMilliHz>1000000 || multiplier<2 || multiplier>6 || marginPermille>=1000) return 0;
  // interval = 1e6 us / (refresh * (1 - margin)), rounded up.
  const uint64_t numerator=uint64_t(1000000)*1000*1000;
  const uint64_t denominator=uint64_t(refreshMilliHz)*(1000-marginPermille);
  return uint32_t((numerator+denominator-1)/denominator);
}

// The game's own limit and the display cap together: the longer interval
// (the lower frame rate) wins; zero means no limit from that side.
inline constexpr uint32_t CombinedIntervalUs(uint32_t gameLimitUs,uint32_t capUs) {
  return gameLimitUs>capUs?gameLimitUs:capUs;
}

// Panel notice: the measured source interval times the multiplier presents
// faster than the display refresh.
inline constexpr bool ExceedsDisplay(uint32_t sourceIntervalUs,uint32_t refreshMilliHz,uint32_t multiplier) {
  if(!sourceIntervalUs || refreshMilliHz<10000 || multiplier<2) return false;
  // multiplier / interval > refresh  <=>  multiplier * 1e9 > refreshMilliHz * interval
  return uint64_t(multiplier)*1000000000ull>uint64_t(refreshMilliHz)*sourceIntervalUs;
}
}
