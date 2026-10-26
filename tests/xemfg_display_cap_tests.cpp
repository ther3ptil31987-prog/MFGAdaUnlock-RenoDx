// SPDX-License-Identifier: MIT
#include <cstdlib>
#include <iostream>

#include "../src/addons/mfgunlock/xemfg/display_cap.hpp"

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition     \
                << '\n';                                                        \
      std::abort();                                                              \
    }                                                                           \
  } while (false)

using namespace mfgunlock::xemfg::native;

// Presented frame rate implied by an interval, in thousandths of a frame per second.
constexpr uint64_t OutputMilliFps(uint32_t intervalUs) { return 1000000000ull/intervalUs; }

int main() {
  // XeLL bound to XeFG limits every presented frame: 165 Hz -> 160.05 FPS
  // presented, whatever the multiplier (40.01 game FPS at 4x).
  const auto i165=DisplayCapIntervalUs(165000,4);
  CHECK(i165==6249);
  CHECK(DisplayCapIntervalUs(165000,2)==i165);
  // Every common refresh stays below the display refresh, and the game rate
  // (presented / multiplier) does not trigger the notice.
  for(uint32_t hz:{60u,120u,144u,165u,240u,360u}) {
    for(uint32_t m=2;m<=6;++m) {
      const auto interval=DisplayCapIntervalUs(hz*1000,m);
      CHECK(interval>0);
      CHECK(OutputMilliFps(interval)<hz*1000);           // presented below refresh
      CHECK(OutputMilliFps(interval)>hz*1000*95/100);    // and not far below it
      CHECK(!ExceedsDisplay(interval*m,hz*1000,m));      // game interval = presented * m
    }
  }
  CHECK(DisplayCapIntervalUs(60000,2)==17183);    // 58.2 FPS presented
  CHECK(DisplayCapIntervalUs(240000,3)==4296);    // Plague Tale at 240 Hz: 77.6 game FPS at 3x
  // Fractional refresh is kept (59.94 Hz).
  CHECK(DisplayCapIntervalUs(59940,2)>DisplayCapIntervalUs(60000,2));
  // Unknown refresh or multiplier: no cap.
  CHECK(DisplayCapIntervalUs(0,4)==0);
  CHECK(DisplayCapIntervalUs(5000,4)==0);
  CHECK(DisplayCapIntervalUs(165000,1)==0);
  CHECK(DisplayCapIntervalUs(165000,7)==0);
  CHECK(DisplayCapIntervalUs(165000,4,1000)==0);
  // The game's stricter limit (lower FPS, longer interval) wins; zero is no limit.
  CHECK(CombinedIntervalUs(33333,6249)==33333);
  CHECK(CombinedIntervalUs(5000,6249)==6249);
  CHECK(CombinedIntervalUs(0,6249)==6249);
  CHECK(CombinedIntervalUs(16666,0)==16666);
  CHECK(CombinedIntervalUs(0,0)==0);
  // Notice: 80 source FPS at 4x on 165 Hz exceeds it; 40 FPS does not.
  CHECK(ExceedsDisplay(12500,165000,4));
  CHECK(!ExceedsDisplay(25000,165000,4));
  CHECK(!ExceedsDisplay(0,165000,4));
  CHECK(!ExceedsDisplay(12500,0,4));
  std::cout << "xemfg_display_cap_tests passed\n";
  return 0;
}
