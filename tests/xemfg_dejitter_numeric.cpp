// SPDX-License-Identifier: MIT
// Plan 4.1/4.2: a static camera with Halton jitter and jittered motion vectors
// must convert to motion ~0; true object motion must survive the conversion.
// Convention (Streamline sl_consts.h): jitterOffset is in pixels, mvecScale
// normalizes the buffer, the vector points from the current sample to the
// previous one. A static point rendered with jitter j moves by j_prev - j_cur.
// The conversion runs the shipped compute pass on a WARP device.
#include "../src/addons/mfgunlock/xemfg/native_dejitter.hpp"
#include <dxgi1_6.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace mfgunlock::xemfg::native;
namespace {
struct Float2 { float x,y; };
void Check(HRESULT hr,const char* what) { if(FAILED(hr)) throw std::runtime_error(what); }
float Halton(unsigned index,unsigned base) {
  float f=1,r=0; for(unsigned i=index;i;i/=base) { f/=float(base); r+=f*float(i%base); } return r;
}
Float2 Jitter(unsigned frame) { return {Halton(frame%8+1,2)-.5f,Halton(frame%8+1,3)-.5f}; }
// texture: full allocation; region: the tagged subrectangle; capture: the
// volatile (ONLY_NOW) path copies on a producer list first.
struct Case { const char* name; unsigned width,height; DejitterRegion region; Float2 scale; bool capture; };
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
  ComPtr<ID3D12Resource> Upload(const std::vector<Float2>& data,unsigned width,unsigned height) {
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT}; D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=width; d.Height=height; d.DepthOrArraySize=1; d.MipLevels=1;
    d.Format=DXGI_FORMAT_R32G32_FLOAT; d.SampleDesc.Count=1;
    ComPtr<ID3D12Resource> texture;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)),"texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; uint64_t total{};
    device->GetCopyableFootprints(&d,0,1,0,&layout,nullptr,nullptr,&total);
    auto upload=Buffer(D3D12_HEAP_TYPE_UPLOAD,total);
    uint8_t* mapped{}; Check(upload->Map(0,nullptr,reinterpret_cast<void**>(&mapped)),"map upload");
    for(unsigned y=0;y<height;++y) memcpy(mapped+layout.Offset+size_t(y)*layout.Footprint.RowPitch,&data[size_t(y)*width],width*sizeof(Float2));
    upload->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{texture.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};
    D3D12_TEXTURE_COPY_LOCATION src{upload.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}}; src.PlacedFootprint=layout;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    list->ResourceBarrier(1,&b);
    Run();
    return texture;
  }
  // Reads a NON_PIXEL_SHADER_RESOURCE texture back as float2 (32 or 16 bit).
  std::vector<Float2> Read(ID3D12Resource* texture) {
    const auto d=texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; uint64_t total{};
    device->GetCopyableFootprints(&d,0,1,0,&layout,nullptr,nullptr,&total);
    auto readback=Buffer(D3D12_HEAP_TYPE_READBACK,total);
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION dst{readback.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}}; dst.PlacedFootprint=layout;
    D3D12_TEXTURE_COPY_LOCATION src{texture,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    std::swap(b.Transition.StateBefore,b.Transition.StateAfter); list->ResourceBarrier(1,&b);
    Run();
    if(d.Format!=DXGI_FORMAT_R32G32_FLOAT) throw std::runtime_error("32-bit input produced a reduced-precision output");
    std::vector<Float2> out(size_t(d.Width)*d.Height);
    uint8_t* mapped{}; Check(readback->Map(0,nullptr,reinterpret_cast<void**>(&mapped)),"map readback");
    for(unsigned y=0;y<d.Height;++y) memcpy(&out[size_t(y)*d.Width],mapped+layout.Offset+size_t(y)*layout.Footprint.RowPitch,size_t(d.Width)*sizeof(Float2));
    readback->Unmap(0,nullptr);
    return out;
  }
};
}
int main() {
  try {
    Gpu gpu; MotionDejitter dejitter;
    Check(dejitter.Initialize(gpu.device.Get())?S_OK:dejitter.LastError(),"dejitter initialization");
    const Case cases[]={{"pixel-scale",64,36,{0,0,64,36},{1.f/64,1.f/36},false},
      {"signed-scale",64,36,{0,0,64,36},{-1.f/64,1.f/36},false},
      {"uv-units",48,48,{0,0,48,48},{1.f,1.f},false},
      {"volatile-subrect",72,40,{4,2,64,36},{1.f/64,-1.f/36},true}};
    const Float2 object{3.25f,-1.5f}; // true motion of the moving block, pixels
    float worst=0;
    for(const auto& c:cases) for(unsigned frame=1;frame<9;++frame) {
      const Float2 prev=Jitter(frame-1),cur=Jitter(frame);
      const unsigned w=c.region.width,h=c.region.height;
      // Buffer units: pixels = value * scale * size of the tagged region.
      const float toBufferX=1.f/(c.scale.x*float(w)),toBufferY=1.f/(c.scale.y*float(h));
      std::vector<Float2> mv(size_t(c.width)*c.height,Float2{99.f,-99.f}); // outside the tag: never read
      for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
        const bool moving=x>=8 && x<24 && y>=8 && y<20;
        const float px=(moving?object.x:0)+prev.x-cur.x,py=(moving?object.y:0)+prev.y-cur.y;
        mv[size_t(y+c.region.y)*c.width+x+c.region.x]={px*toBufferX,py*toBufferY};
      }
      auto source=gpu.Upload(mv,c.width,c.height);
      DejitterVector delta{};
      if(!DejitterDelta({prev.x,prev.y},{cur.x,cur.y},{c.scale.x,c.scale.y},w,h,delta)) throw std::runtime_error("valid scale refused");
      ComPtr<ID3D12Resource> input=source; DejitterRegion region=c.region;
      D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      if(c.capture) {
        if(!dejitter.Capture(gpu.list.Get(),source.Get(),state,c.region,frame,input)) throw std::runtime_error("capture failed");
        gpu.Run(); region={0,0,w,h}; state=D3D12_RESOURCE_STATE_COMMON;
      }
      ID3D12Resource* output{};
      if(!dejitter.Convert(input.Get(),state,region,delta,&output) || !dejitter.Submit(gpu.queue.Get()) || !dejitter.Drain(5000))
        throw std::runtime_error("conversion failed");
      const auto out=gpu.Read(output);
      for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
        const bool moving=x>=8 && x<24 && y>=8 && y<20;
        const Float2 v=out[size_t(y)*w+x];
        const float ex=v.x/toBufferX-(moving?object.x:0),ey=v.y/toBufferY-(moving?object.y:0);
        worst=std::fmax(worst,std::fmax(std::fabs(ex),std::fabs(ey)));
      }
      if(!(worst<1e-3f)) { std::fprintf(stderr,"%s frame %u: residual jitter %.4f px\n",c.name,frame,worst); return 1; }
    }
    DejitterVector unused{};
    if(DejitterDelta({0,0},{1,1},{0,1},64,36,unused)) throw std::runtime_error("zero scale accepted");
    std::printf("PASS: static camera converts to zero motion, object motion preserved (max error %.2g px)\n",worst);
    return 0;
  } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
