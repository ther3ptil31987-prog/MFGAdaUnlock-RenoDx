// SPDX-License-Identifier: MIT
// Own public COM adapter: independent of a particular factory detour entry.
#include "native_factory.hpp"
#include "backend_api.hpp"
#include "vendor/xess_mod.h"
#include <atomic>
#include <string>
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
    // 1.7 (plan J): the flip queue holds as many outputs as XeFG's native swap
    // chain has back buffers, and XeFG creates it with the game's count. As
    // upstream's NativeBuffersScope does, ask for the queue's count and fall
    // back to the game's descriptor if that creation fails. XeFG's proxy keeps
    // reporting the game's count, and a later resize returns to it.
    const uint32_t raise=FlipQueueNativeBuffers();
    if(desc && raise>desc->BufferCount) {
      auto raised=*desc; raised.BufferCount=raise;
      const HRESULT hr=CreateFinalNativeOutput(original.Get(),queue,window,&raised,full,restrictOutput,out);
      static std::atomic<unsigned> logs{};
      if(logs.fetch_add(1)<4) xess_mod::WriteLog("native_buffers","\"game\":"+std::to_string(desc->BufferCount)
          +",\"requested\":"+std::to_string(raise)+",\"result\":"+std::to_string(hr));
      if(SUCCEEDED(hr)) return hr;
    }
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
