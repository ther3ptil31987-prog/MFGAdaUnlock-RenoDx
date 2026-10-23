// An unadmitted external caller, deliberately not the executable or host.
#include <windows.h>
#include <xefg_swapchain_d3d12.h>
static volatile LONG calls{};
extern "C" __declspec(dllexport) __declspec(noinline)
xefg_swapchain_result_t CreateForeign(ID3D12Device* device,xefg_swapchain_handle_t* output) {
  const auto result=xefgSwapChainD3D12CreateContext(device,output);
  InterlockedIncrement(&calls); // Keep this DLL's return address at the SDK entry.
  return result;
}
