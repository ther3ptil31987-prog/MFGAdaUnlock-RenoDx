// SPDX-License-Identifier: MIT
#include "native_ffx.hpp"
#include "native_frame_packet.hpp"
#include "native_constants.hpp"
#include "native_synthetic.hpp"
#include "vendor/ffx_api/ffx_framegeneration.h"
#include "vendor/ffx_api/dx12/ffx_api_dx12.h"
#include "../backend_selection.hpp"
#include <DirectXMath.h>
#include <map>

namespace mfgunlock::xemfg::native::ffx {
namespace {
PfnFfxCreateContext createContext{};
PfnFfxDestroyContext destroyContext{};
PfnFfxConfigure configure{};
PfnFfxQuery query{};
PfnFfxDispatch dispatch{};
// Stand-in contexts handed to the game. They are never passed to the
// FidelityFX runtime: the Intel output replaces its swapchain and generator.
struct StandIn { const char* kind; };
StandIn swapchainContext{"swapchain"},generationContext{"frame_generation"};
// Lock order: this lock, then the synthetic-frame lock, then Output::io.
std::recursive_mutex lock;
const Output* ownedOutput{};
thread_local bool creating{};
uint32_t generationFlags{};
FfxApiResource hudless{};
// The game's own FSR frame-generation switch; applied only while the
// synthesized frames are active, so an unused FSR context cannot override the
// Streamline route's request (1.5.1).
bool ffxRequested{};
// The game's frameID only signals history resets; frame ids are synthesized.
uint64_t lastFfxFrame{UINT64_MAX};
uint32_t prepareLogBudget{};

bool StandInContext(const ffxContext* context) {
  return context && (*context==&swapchainContext || *context==&generationContext);
}
bool Enabled() {
  const auto ini=backend::Directory()/L"MFGAdaUnlock.ini";
  return GetPrivateProfileIntW(L"Adapters",L"FidelityFX",1,ini.c_str())!=0;
}
D3D12_RESOURCE_STATES D3dState(uint32_t state) {
  uint32_t out{};
  if(state&FFX_API_RESOURCE_STATE_UNORDERED_ACCESS) out|=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  if(state&FFX_API_RESOURCE_STATE_COMPUTE_READ) out|=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  if(state&FFX_API_RESOURCE_STATE_PIXEL_READ) out|=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  if(state&FFX_API_RESOURCE_STATE_COPY_SRC) out|=D3D12_RESOURCE_STATE_COPY_SOURCE;
  if(state&FFX_API_RESOURCE_STATE_COPY_DEST) out|=D3D12_RESOURCE_STATE_COPY_DEST;
  if(state&FFX_API_RESOURCE_STATE_INDIRECT_ARGUMENT) out|=D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
  if(state&FFX_API_RESOURCE_STATE_RENDER_TARGET) out|=D3D12_RESOURCE_STATE_RENDER_TARGET;
  return static_cast<D3D12_RESOURCE_STATES>(out); // COMMON and PRESENT are zero
}
const ffxApiHeader* Find(const ffxApiHeader* header,ffxStructType_t type) {
  for(unsigned depth=0;header && depth<16;header=header->pNext,++depth) if(header->type==type) return header;
  return nullptr;
}
void Log(const char* event,const std::string& fields) { xess_mod::WriteLog(event,fields); }
std::string Kind(const ffxApiHeader* desc) { return "\"desc_type\":"+std::to_string(desc?desc->type:0); }

// The adapter backs out (the game's own FidelityFX takes over) unless the
// swapchain it creates through DXGI becomes the Intel output for this window.
ffxReturnCode_t CreateSwapchain(ffxContext* context,ffxCreateContextDescHeader* desc,const ffxAllocationCallbacks* memory) {
  IDXGISwapChain4** out{}; HWND hwnd{}; IDXGIFactory* factory{}; ID3D12CommandQueue* queue{};
  const DXGI_SWAP_CHAIN_DESC1* modern{}; const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full{}; DXGI_SWAP_CHAIN_DESC* legacy{};
  if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12) {
    auto* d=reinterpret_cast<ffxCreateContextDescFrameGenerationSwapChainForHwndDX12*>(desc);
    out=d->swapchain; hwnd=d->hwnd; factory=d->dxgiFactory; queue=d->gameQueue; modern=d->desc; full=d->fullscreenDesc;
  } else {
    auto* d=reinterpret_cast<ffxCreateContextDescFrameGenerationSwapChainNewDX12*>(desc);
    out=d->swapchain; factory=d->dxgiFactory; queue=d->gameQueue; legacy=d->desc; hwnd=legacy?legacy->OutputWindow:nullptr;
  }
  if(!Enabled() || !out || !hwnd || !factory || !queue || (!modern && !legacy) || Active()) {
    Log("native_ffx_swapchain","\"adapter\":false,\"reason\":\"not_eligible\","+Kind(desc));
    return createContext(context,desc,memory);
  }
  ComPtr<IDXGISwapChain> chain; HRESULT hr;
  {
    creating=true;
    if(modern) {
      ComPtr<IDXGIFactory2> factory2; hr=factory->QueryInterface(IID_PPV_ARGS(&factory2));
      ComPtr<IDXGISwapChain1> chain1;
      if(SUCCEEDED(hr)) hr=factory2->CreateSwapChainForHwnd(queue,hwnd,modern,full,nullptr,&chain1);
      chain=chain1;
    } else hr=factory->CreateSwapChain(queue,legacy,&chain);
    creating=false;
  }
  auto o=Active();
  ComPtr<IDXGISwapChain4> chain4;
  if(FAILED(hr) || !o || o->hwnd!=hwnd || FAILED(chain.As(&chain4))) {
    chain4.Reset(); chain.Reset();
    Log("native_ffx_swapchain","\"adapter\":false,\"reason\":\"intel_output_unavailable\",\"hr\":"+std::to_string(int32_t(hr))+","+Kind(desc));
    return createContext(context,desc,memory);
  }
  {
    std::lock_guard guard(lock);
    ownedOutput=o.get(); lastFfxFrame=UINT64_MAX; hudless={}; ffxRequested=false;
  }
  synthetic::Begin(o,synthetic::kFidelityFx);
  *out=chain4.Detach(); *context=&swapchainContext;
  Log("native_ffx_swapchain","\"adapter\":true,\"output\":"+std::to_string(o->diagnosticOutput)+","+Kind(desc));
  return FFX_API_RETURN_OK;
}
ffxReturnCode_t CreateHook(ffxContext* context,ffxCreateContextDescHeader* desc,const ffxAllocationCallbacks* memory) {
  if(!context || !desc) return createContext(context,desc,memory);
  if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12
      || desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12)
    return CreateSwapchain(context,desc,memory);
  if(desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION && Owns()) {
    const auto* d=reinterpret_cast<const ffxCreateContextDescFrameGeneration*>(desc);
    {
      std::lock_guard guard(lock); generationFlags=d->flags;
    }
    // The route state (native generator off, XeLL as the only wait) changes
    // only when FSR frames are actually submitted (synthetic::SubmitFrame).
    auto& s=State();
    *context=&generationContext;
    Log("native_ffx_generation","\"flags\":"+std::to_string(d->flags)+",\"display_width\":"+std::to_string(d->displaySize.width)
        +",\"display_height\":"+std::to_string(d->displaySize.height)+",\"latency_output\":\""
        +(s.latencyOutput.load()==LatencyOutput::XeLL?"XeLL":"Reflex")+"\"");
    if(s.latencyOutput.load()!=LatencyOutput::XeLL)
      Report("XeMFG FidelityFX adapter needs the XeLL latency output (the game has no Reflex markers)");
    return FFX_API_RETURN_OK;
  }
  return createContext(context,desc,memory);
}
ffxReturnCode_t DestroyHook(ffxContext* context,const ffxAllocationCallbacks* memory) {
  if(!StandInContext(context)) return destroyContext(context,memory);
  std::lock_guard guard(lock);
  if(*context==&generationContext) { ffxRequested=false; if(synthetic::Driving(synthetic::kFidelityFx)) State().requested=false; }
  else { synthetic::End(ownedOutput); ownedOutput=nullptr; } // the swapchain follows the game's COM references
  Log("native_ffx_destroy","\"context\":\""+std::string(static_cast<StandIn*>(*context)->kind)+"\"");
  *context=nullptr;
  return FFX_API_RETURN_OK;
}
ffxReturnCode_t ConfigureHook(ffxContext* context,const ffxConfigureDescHeader* desc) {
  if(!StandInContext(context) || !desc) return configure(context,desc);
  if(*context==&generationContext) {
    if(const auto* c=reinterpret_cast<const ffxConfigureDescFrameGeneration*>(Find(desc,FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION))) {
      std::lock_guard guard(lock);
      const bool previous=ffxRequested;
      ffxRequested=c->frameGenerationEnabled;
      if(synthetic::Driving(synthetic::kFidelityFx)) State().requested=ffxRequested;
      hudless=c->HUDLessColor;
      if(previous!=c->frameGenerationEnabled)
        Log("native_ffx_configure","\"enabled\":"+std::string(c->frameGenerationEnabled?"true":"false")
            +",\"hudless\":"+(c->HUDLessColor.resource?"true":"false")+",\"present_callback\":"+(c->presentCallback?"true":"false")
            +",\"generation_callback\":"+(c->frameGenerationCallback?"true":"false"));
    }
  }
  // Swapchain key/values, UI registration and frame-generation key/values have
  // no Intel equivalent; accepting them keeps the game's configuration path.
  return FFX_API_RETURN_OK;
}
ffxReturnCode_t QueryHook(ffxContext* context,ffxQueryDescHeader* desc) {
  if(!StandInContext(context) || !desc) return query(context,desc);
  if(desc->type==FFX_API_QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE) {
    if(auto* usage=reinterpret_cast<ffxQueryDescFrameGenerationGetGPUMemoryUsage*>(desc)->gpuMemoryUsageFrameGeneration) *usage={};
    return FFX_API_RETURN_OK;
  }
  if(desc->type==FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_DX12) {
    if(auto* usage=reinterpret_cast<ffxQueryFrameGenerationSwapChainGetGPUMemoryUsageDX12*>(desc)->gpuMemoryUsageFrameGenerationSwapchain) *usage={};
    return FFX_API_RETURN_OK;
  }
  // Game-driven interpolation (its own command list/texture) is not offered.
  Log("native_ffx_query_refused",Kind(desc));
  return FFX_API_RETURN_ERROR_PARAMETER;
}
packet::Resource Input(xefg_swapchain_resource_type_t type,const FfxApiResource& r,packet::Lifecycle lifecycle,
    void* producer,FfxApiDimensions2D size) {
  packet::Resource input;
  input.type=type; input.native=static_cast<IUnknown*>(r.resource); input.headerValid=true;
  input.state=uint32_t(D3dState(r.state)); input.lifecycle=lifecycle; input.rawLifecycle=uint32_t(lifecycle);
  input.producer=static_cast<IUnknown*>(producer);
  input.extent={0,0,size.width,size.height};
  return input;
}
uint32_t Constants(const ffxDispatchDescFrameGenerationPrepare& p,uint32_t flags,const Output& o,
    xefg_swapchain_frame_constant_data_t& data,packet::Conventions& stated) {
  uint32_t issues{};
  const bool inverted=flags&FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
  const bool infinite=flags&FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
  const bool jittered=flags&FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
  stated.depthInverted=inverted; stated.motionJittered=jittered;
  if(inverted!=o.invert) issues|=ConstantDepth;
  if(jittered!=o.jitter) issues|=ConstantJitterMode;
  const float width=float(p.renderSize.width),height=float(p.renderSize.height);
  const float nearPlane=p.cameraNear,farPlane=infinite?1.e6f:p.cameraFar;
  if(!(width>0) || !(height>0) || !(p.cameraFovAngleVertical>0) || !(nearPlane>0) || !(farPlane>0) || nearPlane==farPlane) issues|=ConstantProjection;
  else {
    // Row-major, row-vector (DirectX) convention, as the Streamline route passes.
    DirectX::XMFLOAT4X4 projection;
    DirectX::XMStoreFloat4x4(&projection,DirectX::XMMatrixPerspectiveFovLH(p.cameraFovAngleVertical,width/height,
        inverted?farPlane:nearPlane,inverted?nearPlane:farPlane));
    memcpy(data.projectionMatrix,&projection,sizeof(data.projectionMatrix));
  }
  // FSR 3.1.4+ chains the camera basis; without it the view is identity and
  // camera motion is carried by the motion vectors alone.
  const auto* camera=reinterpret_cast<const ffxDispatchDescFrameGenerationPrepareCameraInfo*>(
      Find(p.header.pNext,FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO));
  for(unsigned i=0;i<16;++i) data.viewMatrix[i]=i%5==0?1.f:0.f;
  if(camera) {
    const float* basis[]={camera->cameraRight,camera->cameraUp,camera->cameraForward};
    for(unsigned col=0;col<3;++col) {
      const float* b=basis[col];
      data.viewMatrix[col]=b[0]; data.viewMatrix[4+col]=b[1]; data.viewMatrix[8+col]=b[2];
      data.viewMatrix[12+col]=-(b[0]*camera->cameraPosition[0]+b[1]*camera->cameraPosition[1]+b[2]*camera->cameraPosition[2]);
    }
  }
  data.jitterOffsetX=p.jitterOffset.x; data.jitterOffsetY=p.jitterOffset.y;
  // FidelityFX scales vectors to pixels; Streamline/XeFG convention here is
  // pixels per render size (BeforePresent multiplies by the tagged size).
  data.motionVectorScaleX=width>0?p.motionVectorScale.x/width:0.f;
  data.motionVectorScaleY=height>0?p.motionVectorScale.y/height:0.f;
  data.resetHistory=0; data.frameRenderTime=p.frameTimeDelta;
  for(float v:data.viewMatrix) if(!ConstantFinite(v)) issues|=ConstantView;
  for(float v:data.projectionMatrix) if(!ConstantFinite(v)) issues|=ConstantProjection;
  if(!ConstantFinite(data.jitterOffsetX) || !ConstantFinite(data.jitterOffsetY)) issues|=ConstantJitter;
  if(!ConstantFinite(data.motionVectorScaleX) || !ConstantFinite(data.motionVectorScaleY)
      || data.motionVectorScaleX==0 || data.motionVectorScaleY==0) issues|=ConstantMotionScale;
  return issues;
}
ffxReturnCode_t Prepare(const ffxDispatchDescFrameGenerationPrepare& p) {
  const auto o=Active();
  std::lock_guard guard(lock);
  // Only frames of an enabled FSR generator are taken over.
  const auto id=o && ffxRequested?synthetic::SubmitFrame(o,synthetic::kFidelityFx):0;
  if(!id) {
    if(prepareLogBudget<4) { ++prepareLogBudget; Log("native_ffx_prepare_skipped","\"active\":"+std::string(o?"true":"false")); }
    return FFX_API_RETURN_OK;
  }
  State().requested=true; // the synthesized frames are active and FSR frame generation is on
  const bool continuous=lastFfxFrame!=UINT64_MAX && p.frameID==lastFfxFrame+1;
  lastFfxFrame=p.frameID;
  packet::Conventions learn;
  {
    std::lock_guard io(o->io);
    if(auto* frame=FindFrame(*o,id)) {
      frame->viewport=0;
      packet::SubmitResource(*o,*frame,id,Input(XEFG_SWAPCHAIN_RES_DEPTH,p.depth,packet::Lifecycle::OnlyNow,p.commandList,p.renderSize));
      packet::SubmitResource(*o,*frame,id,Input(XEFG_SWAPCHAIN_RES_MOTION_VECTOR,p.motionVectors,packet::Lifecycle::OnlyNow,p.commandList,
          (generationFlags&FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS)
              ?FfxApiDimensions2D{p.motionVectors.description.width,p.motionVectors.description.height}:p.renderSize));
      if(hudless.resource)
        packet::SubmitResource(*o,*frame,id,Input(XEFG_SWAPCHAIN_RES_HUDLESS_COLOR,hudless,packet::Lifecycle::UntilPresent,nullptr,
            {hudless.description.width,hudless.description.height}));
      xefg_swapchain_frame_constant_data_t data{}; packet::Conventions stated;
      const auto issues=Constants(p,generationFlags,*o,data,stated);
      data.resetHistory=!continuous;
      learn=packet::SubmitConstants(*frame,issues,data,stated);
      if(prepareLogBudget<4 || (issues && prepareLogBudget<12)) {
        ++prepareLogBudget;
        Log("native_ffx_prepare","\"id\":"+std::to_string(id)+",\"frame_id\":"+std::to_string(p.frameID)
            +",\"issues\":"+std::to_string(issues)+",\"resources\":"+std::to_string(frame->resources)
            +",\"render_width\":"+std::to_string(p.renderSize.width)+",\"render_height\":"+std::to_string(p.renderSize.height));
      }
    }
  }
  if(learn.depthInverted || learn.motionJittered) LearnStatedConventions(learn.depthInverted,learn.motionJittered);
  return FFX_API_RETURN_OK;
}
ffxReturnCode_t DispatchHook(ffxContext* context,const ffxDispatchDescHeader* desc) {
  if(!StandInContext(context) || !desc) return dispatch(context,desc);
  if(*context==&generationContext && desc->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE)
    return Prepare(*reinterpret_cast<const ffxDispatchDescFrameGenerationPrepare*>(desc));
  // Manual generation dispatch and wait-for-presents: the Intel output paces
  // and generates on its own presentation path.
  return FFX_API_RETURN_OK;
}
}
bool CreatingOutput() noexcept { return creating; }
bool Owns() noexcept { return synthetic::Owner()==synthetic::kFidelityFx; }
void Discover() {
  static std::mutex once; std::lock_guard guard(once);
  for(const wchar_t* name:{L"amd_fidelityfx_loader_dx12.dll",L"amd_fidelityfx_dx12.dll"}) {
    HMODULE module=GetModuleHandleW(name);
    if(!module) continue;
    HMODULE pinned{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(module),&pinned)) continue;
    const auto entry=[&](const char* function) { return reinterpret_cast<void*>(GetProcAddress(module,function)); };
    // Every entry is attempted; the adapter is usable only when all five are.
    const bool hooks[]={Hook(entry("ffxCreateContext"),reinterpret_cast<void*>(&CreateHook),reinterpret_cast<void**>(&createContext)),
        Hook(entry("ffxDestroyContext"),reinterpret_cast<void*>(&DestroyHook),reinterpret_cast<void**>(&destroyContext)),
        Hook(entry("ffxConfigure"),reinterpret_cast<void*>(&ConfigureHook),reinterpret_cast<void**>(&configure)),
        Hook(entry("ffxQuery"),reinterpret_cast<void*>(&QueryHook),reinterpret_cast<void**>(&query)),
        Hook(entry("ffxDispatch"),reinterpret_cast<void*>(&DispatchHook),reinterpret_cast<void**>(&dispatch))};
    uint32_t mask{}; for(unsigned i=0;i<5;++i) mask|=uint32_t(hooks[i])<<i;
    const bool hooked=mask==31;
    static std::map<HMODULE,uint32_t> logged;
    if(auto& last=logged[module]; last!=mask+1) { last=mask+1;
      Log("native_ffx_hooks","\"module\":\""+std::string(name==std::wstring(L"amd_fidelityfx_dx12.dll")?"amd_fidelityfx_dx12":"amd_fidelityfx_loader_dx12")
          +"\",\"version\":\""+FileVersion(module)+"\",\"hooked\":"+(hooked?"true":"false")+",\"mask\":"+std::to_string(mask)); }
    if(hooked) return; // one provider per process; the loader forwards to the effect DLLs
  }
}
}
