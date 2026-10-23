// SPDX-License-Identifier: MIT
#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
namespace mfgunlock::xemfg::native {
void ConfigureNativeContinuity(int mode) noexcept;
bool NativeContinuityEnabled() noexcept;
void RetainNativeOutput(Microsoft::WRL::ComPtr<IDXGISwapChain4>,ID3D12CommandQueue*);
void ClearRetainedNative(HWND window=nullptr);
void ConfirmNativeRetirement(HWND window,bool success);
HRESULT ReuseNativeOutput(IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,IDXGISwapChain1**);
}
