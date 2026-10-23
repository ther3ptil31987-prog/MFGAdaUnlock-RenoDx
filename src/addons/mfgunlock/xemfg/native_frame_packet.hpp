// SPDX-License-Identifier: MIT
#pragma once
#include "native_internal.hpp"

namespace mfgunlock::xemfg::native::packet {
// Plan 5.1: adapter-neutral frame inputs. An input adapter (Streamline DLSS-G
// today) translates its own API into these and submits them; validation,
// tagging, staging and admission are the same for every adapter.
enum class Lifecycle { UntilPresent, OnlyNow, Invalid };
struct Resource {
  xefg_swapchain_resource_type_t type{};
  IUnknown* native{};      // nullptr removes this input from the frame
  bool headerValid{};      // the adapter's own ABI/type checks passed
  uint32_t state{UINT_MAX}; // D3D12_RESOURCE_STATES; UINT_MAX is unknown
  DejitterRegion extent{}; // zero width/height: the whole resource
  Lifecycle lifecycle{Lifecycle::Invalid};
  IUnknown* producer{};    // producer command list for volatile inputs
  // Diagnostics only: the adapter's raw lifecycle and structure versions.
  uint32_t rawLifecycle{}, tagVersion{}, resourceVersion{};
};
// Both are called with Output::io held and a frame of the current epoch.
void SubmitResource(Output& o,Frame& frame,uint32_t id,const Resource& input);
void SubmitBackbufferRegion(Output& o,Frame& frame,uint32_t id,const DejitterRegion& extent);
// Normalized constants and the conventions the game stated (if it did).
// Returns the conventions to learn for the next output (plan 3.3).
struct Conventions { std::optional<bool> depthInverted, motionJittered; };
Conventions SubmitConstants(Frame& frame,uint32_t issues,const xefg_swapchain_frame_constant_data_t& data,
    const Conventions& stated);
}
