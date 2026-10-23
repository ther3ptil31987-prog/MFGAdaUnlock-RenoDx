// SPDX-License-Identifier: MIT
// Plan 5.3: stands in for amd_fidelityfx_loader_dx12.dll. Every entry counts
// and fails, so a call the adapter should have replaced (swapchain, frame
// generation) is visible to the test instead of silently working.
#include "../src/addons/mfgunlock/xemfg/vendor/ffx_api/ffx_api.h"
#include <atomic>
namespace { std::atomic<uint32_t> originalCalls{}; std::atomic<uint64_t> lastType{}; }
extern "C" {
FFX_API_ENTRY ffxReturnCode_t ffxCreateContext(ffxContext* context,ffxCreateContextDescHeader* desc,const ffxAllocationCallbacks*) {
  ++originalCalls; lastType=desc?desc->type:0; if(context) *context=nullptr; return FFX_API_RETURN_ERROR;
}
FFX_API_ENTRY ffxReturnCode_t ffxDestroyContext(ffxContext* context,const ffxAllocationCallbacks*) {
  ++originalCalls; lastType=0; if(context) *context=nullptr; return FFX_API_RETURN_ERROR;
}
FFX_API_ENTRY ffxReturnCode_t ffxConfigure(ffxContext*,const ffxConfigureDescHeader* desc) {
  ++originalCalls; lastType=desc?desc->type:0; return FFX_API_RETURN_ERROR;
}
FFX_API_ENTRY ffxReturnCode_t ffxQuery(ffxContext*,ffxQueryDescHeader* desc) {
  ++originalCalls; lastType=desc?desc->type:0; return FFX_API_RETURN_ERROR;
}
FFX_API_ENTRY ffxReturnCode_t ffxDispatch(ffxContext*,const ffxDispatchDescHeader* desc) {
  ++originalCalls; lastType=desc?desc->type:0; return FFX_API_RETURN_ERROR;
}
__declspec(dllexport) uint32_t FixtureFfxOriginalCalls() { return originalCalls; }
__declspec(dllexport) uint64_t FixtureFfxLastType() { return lastType; }
}
