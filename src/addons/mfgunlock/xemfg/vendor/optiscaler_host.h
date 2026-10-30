// SPDX-License-Identifier: MIT
#pragma once
// 1.7 (plan J): the only part of MFG Unlocker's optiscaler_host the pacing
// component uses. This build never runs under OptiScaler, so no OptiScaler
// menu draws on XeFG's native swap chain: its buffer cap is the flip queue's
// own maximum.
#include <cstdint>

namespace optiscaler_host
{
constexpr uint32_t kMenuBackBuffers = 16;
inline uint32_t MenuBackBuffers() noexcept { return kMenuBackBuffers; }
}
