// SPDX-License-Identifier: MIT
#pragma once
// Test-only public native-object wrapper; no third-party implementation.
class NativeQueueProxy final:public ID3D12CommandQueue {
  std::atomic<ULONG> refs{1};
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
public:
  explicit NativeQueueProxy(ID3D12CommandQueue* q):queue(q){}
  ULONG References() const {return refs.load();}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
    if(!out)return E_POINTER;*out=nullptr;
    constexpr GUID base={0xadec44e2,0x61f0,0x45c3,{0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}};
    if(id==base){*out=queue.Get();queue->AddRef();return S_OK;}
    if(id==__uuidof(IUnknown)||id==__uuidof(ID3D12Object)||id==__uuidof(ID3D12DeviceChild)||id==__uuidof(ID3D12Pageable)||id==__uuidof(ID3D12CommandQueue)){
      *out=static_cast<ID3D12CommandQueue*>(this);AddRef();return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
  ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c) override{return queue->GetPrivateData(a,b,c);}
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c) override{return queue->SetPrivateData(a,b,c);}
  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b) override{return queue->SetPrivateDataInterface(a,b);}
  HRESULT STDMETHODCALLTYPE SetName(LPCWSTR a) override{return queue->SetName(a);}
  HRESULT STDMETHODCALLTYPE GetDevice(REFIID a,void** b) override{return queue->GetDevice(a,b);}
  void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource* a,UINT b,const D3D12_TILED_RESOURCE_COORDINATE* c,const D3D12_TILE_REGION_SIZE* d,ID3D12Heap* e,UINT f,const D3D12_TILE_RANGE_FLAGS* g,const UINT* h,const UINT* i,D3D12_TILE_MAPPING_FLAGS j) override{queue->UpdateTileMappings(a,b,c,d,e,f,g,h,i,j);}
  void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* a,const D3D12_TILED_RESOURCE_COORDINATE* b,ID3D12Resource* c,const D3D12_TILED_RESOURCE_COORDINATE* d,const D3D12_TILE_REGION_SIZE* e,D3D12_TILE_MAPPING_FLAGS f) override{queue->CopyTileMappings(a,b,c,d,e,f);}
  void STDMETHODCALLTYPE ExecuteCommandLists(UINT a,ID3D12CommandList*const* b) override{queue->ExecuteCommandLists(a,b);}
  void STDMETHODCALLTYPE SetMarker(UINT a,const void* b,UINT c) override{queue->SetMarker(a,b,c);}
  void STDMETHODCALLTYPE BeginEvent(UINT a,const void* b,UINT c) override{queue->BeginEvent(a,b,c);}
  void STDMETHODCALLTYPE EndEvent() override{queue->EndEvent();}
  HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* a,UINT64 b) override{return queue->Signal(a,b);}
  HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* a,UINT64 b) override{return queue->Wait(a,b);}
  HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* a) override{return queue->GetTimestampFrequency(a);}
  HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* a,UINT64* b) override{return queue->GetClockCalibration(a,b);}
  D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override{return queue->GetDesc();}
};
