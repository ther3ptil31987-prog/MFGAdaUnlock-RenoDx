// SPDX-License-Identifier: MIT
#pragma once
// 1.7 (plan F): the parts of MFG Unlocker's single_module the xvk layer and
// the Vulkan host use. The XeMFG build implements them on its own log and
// module (native_vulkan.cpp).
#include <Windows.h>

namespace single_module
{
void Log(const wchar_t* text) noexcept;
HMODULE Self() noexcept;
}
