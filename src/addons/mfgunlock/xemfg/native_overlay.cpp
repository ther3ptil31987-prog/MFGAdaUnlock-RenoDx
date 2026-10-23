// SPDX-License-Identifier: MIT
// ReShade's public manual-runtime API for Intel's unproxied presentation queue.
// Effects and input run at the final output; never draw into an FG input.
#include "native_internal.hpp"
#include "native_overlay.hpp"
#include "native_continuity.hpp"
#include "../backend_selection.hpp"
#include <deps/imgui/imgui.h>
#include <reshade.hpp>
namespace mfgunlock::xemfg::native {
namespace {
std::atomic<uint64_t> created{},destroyed{},updates{},failed{};
std::atomic<uint64_t> keyRequests{};
class FinalOutput final:public IDXGISwapChain4 {
  std::atomic<ULONG> refs{1};
  ComPtr<IDXGISwapChain4> chain;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  reshade::api::effect_runtime* runtime{};
  std::recursive_mutex lock;
  DXGI_COLOR_SPACE_TYPE color{DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709};
  bool colorKnown{};
  unsigned requestLogs{};
  uint64_t diagnosticOutput{};
  std::unique_ptr<QueueTrace> gpuTrace;
  static thread_local FinalOutput* rendering;
  void Color() {
    if(!runtime || !colorKnown) return;
    using Space=reshade::api::color_space;
    if(color==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) runtime->set_color_space(Space::hdr10_st2084);
    else if(color==DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) runtime->set_color_space(Space::extended_srgb_linear);
    else if(color==DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) runtime->set_color_space(Space::srgb_nonlinear);
  }
  void Create() {
    const auto config=(backend::Directory()/L"ReShade.ini").u8string();
    if(!reshade::create_effect_runtime(reshade::api::device_api::d3d12,device.Get(),queue.Get(),chain.Get(),
        reinterpret_cast<const char*>(config.c_str()),&runtime)) { ++failed; return; }
    ++created; Color();
  }
  void Destroy() {
    if(!runtime) return;
    reshade::destroy_effect_runtime(runtime); runtime=nullptr; ++destroyed;
  }
  void Render(UINT flags) {
    if(TraceEnabled() && !diagnosticOutput) if(auto owner=Active())diagnosticOutput=owner->diagnosticOutput;
    if(GpuTraceEnabled() && !gpuTrace)gpuTrace=std::make_unique<QueueTrace>(device.Get(),queue.Get(),diagnosticOutput,"presentation");
    if(gpuTrace && !(flags&DXGI_PRESENT_TEST))gpuTrace->Begin(State().presenting.load());
    RuntimeCall trace("reshade_final_update",diagnosticOutput);
    if(runtime && !(flags&DXGI_PRESENT_TEST)) {
      struct Scope { FinalOutput* previous; Scope(FinalOutput* value):previous(rendering){rendering=value;} ~Scope(){rendering=previous;} } scope(this);
      // ReShade owns its shortcut, active widgets and input capture. Updating
      // the final runtime must not reserve Home, replay keys or veto toggles.
      reshade::update_and_present_effect_runtime(runtime); ++updates;
    }
  }
public:
  static bool OverlayChange(reshade::api::effect_runtime* value,bool open,reshade::api::input_source source) {
    auto* self=rendering;
    if(!self || self->runtime!=value) return false;
    ++keyRequests;
    if(self->requestLogs++<32) xess_mod::WriteLog("native_overlay_request","\"open\":"+std::string(open?"true":"false")
        +",\"source\":"+std::to_string(uint32_t(source))+",\"owner\":\"ReShade\"");
    return false;
  }
  FinalOutput(ComPtr<IDXGISwapChain4> value,ComPtr<ID3D12Device> d,ComPtr<ID3D12CommandQueue> q)
      :chain(std::move(value)),device(std::move(d)),queue(std::move(q)) { Create(); }
  ~FinalOutput() {
    Destroy();
    ComPtr<IDXGISwapChain4> retained;
    if(NativeContinuityEnabled()) retained=chain;
    auto* native=chain.Detach();
    const auto remaining=native?native->Release():0;
    const auto externalRemaining=retained && remaining?remaining-1:remaining;
    if(retained && remaining>1) RetainNativeOutput(std::move(retained),queue.Get());
    xess_mod::WriteLog("native_final_output_retired","\"native\":"+std::to_string(uintptr_t(native))
        +",\"remaining_refs\":"+std::to_string(externalRemaining));
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
    if(!out) return E_POINTER; *out=nullptr;
    if(id==__uuidof(IUnknown)||id==__uuidof(IDXGIObject)||id==__uuidof(IDXGIDeviceSubObject)
        ||id==__uuidof(IDXGISwapChain)||id==__uuidof(IDXGISwapChain1)||id==__uuidof(IDXGISwapChain2)
        ||id==__uuidof(IDXGISwapChain3)||id==__uuidof(IDXGISwapChain4)) {
      *out=static_cast<IDXGISwapChain4*>(this); AddRef(); return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
  HRESULT STDMETHODCALLTYPE Present(UINT sync,UINT flags) override {
    { std::lock_guard guard(lock); Render(flags); }
    // DXGI may wait on the window thread. The SDK owns external Present/Resize
    // ordering; do not hold our runtime lock across the native call.
    RuntimeCall trace("native_present",diagnosticOutput);const auto r=chain->Present(sync,flags);trace.Result(r);
    {std::lock_guard guard(lock);if(gpuTrace)gpuTrace->End();}return r;
  }
  HRESULT STDMETHODCALLTYPE Present1(UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* parameters) override {
    { std::lock_guard guard(lock); Render(flags); }
    RuntimeCall trace("native_present1",diagnosticOutput);const auto r=chain->Present1(sync,flags,parameters);trace.Result(r);
    {std::lock_guard guard(lock);if(gpuTrace)gpuTrace->End();}return r;
  }
  HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT a,UINT b,UINT c,DXGI_FORMAT d,UINT e) override {
    { std::lock_guard guard(lock); Destroy(); }
    const auto result=chain->ResizeBuffers(a,b,c,d,e);
    { std::lock_guard guard(lock); Create(); }
    return result;
  }
  HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT a,UINT b,UINT c,DXGI_FORMAT d,UINT e,const UINT* f,IUnknown*const* g) override {
    { std::lock_guard guard(lock); Destroy(); }
    const auto result=chain->ResizeBuffers1(a,b,c,d,e,f,g);
    { std::lock_guard guard(lock); Create(); }
    return result;
  }
  HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE value) override {
    const auto result=chain->SetColorSpace1(value);
    if(SUCCEEDED(result)) { std::lock_guard guard(lock); color=value; colorKnown=true; Color(); } return result;
  }
#define HR(name,args,call) HRESULT STDMETHODCALLTYPE name args override { return chain->name call; }
  HR(SetPrivateData,(REFGUID a,UINT b,const void* c),(a,b,c))
  HR(SetPrivateDataInterface,(REFGUID a,const IUnknown* b),(a,b))
  HR(GetPrivateData,(REFGUID a,UINT* b,void* c),(a,b,c))
  HR(GetParent,(REFIID a,void** b),(a,b)) HR(GetDevice,(REFIID a,void** b),(a,b))
  HR(GetBuffer,(UINT a,REFIID b,void** c),(a,b,c))
  HR(SetFullscreenState,(BOOL a,IDXGIOutput* b),(a,b)) HR(GetFullscreenState,(BOOL* a,IDXGIOutput** b),(a,b))
  HR(GetDesc,(DXGI_SWAP_CHAIN_DESC* a),(a)) HR(ResizeTarget,(const DXGI_MODE_DESC* a),(a))
  HR(GetContainingOutput,(IDXGIOutput** a),(a)) HR(GetFrameStatistics,(DXGI_FRAME_STATISTICS* a),(a))
  HR(GetLastPresentCount,(UINT* a),(a)) HR(GetDesc1,(DXGI_SWAP_CHAIN_DESC1* a),(a))
  HR(GetFullscreenDesc,(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a),(a)) HR(GetHwnd,(HWND* a),(a))
  HR(GetCoreWindow,(REFIID a,void** b),(a,b))
  HR(GetRestrictToOutput,(IDXGIOutput** a),(a)) HR(SetBackgroundColor,(const DXGI_RGBA* a),(a))
  HR(GetBackgroundColor,(DXGI_RGBA* a),(a)) HR(SetRotation,(DXGI_MODE_ROTATION a),(a))
  HR(GetRotation,(DXGI_MODE_ROTATION* a),(a)) HR(SetSourceSize,(UINT a,UINT b),(a,b))
  HR(GetSourceSize,(UINT* a,UINT* b),(a,b)) HR(SetMaximumFrameLatency,(UINT a),(a))
  HR(GetMaximumFrameLatency,(UINT* a),(a)) HR(SetMatrixTransform,(const DXGI_MATRIX_3X2_F* a),(a))
  HR(GetMatrixTransform,(DXGI_MATRIX_3X2_F* a),(a)) HR(CheckColorSpaceSupport,(DXGI_COLOR_SPACE_TYPE a,UINT* b),(a,b))
  HR(SetHDRMetaData,(DXGI_HDR_METADATA_TYPE a,UINT b,void* c),(a,b,c))
#undef HR
  BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return chain->IsTemporaryMonoSupported(); }
  HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return chain->GetFrameLatencyWaitableObject(); }
  UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return chain->GetCurrentBackBufferIndex(); }
};
thread_local FinalOutput* FinalOutput::rendering{};
}
OverlayStatus OverlaySnapshot() noexcept { return {created,destroyed,updates,failed}; }
OverlayInputStatus OverlayInputSnapshot() noexcept { return {keyRequests,0,0}; }
void WrapFinalOutput(IUnknown* source,HWND hwnd,IDXGISwapChain1** output) noexcept {
  try {
    if(!output || !*output || !single_overlay::native::SystemDxgiObject(*output,40)) return;
    HWND actual{}; if(FAILED((*output)->GetHwnd(&actual)) || actual!=hwnd) return;
    ComPtr<ID3D12CommandQueue> queue; ComPtr<ID3D12Device> device; ComPtr<IDXGISwapChain4> chain;
    if(!source || FAILED(source->QueryInterface(IID_PPV_ARGS(&queue)))
        || queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT
        || FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || FAILED((*output)->QueryInterface(IID_PPV_ARGS(&chain)))) return;
    static std::once_flag callbacks;
    std::call_once(callbacks,[] {
      reshade::register_event<reshade::addon_event::reshade_open_overlay>(&FinalOutput::OverlayChange);
    });
    auto* replacement=new FinalOutput(std::move(chain),std::move(device),std::move(queue));
    (*output)->Release(); *output=replacement;
    xess_mod::WriteLog("native_final_overlay","\"manual_runtime\":true");
  } catch(...) { ++failed; }
}
}
extern "C" __declspec(dllexport) bool MFGAXeOverlayStatus(mfgunlock::xemfg::native::OverlayStatus* out,uint32_t bytes) {
  if(!out || bytes!=sizeof(*out)) return false; *out=mfgunlock::xemfg::native::OverlaySnapshot(); return true;
}
extern "C" __declspec(dllexport) bool MFGAXeOverlayInputStatus(mfgunlock::xemfg::native::OverlayInputStatus* out,uint32_t bytes) {
  if(!out || bytes!=sizeof(*out)) return false; *out=mfgunlock::xemfg::native::OverlayInputSnapshot(); return true;
}
