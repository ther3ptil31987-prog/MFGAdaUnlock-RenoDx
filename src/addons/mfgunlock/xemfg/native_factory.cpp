// SPDX-License-Identifier: MIT
// Own public COM adapter: independent of a particular factory detour entry.
#include "native_factory.hpp"
#include <atomic>
namespace mfgunlock::xemfg::native {
using Microsoft::WRL::ComPtr;
namespace {
class OutputFactory final:public IDXGIFactory2 {
  std::atomic<ULONG> references{1};
  ComPtr<IDXGIFactory2> original;
public:
  explicit OutputFactory(IDXGIFactory2* value):original(value) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
    if(!out)return E_POINTER;*out=nullptr;
    if(id==__uuidof(IUnknown)||id==__uuidof(IDXGIObject)||id==__uuidof(IDXGIFactory)
        ||id==__uuidof(IDXGIFactory1)||id==__uuidof(IDXGIFactory2)) {
      *out=static_cast<IDXGIFactory2*>(this);AddRef();return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
  ULONG STDMETHODCALLTYPE Release() override {const auto n=--references;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* queue,HWND window,const DXGI_SWAP_CHAIN_DESC1* desc,
      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full,IDXGIOutput* restrictOutput,IDXGISwapChain1** out) override {
    return CreateFinalNativeOutput(original.Get(),queue,window,desc,full,restrictOutput,out);
  }
#define HR(name,args,call) HRESULT STDMETHODCALLTYPE name args override {return original->name call;}
  HR(SetPrivateData,(REFGUID a,UINT b,const void* c),(a,b,c))
  HR(SetPrivateDataInterface,(REFGUID a,const IUnknown* b),(a,b))
  HR(GetPrivateData,(REFGUID a,UINT* b,void* c),(a,b,c))
  HR(GetParent,(REFIID a,void** b),(a,b))
  HR(EnumAdapters,(UINT a,IDXGIAdapter** b),(a,b))
  HR(MakeWindowAssociation,(HWND a,UINT b),(a,b))
  HR(GetWindowAssociation,(HWND* a),(a))
  HR(CreateSwapChain,(IUnknown* a,DXGI_SWAP_CHAIN_DESC* b,IDXGISwapChain** c),(a,b,c))
  HR(CreateSoftwareAdapter,(HMODULE a,IDXGIAdapter** b),(a,b))
  HR(EnumAdapters1,(UINT a,IDXGIAdapter1** b),(a,b))
  HR(CreateSwapChainForCoreWindow,(IUnknown* a,IUnknown* b,const DXGI_SWAP_CHAIN_DESC1* c,IDXGIOutput* d,IDXGISwapChain1** e),(a,b,c,d,e))
  HR(GetSharedResourceAdapterLuid,(HANDLE a,LUID* b),(a,b))
  HR(RegisterStereoStatusWindow,(HWND a,UINT b,DWORD* c),(a,b,c))
  HR(RegisterStereoStatusEvent,(HANDLE a,DWORD* b),(a,b))
  HR(RegisterOcclusionStatusWindow,(HWND a,UINT b,DWORD* c),(a,b,c))
  HR(RegisterOcclusionStatusEvent,(HANDLE a,DWORD* b),(a,b))
  HR(CreateSwapChainForComposition,(IUnknown* a,const DXGI_SWAP_CHAIN_DESC1* b,IDXGIOutput* c,IDXGISwapChain1** d),(a,b,c,d))
#undef HR
  BOOL STDMETHODCALLTYPE IsCurrent() override {return original->IsCurrent();}
  BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override {return original->IsWindowedStereoEnabled();}
  void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD value) override {original->UnregisterStereoStatus(value);}
  void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD value) override {original->UnregisterOcclusionStatus(value);}
};
}
ComPtr<IDXGIFactory2> FinalOutputFactory(IDXGIFactory2* factory) {
  ComPtr<IDXGIFactory2> result;result.Attach(new OutputFactory(factory));return result;
}
}
