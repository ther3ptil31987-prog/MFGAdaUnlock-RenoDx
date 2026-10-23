#pragma once
#include <windows.h>
#include <string>
// Small host adapter for the MIT program port and pacing component. The
// original project's standalone renderer, HUD logic and DLSS backend are absent.
namespace xess_mod {
HMODULE Self() noexcept;
void WriteLog(const char* stage, const std::string& fields) noexcept;
}
