// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
namespace mfgunlock::xemfg::native {
std::string FileVersion(HMODULE module);
std::string FileVersion(const std::wstring& path);
// Once per process, from a worker thread: time, executable, OS, GPU/driver,
// addon build and relevant module names with file versions.
void LogSession() noexcept;
// Bounded inventory of presentation/Streamline-relevant modules at output creation.
void LogModules(uint64_t output) noexcept;
// slInit's sdkVersion plus interposer/plugin file versions, once per slInit.
void LogStreamlineVersion(uint64_t sdkVersion) noexcept;
// "major.minor.patch" of the last slInit, or empty before slInit was observed.
std::string StreamlineSdkVersion();
bool StreamlineSdkMagic() noexcept;
}
