// SPDX-License-Identifier: MIT
// 1.8.7: offline check of the vendored nefh provider preparation (RTX 20/30).
// Each argument is a DLSS-G provider (nvngx_dlssg.dll or an NGX model .bin).
// The file is mapped as an image in this process and prepared for the given
// architecture; nothing on disk changes. Exit 77 (skipped) with no inputs.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/addons/mfgunlock/ampere/provider.hpp"

extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int level, const char* message) {
  std::printf("  [log %d] %s\n", level, message ? message : "");
}

namespace {
uint32_t MinimumArchitecture(HMODULE module) {
  const auto* entry = reinterpret_cast<const unsigned char*>(GetProcAddress(module, "NVSDK_NGX_GetGPUArchitecture"));
  if (entry == nullptr || entry[0] != 0xB8) return 0;
  uint32_t value = 0;
  std::memcpy(&value, entry + 1, sizeof(value));
  return value;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  namespace a = mfgunlock::ampere;
  // Default Ampere; "--turing" as the first argument selects the sm_75 path.
  int first = 1;
  auto architecture = a::Architecture::kAmpere;
  if (argc > 1 && std::wcscmp(argv[1], L"--turing") == 0) {
    architecture = a::Architecture::kTuring;
    first = 2;
  }
  if (argc <= first) {
    std::puts("no provider inputs; skipped");
    return 77;
  }
  a::architecture::Configure(architecture);
  int failures = 0;
  for (int i = first; i < argc; ++i) {
    std::wprintf(L"== %ls\n", argv[i]);
    HMODULE module = LoadLibraryExW(argv[i], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (module == nullptr) {
      std::printf("  cannot map (error %lu)\n", GetLastError());
      ++failures;
      continue;
    }
    const uint32_t before = MinimumArchitecture(module);
    const bool prepared = a::provider::PrepareProvider(module);
    const uint32_t after = MinimumArchitecture(module);
    std::printf("  prepared=%d minimum 0x%x -> 0x%x\n", prepared ? 1 : 0, before, after);
    if (!prepared) ++failures;
  }
  std::printf("%d of %d provider(s) not prepared\n", failures, argc - first);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
