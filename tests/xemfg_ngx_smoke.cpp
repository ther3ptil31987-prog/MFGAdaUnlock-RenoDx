// SPDX-License-Identifier: MIT
// Plan 5.4: a DLSS-SR game through NGX, with no Streamline in the process and
// no frame generation of its own. With [Adapters] DlssSr=1 its swapchain must
// become the Intel output and each DLSS evaluation must feed one source frame.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <nvsdk_ngx.h>
#include "../src/addons/mfgunlock/xemfg/backend_api.hpp"
#include "../src/addons/mfgunlock/xemfg/native_bridge.hpp"
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT code,const char* where) { if(FAILED(code)) { std::cerr<<where<<" 0x"<<std::hex<<code<<std::endl; throw std::runtime_error(where); } }
void Require(bool value,const char* what) { if(!value) throw std::runtime_error(what); }
template<class T> T Import(HMODULE m,const char* name) { auto p=reinterpret_cast<T>(GetProcAddress(m,name)); Require(p!=nullptr,name); return p; }
void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after}; list->ResourceBarrier(1,&b);
}
LRESULT CALLBACK WindowProc(HWND w,UINT m,WPARAM a,LPARAM b) { return DefWindowProcW(w,m,a,b); }
// A game-side NGX parameter block (NGX defines only the interface).
struct Parameters final : NVSDK_NGX_Parameter {
  std::map<std::string,unsigned long long> integers; std::map<std::string,double> reals; std::map<std::string,void*> pointers;
  void Set(const char* n,unsigned long long v) override { integers[n]=v; }
  void Set(const char* n,float v) override { reals[n]=v; }
  void Set(const char* n,double v) override { reals[n]=v; }
  void Set(const char* n,unsigned int v) override { integers[n]=v; }
  void Set(const char* n,int v) override { integers[n]=static_cast<unsigned long long>(static_cast<long long>(v)); }
  void Set(const char* n,ID3D11Resource* v) override { pointers[n]=v; }
  void Set(const char* n,ID3D12Resource* v) override { pointers[n]=v; }
  void Set(const char* n,void* v) override { pointers[n]=v; }
  template<class T> NVSDK_NGX_Result Integer(const char* n,T* out) const {
    const auto it=integers.find(n); if(it==integers.end() || !out) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    *out=static_cast<T>(it->second); return NVSDK_NGX_Result_Success;
  }
  template<class T> NVSDK_NGX_Result Real(const char* n,T* out) const {
    const auto it=reals.find(n); if(it==reals.end() || !out) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    *out=static_cast<T>(it->second); return NVSDK_NGX_Result_Success;
  }
  template<class T> NVSDK_NGX_Result Pointer(const char* n,T** out) const {
    const auto it=pointers.find(n); if(it==pointers.end() || !out) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    *out=static_cast<T*>(it->second); return NVSDK_NGX_Result_Success;
  }
  NVSDK_NGX_Result Get(const char* n,unsigned long long* v) const override { return Integer(n,v); }
  NVSDK_NGX_Result Get(const char* n,float* v) const override { return Real(n,v); }
  NVSDK_NGX_Result Get(const char* n,double* v) const override { return Real(n,v); }
  NVSDK_NGX_Result Get(const char* n,unsigned int* v) const override { return Integer(n,v); }
  NVSDK_NGX_Result Get(const char* n,int* v) const override { return Integer(n,v); }
  NVSDK_NGX_Result Get(const char* n,ID3D11Resource** v) const override { return Pointer(n,v); }
  NVSDK_NGX_Result Get(const char* n,ID3D12Resource** v) const override { return Pointer(n,v); }
  NVSDK_NGX_Result Get(const char* n,void** v) const override { return Pointer(n,v); }
  void Reset() override { integers.clear(); reals.clear(); pointers.clear(); }
};
using CreateFn=NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
using EvaluateFn=NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using ReleaseFn=NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
}
int main(int argc,char** argv) {
  try {
    const unsigned multiplier=argc>1?unsigned(std::stoul(argv[1])):4;
    bool expectOff=false; for(int i=2;i<argc;++i) expectOff|=std::string(argv[i])=="--dlss-sr-default-off";
    auto reshade=LoadLibraryW(L".\\dxgi.dll"); Require(reshade,"ReShade");
    ComPtr<IDXGIFactory4> factory;
    Check(Import<HRESULT(WINAPI*)(REFIID,void**)>(reshade,"CreateDXGIFactory1")(IID_PPV_ARGS(&factory)),"factory");
    ComPtr<ID3D12Device> device;
    for(UINT i=0;!device;++i) {
      ComPtr<IDXGIAdapter1> adapter;
      if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND) break;
      DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
      if(desc.VendorId==0x10de) D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device));
    }
    Require(device!=nullptr,"NVIDIA device");
    auto addon=GetModuleHandleW(L"renodx-mfgunlock-native.addon64"); Require(addon,"ReShade did not load the addon");
    auto modelStatus=Import<mfgunlock::xemfg::GetStatus>(addon,"MFGAXeGetStatus");
    auto bridgeStatus=Import<bool(*)(mfgunlock::xemfg::native::Status*,uint32_t)>(addon,"MFGAXeNativeStatus");
    Require(!GetModuleHandleW(L"sl.interposer.dll"),"Streamline is present in the DLSS-SR scenario");
    // The NGX driver module maps late; the adapter hooks it from the loader worker.
    HMODULE ngx=LoadLibraryW(L"_nvngx.dll"); Require(ngx,"NGX fixture");
    const auto create=Import<CreateFn>(ngx,"NVSDK_NGX_D3D12_CreateFeature");
    const auto evaluate=Import<EvaluateFn>(ngx,"NVSDK_NGX_D3D12_EvaluateFeature");
    const auto release=Import<ReleaseFn>(ngx,"NVSDK_NGX_D3D12_ReleaseFeature");
    const auto evaluations=Import<uint32_t(*)()>(ngx,"FixtureNgxEvaluations");
    const auto* entry=reinterpret_cast<const uint8_t*>(evaluate);
    if(expectOff) {
      // Off by default: NGX stays untouched and the swapchain stays native.
      Sleep(1500);
      Require(entry[0]!=0xE9,"NGX was hooked without [Adapters] DlssSr=1");
      D3D12_COMMAND_QUEUE_DESC qd{}; ComPtr<ID3D12CommandQueue> queue; Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");
      WNDCLASSW wc{}; wc.lpfnWndProc=WindowProc; wc.lpszClassName=L"XeMFGNgxOff"; wc.hInstance=GetModuleHandleW(nullptr); RegisterClassW(&wc);
      HWND hwnd=CreateWindowW(wc.lpszClassName,L"XeMFG DLSS-SR off",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,wc.hInstance,nullptr);
      DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=640; desc.Height=360; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
      desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=3; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
      ComPtr<IDXGISwapChain1> chain; Check(factory->CreateSwapChainForHwnd(queue.Get(),hwnd,&desc,nullptr,nullptr,&chain),"native swapchain");
      mfgunlock::xemfg::native::Status status{}; bridgeStatus(&status,sizeof(status));
      Require(!status.output,"an Intel output was created without the DLSS-SR opt-in");
      chain.Reset(); DestroyWindow(hwnd);
      Require(Import<bool(*)(uint32_t)>(addon,"MFGAXeLogFlush")(2000),"log flush");
      std::cout<<"PASS: DLSS-SR adapter is off by default"<<std::endl;
      return 0;
    }
    for(const auto deadline=GetTickCount64()+5000;entry[0]!=0xE9 && GetTickCount64()<deadline;) Sleep(10);
    Require(entry[0]==0xE9,"NGX evaluation was not hooked");
    D3D12_COMMAND_QUEUE_DESC qd{}; ComPtr<ID3D12CommandQueue> queue; Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");
    WNDCLASSW wc{}; wc.lpfnWndProc=WindowProc; wc.lpszClassName=L"XeMFGNgxSmoke"; wc.hInstance=GetModuleHandleW(nullptr); RegisterClassW(&wc);
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"XeMFG DLSS-SR adapter smoke",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,wc.hInstance,nullptr);
    Require(hwnd,"window");
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=640; desc.Height=360; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=3; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC windowed{}; windowed.Windowed=TRUE;
    ComPtr<IDXGISwapChain1> base; Check(factory->CreateSwapChainForHwnd(queue.Get(),hwnd,&desc,&windowed,nullptr,&base),"swapchain");
    ComPtr<IDXGISwapChain4> chain; Check(base.As(&chain),"chain4"); base.Reset();
    mfgunlock::xemfg::native::Status ready{}; bridgeStatus(&ready,sizeof(ready));
    Require(ready.output,"armed DLSS-SR swapchain is not the Intel output");
    ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list; ComPtr<ID3D12Fence> fence; uint64_t value{};
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    const auto submit=[&] {
      Check(list->Close(),"close"); ID3D12CommandList* raw=list.Get(); queue->ExecuteCommandLists(1,&raw);
      Check(queue->Signal(fence.Get(),++value),"signal"); while(fence->GetCompletedValue()<value) Sleep(0);
    };
    // Depth (R32) and motion (R16G16) at render resolution, as DLSS-SR inputs.
    ComPtr<ID3D12Resource> depth,motion;
    for(auto item:{std::pair{std::addressof(depth),DXGI_FORMAT_R32_FLOAT},std::pair{std::addressof(motion),DXGI_FORMAT_R16G16_FLOAT}}) {
      D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT}; D3D12_RESOURCE_DESC rd{};
      rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width=desc.Width; rd.Height=desc.Height; rd.DepthOrArraySize=1; rd.MipLevels=1;
      rd.Format=item.second; rd.SampleDesc.Count=1; rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,
          IID_PPV_ARGS(item.first->ReleaseAndGetAddressOf())),"input texture");
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> rtv; Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)),"rtv heap");
    Parameters params;
    params.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_DepthInverted|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes));
    params.Set(NVSDK_NGX_Parameter_Width,desc.Width); params.Set(NVSDK_NGX_Parameter_Height,desc.Height);
    NVSDK_NGX_Handle* feature{};
    Require(NVSDK_NGX_SUCCEED(create(list.Get(),NVSDK_NGX_Feature_SuperSampling,&params,&feature)) && feature,"DLSS-SR feature");
    submit();
    unsigned readyFrames{};
    for(unsigned frame=0;frame<24;++frame) {
      Check(allocator->Reset(),"allocator reset"); Check(list->Reset(allocator.Get(),nullptr),"list reset");
      ComPtr<ID3D12Resource> back; Check(chain->GetBuffer(chain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back)),"back buffer");
      auto handle=rtv->GetCPUDescriptorHandleForHeapStart(); device->CreateRenderTargetView(back.Get(),nullptr,handle);
      Barrier(list.Get(),back.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
      const float color[]={.1f,.2f,.3f,1.f}; list->ClearRenderTargetView(handle,color,0,nullptr);
      Barrier(list.Get(),back.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
      params.Set(NVSDK_NGX_Parameter_Depth,depth.Get()); params.Set(NVSDK_NGX_Parameter_MotionVectors,motion.Get());
      params.Set(NVSDK_NGX_Parameter_Jitter_Offset_X,frame%2?.25f:-.25f); params.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,frame%2?-.25f:.25f);
      params.Set(NVSDK_NGX_Parameter_MV_Scale_X,1.f); params.Set(NVSDK_NGX_Parameter_MV_Scale_Y,1.f);
      params.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,desc.Width);
      params.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,desc.Height);
      params.Set(NVSDK_NGX_Parameter_Reset,int(frame==0));
      Require(NVSDK_NGX_SUCCEED(evaluate(list.Get(),feature,&params,nullptr)),"DLSS-SR evaluation");
      submit(); back.Reset();
      Check(chain->Present(0,0),"Present");
      bridgeStatus(&ready,sizeof(ready)); if(ready.enabled) ++readyFrames;
      MSG msg{}; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&msg);
    }
    mfgunlock::xemfg::Status ms; modelStatus(&ms,sizeof(ms));
    std::cout<<"DLSS-SR ready frames="<<readyFrames<<" evaluations="<<evaluations()<<" presented="<<ms.presented
        <<" generated="<<ms.generated<<" result="<<ms.result<<" detail="<<ready.detail<<std::endl;
    Require(evaluations()==24,"DLSS-SR itself did not run every frame");
    Require(readyFrames>=16,"DLSS-SR frames did not reach ready");
    Require(ms.generated==multiplier-1 && ms.presented==multiplier && ms.result==0,"DLSS-SR output presented no generated frames");
    Require(NVSDK_NGX_SUCCEED(release(feature)),"DLSS-SR release");
    chain.Reset(); DestroyWindow(hwnd);
    Require(Import<bool(*)(uint32_t)>(addon,"MFGAXeLogFlush")(2000),"log flush");
    std::cout<<"PASS: DLSS-SR adapter reached ready"<<std::endl;
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<std::endl; return 1; }
}
