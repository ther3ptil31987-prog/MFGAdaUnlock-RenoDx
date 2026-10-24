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
// Panel (1.5.1). The opt-in as read at startup; the adapter follows it until
// the next launch.
bool Configured() noexcept;
// The game runs DLSS-SR and nothing else XeMFG can use: not opted in, no
// Streamline, NGX mapped, no FidelityFX owner and no output yet. Never
// installs hooks; the panel also requires no game-owned XeFG context.
bool Suggested() noexcept;
// Persists [Adapters] DlssSr for the next launch. -1 nothing saved this
// session, otherwise the value saved.
bool Save(bool enabled);
int Saved() noexcept;
}
