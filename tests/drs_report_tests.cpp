// SPDX-License-Identifier: MIT
// 1.9.0: the read-only driver-profile report. Without NVAPI (no NVIDIA driver)
// it must say so and exit 77 (skipped); with it, both profiles are listed and
// every key has a line. Nothing is written to the driver profile.
#include <windows.h>

#include <cstdio>
#include <string>

#include "../src/addons/mfgunlock/drs_report.hpp"

extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int level, const char* message) {
  std::printf("  [log %d] %s\n", level, message ? message : "");
}

int main() {
  const std::string report = mfgunlock::drsreport::Describe();
  std::printf("%s", report.c_str());
  if (report.find("NVAPI not available") != std::string::npos) return 77;
  if (report.rfind("driver_profile (read only):\n", 0) != 0) return 1;
  if (report.find("global (base) profile:") == std::string::npos) return 2;
  for (const auto& key : mfgunlock::drsreport::kKeys) {
    char id[16];
    std::snprintf(id, sizeof(id), "0x%08X", key.id);
    if (report.find(id) == std::string::npos) return 3;
  }
  std::printf("drs_report tests passed\n");
  return 0;
}
