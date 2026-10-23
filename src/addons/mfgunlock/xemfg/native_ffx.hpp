// SPDX-License-Identifier: MIT
#pragma once
#include "native_internal.hpp"

// Plan 5.3: FidelityFX frame generation (ffx_api, FSR 3.1) input adapter.
// The game's FSR swapchain and frame-generation contexts are replaced by the
// Intel output; ffxDispatch(prepare) becomes packet inputs, and the latency
// markers and source wait that Streamline games send are synthesized around
// Present (user decision, 2026-10-22).
namespace mfgunlock::xemfg::native::ffx {
// Hooks the FidelityFX loader if it is mapped. Idempotent, any thread.
void Discover();
// True while this adapter's swapchain creation runs on the calling thread.
bool CreatingOutput() noexcept;
// The current output was created by this adapter and the adapter owns it.
bool Owns() noexcept;
}
