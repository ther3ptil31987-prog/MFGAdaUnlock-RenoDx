// SPDX-License-Identifier: MIT
// 1.7 (plan J): the pacing component (vendor/sdk_pacing.cpp, dashdogy v1.7.0)
// tells the xvk Vulkan swap chain which XeFG frame it presents, for Reflex
// markers on Vulkan. Builds without the Vulkan host (plan F) have nothing to
// tell; the real definitions are in vendor/xvk/xvk_dxgi.cpp.
#ifndef MFGUNLOCK_XEMFG_VULKAN
#include "vendor/xvk/xvk_output_frame.h"

namespace xvk {
void SetOutputFrame(uint64_t, bool) noexcept {}
void ClearOutputFrame() noexcept {}
}
#endif
