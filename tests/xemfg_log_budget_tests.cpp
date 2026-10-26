// SPDX-License-Identifier: MIT
#include <cstdlib>
#include <iostream>
#include <string>

#include "../src/addons/mfgunlock/xemfg/log_budget.hpp"

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition     \
                << '\n';                                                        \
      std::abort();                                                              \
    }                                                                           \
  } while (false)

using mfgunlock::xemfg::native::AdmitSdkMessage;
using mfgunlock::xemfg::native::PatternBudget;

int main() {
  // A load screen repeats one message with a new frame number every frame.
  PatternBudget budget(48);
  uint64_t admitted = 0;
  for (int frame = 1; frame <= 200; ++frame) {
    const std::string message = "Frame " + std::to_string(frame) + " skipped: inputs missing";
    if (AdmitSdkMessage(budget, message.c_str())) ++admitted;
  }
  CHECK(admitted == 4);  // occurrences 1, 2, 3 and 16
  // The first different message after that still reaches the log (the old
  // fixed budget of 100 messages had been spent by the repetitions).
  CHECK(AdmitSdkMessage(budget, "hudless and backbuffer resource resolutions must match") == 1);
  // Later repetitions of the first pattern stay grouped.
  for (int frame = 201; frame <= 255; ++frame) {
    const std::string message = "Frame " + std::to_string(frame) + " skipped: inputs missing";
    CHECK(AdmitSdkMessage(budget, message.c_str()) == 0);
  }
  CHECK(AdmitSdkMessage(budget, "Frame 256 skipped: inputs missing") == 256);
  CHECK(AdmitSdkMessage(budget, nullptr) == 1);  // a null message is one empty pattern
  // Distinct patterns beyond the limit are dropped instead of growing the table.
  PatternBudget small(2);
  CHECK(AdmitSdkMessage(small, "a") == 1);
  CHECK(AdmitSdkMessage(small, "b") == 1);
  CHECK(AdmitSdkMessage(small, "c") == 0);
  std::cout << "xemfg_log_budget_tests passed\n";
  return 0;
}
