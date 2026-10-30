// SPDX-License-Identifier: MIT
// 1.7 (plan E.2): HUD stabilization on a WARP device. A scene that changes
// every frame with a static HUD block over it: after kThreshold unchanged real
// frames the UI output holds the HUD (alpha 1, colour from the newest frame)
// with one pixel of dilation, and the scene stays transparent. The HUD-less
// output is an exact copy of the back buffer. A size change restarts history.
#include "../src/addons/mfgunlock/xemfg/native_ui_mask.hpp"
#include <dxgi1_6.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace mfgunlock::xemfg::native;
namespace {
void Check(HRESULT hr,const char* what) { if(FAILED(hr)) throw std::runtime_error(what); }
void Require(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
struct Gpu {
  ComPtr<ID3D12Device> device; ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence; uint64_t value{};
  Gpu() {
    ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter> warp; Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"warp adapter");
    Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"warp device");
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT}; Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
  }
  void Run() {
    Check(list->Close(),"close"); ID3D12CommandList* raw=list.Get(); queue->ExecuteCommandLists(1,&raw);
    Check(queue->Signal(fence.Get(),++value),"signal");
    while(fence->GetCompletedValue()<value) Sleep(0);
    Check(allocator->Reset(),"allocator reset"); Check(list->Reset(allocator.Get(),nullptr),"list reset");
  }
  ComPtr<ID3D12Resource> Buffer(D3D12_HEAP_TYPE type,uint64_t size) {
    D3D12_HEAP_PROPERTIES heap{type}; D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=size; d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1;
    d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
        type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)),"buffer");
    return r;
  }
  void Barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to}; list->ResourceBarrier(1,&b);
  }
  // A back buffer: RGBA8, left in PRESENT (COMMON) like a swap-chain buffer.
  ComPtr<ID3D12Resource> BackBuffer(const std::vector<uint32_t>& pixels,unsigned width,unsigned height) {
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT}; D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=width; d.Height=height; d.DepthOrArraySize=1; d.MipLevels=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1;
    ComPtr<ID3D12Resource> texture;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)),"texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; uint64_t total{};
    device->GetCopyableFootprints(&d,0,1,0,&layout,nullptr,nullptr,&total);
    auto upload=Buffer(D3D12_HEAP_TYPE_UPLOAD,total);
    uint8_t* mapped{}; Check(upload->Map(0,nullptr,reinterpret_cast<void**>(&mapped)),"map upload");
    for(unsigned y=0;y<height;++y) memcpy(mapped+layout.Offset+size_t(y)*layout.Footprint.RowPitch,&pixels[size_t(y)*width],width*4);
    upload->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{texture.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};
    D3D12_TEXTURE_COPY_LOCATION src{upload.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}}; src.PlacedFootprint=layout;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    Barrier(texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
    Run();
    return texture;
  }
  // Reads a NON_PIXEL_SHADER_RESOURCE RGBA8 texture.
  std::vector<uint32_t> Read(ID3D12Resource* texture) {
    const auto d=texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; uint64_t total{};
    device->GetCopyableFootprints(&d,0,1,0,&layout,nullptr,nullptr,&total);
    auto readback=Buffer(D3D12_HEAP_TYPE_READBACK,total);
    Barrier(texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{readback.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}}; dst.PlacedFootprint=layout;
    D3D12_TEXTURE_COPY_LOCATION src{texture,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    Barrier(texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Run();
    std::vector<uint32_t> out(size_t(d.Width)*d.Height);
    uint8_t* mapped{}; Check(readback->Map(0,nullptr,reinterpret_cast<void**>(&mapped)),"map readback");
    for(unsigned y=0;y<d.Height;++y) memcpy(&out[size_t(y)*d.Width],mapped+layout.Offset+size_t(y)*layout.Footprint.RowPitch,size_t(d.Width)*4);
    readback->Unmap(0,nullptr);
    return out;
  }
};
constexpr unsigned kWidth=48, kHeight=32;
constexpr uint32_t kHud=0xFF20E0F0u; // ABGR: an opaque HUD colour
bool InHud(unsigned x,unsigned y) { return x>=10 && x<20 && y>=6 && y<12; }
bool NearHud(unsigned x,unsigned y) { return x+1>=10 && x<=20 && y+1>=6 && y<=12; }
// The scene changes every frame by far more than the tolerance.
uint32_t Scene(unsigned x,unsigned y,unsigned frame) {
  const uint32_t v=(x*7+y*13+frame*40)&0xFF;
  return 0xFF000000u|v|(((v+85)&0xFF)<<8)|(((v+170)&0xFF)<<16);
}
std::vector<uint32_t> Frame(unsigned frame,unsigned width=kWidth,unsigned height=kHeight) {
  std::vector<uint32_t> pixels(size_t(width)*height);
  for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
    pixels[size_t(y)*width+x]=InHud(x,y)?kHud:Scene(x,y,frame);
  return pixels;
}
}
int main() {
  try {
    Gpu gpu; UiMask mask;
    Require(UiMask::SupportedFormat(DXGI_FORMAT_R8G8B8A8_UNORM) && !UiMask::SupportedFormat(DXGI_FORMAT_R16G16B16A16_FLOAT),
        "format filter");
    Check(mask.Initialize(gpu.device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM)?S_OK:mask.LastError(),"mask initialization");
    for(unsigned frame=0;frame<UiMask::kThreshold+3;++frame) {
      const auto pixels=Frame(frame);
      auto backbuffer=gpu.BackBuffer(pixels,kWidth,kHeight);
      ID3D12Resource* hudless{}; ID3D12Resource* ui{};
      Require(mask.Record(backbuffer.Get(),D3D12_RESOURCE_STATE_PRESENT,nullptr,D3D12_RESOURCE_STATE_COMMON,{},
          &hudless,&ui)!=nullptr && !mask.UsedReference(),"record");
      Require(mask.Submit(gpu.queue.Get()) && mask.Drain(5000),"submit");
      Require(gpu.Read(hudless)==pixels,"HUD-less is not an exact copy of the back buffer");
      const auto out=gpu.Read(ui);
      // Frame 0 has no history; frame k has k unchanged frames behind it.
      const bool hudExpected=frame>=UiMask::kThreshold;
      for(unsigned y=0;y<kHeight;++y) for(unsigned x=0;x<kWidth;++x) {
        const uint32_t v=out[size_t(y)*kWidth+x];
        if(InHud(x,y)) Require(hudExpected?v==kHud:v==0,"HUD pixel");
        else if(!NearHud(x,y)) Require(v==0,"scene pixel composed as HUD");
        else if(hudExpected && frame>UiMask::kThreshold)
          Require(v==pixels[size_t(y)*kWidth+x],"dilated edge does not carry the newest frame");
      }
    }
    // A new size restarts history: nothing is composed on its first frame.
    const auto resized=Frame(1,32,16);
    auto backbuffer=gpu.BackBuffer(resized,32,16);
    ID3D12Resource* hudless{}; ID3D12Resource* ui{};
    Require(mask.Record(backbuffer.Get(),D3D12_RESOURCE_STATE_PRESENT,nullptr,D3D12_RESOURCE_STATE_COMMON,{},&hudless,&ui)
        && mask.Submit(gpu.queue.Get())
        && mask.Drain(5000),"resized record");
    for(const auto v:gpu.Read(ui)) Require(v==0,"history survived a size change");
    // With the game's HUD-less image, coverage decides: a semi-transparent HUD
    // block over a scene that changes every frame (never still) is composed
    // after kReferenceThreshold frames, and the scene never is.
    for(unsigned frame=0;frame<UiMask::kReferenceThreshold+2;++frame) {
      std::vector<uint32_t> scene(size_t(kWidth)*kHeight), pixels(scene.size());
      for(unsigned y=0;y<kHeight;++y) for(unsigned x=0;x<kWidth;++x) {
        const uint32_t s=Scene(x,y,frame), i=size_t(y)*kWidth+x;
        scene[i]=s;
        // 50% white over the scene, per channel.
        pixels[i]=InHud(x,y)?0xFF000000u|(((s&0xFF)+255)/2)|((((s>>8&0xFF)+255)/2)<<8)|((((s>>16&0xFF)+255)/2)<<16):s;
      }
      auto bb=gpu.BackBuffer(pixels,kWidth,kHeight);
      auto reference=gpu.BackBuffer(scene,kWidth,kHeight);
      Require(mask.Record(bb.Get(),D3D12_RESOURCE_STATE_PRESENT,reference.Get(),D3D12_RESOURCE_STATE_PRESENT,
          {0,0,kWidth,kHeight},&hudless,&ui) && mask.UsedReference() && mask.Submit(gpu.queue.Get()) && mask.Drain(5000),
          "reference record");
      const auto out=gpu.Read(ui);
      const bool hudExpected=frame+1>=UiMask::kReferenceThreshold;
      for(unsigned y=0;y<kHeight;++y) for(unsigned x=0;x<kWidth;++x) {
        const uint32_t v=out[size_t(y)*kWidth+x];
        if(InHud(x,y) && !(hudExpected?v==pixels[size_t(y)*kWidth+x]:v==0)) {
          std::fprintf(stderr,"frame %u x %u y %u got %08X want %08X\n",frame,x,y,unsigned(v),
              unsigned(hudExpected?pixels[size_t(y)*kWidth+x]:0u));
          throw std::runtime_error("covered HUD pixel");
        }
        else if(!NearHud(x,y)) Require(v==0,"scene composed with a HUD-less reference");
      }
    }
    std::printf("PASS: static HUD composed after %u unchanged frames (covered HUD with a HUD-less reference too), scene transparent, HUD-less exact\n",UiMask::kThreshold);
    return 0;
  } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
