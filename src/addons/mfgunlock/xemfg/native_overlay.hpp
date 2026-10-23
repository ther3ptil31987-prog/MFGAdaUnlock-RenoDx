// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <dxgi1_6.h>
#include <cstdint>
namespace mfgunlock::xemfg::native {
struct OverlayStatus { uint64_t created{},destroyed{},updates{},failed{}; };
OverlayStatus OverlaySnapshot() noexcept;
struct OverlayInputStatus { uint64_t requests{},recovered{},dropped{}; };
OverlayInputStatus OverlayInputSnapshot() noexcept;
// Only the internal Intel output, only if it is still a system DXGI object.
// Automatically wrapped ReShade outputs are left to ReShade's normal owner.
void WrapFinalOutput(IUnknown* queue,HWND hwnd,IDXGISwapChain1** chain) noexcept;
}
