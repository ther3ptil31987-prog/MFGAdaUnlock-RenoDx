// SPDX-License-Identifier: MIT
#pragma once
#include "native_internal.hpp"

// Plan 5.4: experimental DLSS-SR input adapter, off unless [Adapters]
// DlssSr=1. NGX evaluation of the SuperSampling feature supplies depth,
// motion and jitter; there is no camera, so the projection is assumed and the
// HUD is interpolated with the scene. Frames are synthesized (native_synthetic).
namespace mfgunlock::xemfg::native::ngx {
// Hooks the NGX driver module if it is mapped. Idempotent, any thread.
void Discover();
// Opted in and no Streamline in the process: every swapchain becomes an Intel
// output that this adapter feeds once the game evaluates DLSS-SR.
bool Armed() noexcept;
// Called when an output was created while armed.
void OutputCreated(const std::shared_ptr<Output>& o);
}
