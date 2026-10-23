// SPDX-License-Identifier: MIT
// Plan 4.4: the scRGB encoder on a WARP device against an ST 2084 / BT.2087
// reference. scRGB 1.0 is 80 nits; colors outside BT.2020 clip to its gamut
// and luminance clips at 10000 nits. Tolerance: one 10-bit code value.
#include "../src/addons/mfgunlock/xemfg/native_scrgb.hpp"
#include <DirectXPackedVector.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace mfgunlock::xemfg::native;
namespace {
void Check(HRESULT hr,const char* what) { if(FAILED(hr)) throw std::runtime_error(what); }
double Pq(double nits) {
  const double m1=0.1593017578125,m2=78.84375,c1=0.8359375,c2=18.8515625,c3=18.6875;
  const double y=std::pow(std::clamp(nits/10000.0,0.0,1.0),m1);
  return std::pow((c1+c2*y)/(1+c3*y),m2);
}
void Reference(const float in[4],double out[4]) {
  static const double m[3][3]={{0.6274040,0.3292820,0.0433136},{0.0690970,0.9195400,0.0113612},{0.0163916,0.0880132,0.8955950}};
  for(int r=0;r<3;++r) out[r]=Pq(std::max(0.0,m[r][0]*in[0]+m[r][1]*in[1]+m[r][2]*in[2])*80.0);
  out[3]=std::clamp(double(in[3]),0.0,1.0);
}
D3D12_RESOURCE_DESC Texture(DXGI_FORMAT format,UINT width,D3D12_RESOURCE_FLAGS flags) {
  D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=width; d.Height=1;
  d.DepthOrArraySize=1; d.MipLevels=1; d.Format=format; d.SampleDesc.Count=1; d.Flags=flags; return d;
}
}
int main() {
  try {
    ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter> warp; Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"warp adapter");
    ComPtr<ID3D12Device> device; Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"warp device");
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT}; ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");
    ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list; ComPtr<ID3D12Fence> fence; uint64_t value{};
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    const auto run=[&] {
      Check(list->Close(),"close"); ID3D12CommandList* raw=list.Get(); queue->ExecuteCommandLists(1,&raw);
      Check(queue->Signal(fence.Get(),++value),"signal"); while(fence->GetCompletedValue()<value) Sleep(0);
      Check(allocator->Reset(),"allocator reset"); Check(list->Reset(allocator.Get(),nullptr),"list reset");
    };
    // black, 80-nit white, 203-nit reference white, saturated red, a color
    // outside BT.709 (negative component), 10000+ nits, alpha below/above range.
    const float pixels[][4]={{0,0,0,1},{1,1,1,1},{2.5375f,2.5375f,2.5375f,.5f},{4,0,0,1},
      {-.2f,1.2f,.1f,1},{200,200,200,1},{.5f,.25f,.125f,-1},{1,1,1,2}};
    const UINT count=UINT(std::size(pixels));
    D3D12_HEAP_PROPERTIES defaults{D3D12_HEAP_TYPE_DEFAULT},upload{D3D12_HEAP_TYPE_UPLOAD},readback{D3D12_HEAP_TYPE_READBACK};
    auto sd=Texture(DXGI_FORMAT_R16G16B16A16_FLOAT,count,D3D12_RESOURCE_FLAG_NONE);
    auto td=Texture(ScrgbEncoder::kTargetFormat,count,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    ComPtr<ID3D12Resource> source,target,staging,result;
    Check(device->CreateCommittedResource(&defaults,D3D12_HEAP_FLAG_NONE,&sd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)),"source");
    Check(device->CreateCommittedResource(&defaults,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_PRESENT,nullptr,IID_PPV_ARGS(&target)),"target");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT in{},out{}; uint64_t inSize{},outSize{};
    device->GetCopyableFootprints(&sd,0,1,0,&in,nullptr,nullptr,&inSize);
    device->GetCopyableFootprints(&td,0,1,0,&out,nullptr,nullptr,&outSize);
    D3D12_RESOURCE_DESC bd{}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1;
    bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bd.Width=inSize; Check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&staging)),"upload");
    bd.Width=outSize; Check(device->CreateCommittedResource(&readback,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)),"readback");
    uint16_t* halves{}; Check(staging->Map(0,nullptr,reinterpret_cast<void**>(&halves)),"map upload");
    for(UINT i=0;i<count;++i) for(int c=0;c<4;++c) halves[i*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(pixels[i][c]);
    staging->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{source.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};
    D3D12_TEXTURE_COPY_LOCATION src{staging.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}}; src.PlacedFootprint=in;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={source.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT};
    list->ResourceBarrier(1,&b); run();
    ScrgbEncoder encoder;
    if(!encoder.Initialize(device.Get())) Check(encoder.LastError(),"encoder initialization");
    if(!encoder.Encode(queue.Get(),source.Get(),D3D12_RESOURCE_STATE_PRESENT,target.Get(),D3D12_RESOURCE_STATE_PRESENT) || !encoder.Drain(5000))
      Check(encoder.LastError(),"encode");
    b.Transition={target.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION rdst{result.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}}; rdst.PlacedFootprint=out;
    D3D12_TEXTURE_COPY_LOCATION rsrc{target.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};
    list->CopyTextureRegion(&rdst,0,0,0,&rsrc,nullptr); run();
    uint32_t* packed{}; Check(result->Map(0,nullptr,reinterpret_cast<void**>(&packed)),"map readback");
    double worst=0;
    for(UINT i=0;i<count;++i) {
      // The reference consumes the half-precision value the GPU actually read.
      float stored[4]; for(int c=0;c<4;++c) stored[c]=DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(pixels[i][c]));
      double expected[4]; Reference(stored,expected);
      const uint32_t v=packed[i];
      const double got[4]={(v&1023)/1023.0,((v>>10)&1023)/1023.0,((v>>20)&1023)/1023.0,(v>>30)/3.0};
      for(int c=0;c<4;++c) {
        const double tolerance=c==3?.5/3.0+1e-6:1.0/1023.0;
        const double error=std::fabs(got[c]-expected[c]);
        if(c<3) worst=std::max(worst,error);
        if(error>tolerance) {
          std::fprintf(stderr,"pixel %u channel %d: got %.5f expected %.5f\n",i,c,got[c],expected[c]);
          return 1;
        }
      }
    }
    result->Unmap(0,nullptr);
    std::printf("PASS: scRGB encoded to HDR10 PQ BT.2020 within one 10-bit code (max error %.2g)\n",worst);
    return 0;
  } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
