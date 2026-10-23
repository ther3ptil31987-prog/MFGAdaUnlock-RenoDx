// SPDX-License-Identifier: MIT
#pragma once
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl/client.h>
namespace mfgunlock::xemfg::native {
Microsoft::WRL::ComPtr<IDXGIFactory2> FinalOutputFactory(IDXGIFactory2*);
HRESULT CreateFinalNativeOutput(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
}
