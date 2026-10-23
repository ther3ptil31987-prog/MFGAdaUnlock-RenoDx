// SPDX-License-Identifier: MIT
#include "native_trace.hpp"
#include "vendor/xess_mod.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>
namespace mfgunlock::xemfg::native {
namespace {
std::atomic<bool> enabled{},gpuEnabled{};
struct InFlight {
  std::atomic<uint64_t> ticket{},start{},context{},frame{},lastLog{};
  std::atomic<const char*> name{};
  std::atomic<DWORD> thread{};
};
std::array<InFlight,64> calls;
std::atomic<uint64_t> sequence{};
uint64_t Now() noexcept { LARGE_INTEGER n{}; QueryPerformanceCounter(&n); return n.QuadPart; }
uint64_t Frequency() noexcept { static const auto f=[] {LARGE_INTEGER n{};QueryPerformanceFrequency(&n);return uint64_t(n.QuadPart);}(); return f; }
uint64_t Micros(uint64_t ticks) noexcept { return uint64_t(double(ticks)*1.e6/Frequency()); }
void Log(const char* event,const std::string& fields) noexcept { xess_mod::WriteLog(event,fields); }
struct Progress {
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  uint64_t output{};
  const char* queueName{"source"};
  std::atomic<uint64_t> begin{},end{},frame{};
};
std::array<std::atomic<std::shared_ptr<Progress>>,16> progress;
}
void ConfigureTrace(bool cpu,bool gpu) noexcept { enabled=cpu; gpuEnabled=cpu&&gpu; }
bool TraceEnabled() noexcept { return enabled.load(std::memory_order_relaxed); }
bool GpuTraceEnabled() noexcept { return gpuEnabled.load(std::memory_order_relaxed); }
void StartTraceWorker() {
  if(!TraceEnabled()) return;
  static std::once_flag once;
  std::call_once(once,[] { std::thread([] {
    for(;;) {
      Sleep(250);
      if(!TraceEnabled()) continue;
      const auto now=Now();
      for(auto& c:calls) {
        const auto stamp=c.start.load(std::memory_order_acquire);
        if(stamp<=1 || stamp>now || Micros(now-stamp)<500000) continue;
        const auto previous=c.lastLog.load();
        if(previous && Micros(now-previous)<1000000) continue;
        c.lastLog=now;
        const auto ticket=c.ticket.load(),context=c.context.load(),frame=c.frame.load();
        const auto name=c.name.load(); const auto tid=c.thread.load();
        if(c.start.load(std::memory_order_acquire)!=stamp || !name) continue;
        std::string fields="\"call\":\""+std::string(name)+"\",\"ticket\":"+std::to_string(ticket)
            +",\"output\":"+std::to_string(context)+",\"frame\":"+std::to_string(frame)
            +",\"thread\":"+std::to_string(tid)+",\"elapsed_us\":"+std::to_string(Micros(now-stamp));
        for(auto& entry:progress) if(auto p=entry.load(); p && p->output==context) {
          const std::string prefix=strcmp(p->queueName,"source")==0?"gpu_":"presentation_gpu_";
          fields+=",\""+prefix+"begin_fence\":"+std::to_string(p->begin.load())+",\""+prefix+"end_fence\":"+std::to_string(p->end.load())
              +",\""+prefix+"completed\":"+std::to_string(p->fence->GetCompletedValue());
        }
        Log("runtime_call_pending",fields);
      }
    }
  }).detach(); });
}
RuntimeCall::RuntimeCall(const char* operation,uint64_t output,uint64_t frameId) noexcept {
  if(!TraceEnabled()) return;
  const auto lastError=GetLastError();
  name=operation;context=output;frame=frameId;ticket=++sequence;start=Now();
  for(unsigned i=0;i<calls.size();++i) {
    uint64_t vacant{};
    if(!calls[i].start.compare_exchange_strong(vacant,1)) continue;
    slot=i; auto& c=calls[i]; c.ticket=ticket;c.context=context;c.frame=frame;c.name=name;
    c.thread=GetCurrentThreadId();c.lastLog=0;c.start.store(start,std::memory_order_release);break;
  }
  SetLastError(lastError);
}
RuntimeCall::~RuntimeCall() {
  if(!name) return;
  const auto lastError=GetLastError();
  const auto elapsed=Micros(Now()-start);
  if(slot<calls.size()) calls[slot].start.store(0,std::memory_order_release);
  if(elapsed>=100000 || failed) Log("runtime_call", "\"call\":\""+std::string(name)+"\",\"ticket\":"+std::to_string(ticket)
      +",\"output\":"+std::to_string(context)+",\"frame\":"+std::to_string(frame)
      +",\"thread\":"+std::to_string(GetCurrentThreadId())+",\"duration_us\":"+std::to_string(elapsed)
      +",\"result_known\":"+(resultKnown?"true":"false")+",\"failed\":"+(failed?"true":"false")+",\"result\":"+std::to_string(result));
  SetLastError(lastError);
}
struct QueueTrace::Impl {
  using Queue=Microsoft::WRL::ComPtr<ID3D12CommandQueue>;
  using Allocator=Microsoft::WRL::ComPtr<ID3D12CommandAllocator>;
  using List=Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>;
  struct Slot { Allocator allocator[2]; List list[2]; uint64_t begin{},end{}; uint32_t frame{}; };
  std::array<Slot,8> slots;
  Queue queue;
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries;
  Microsoft::WRL::ComPtr<ID3D12Resource> readback;
  std::shared_ptr<Progress> state;
  uint64_t serial{},frequency{};
  unsigned active{~0u},registry{~0u};
  bool usable{},unknownSubmission{};
  static void Reap(Impl* retired=nullptr) {
    static std::mutex lock;static auto* pending=new std::vector<Impl*>;
    std::lock_guard guard(lock);
    for(auto it=pending->begin();it!=pending->end();) {
      auto* p=*it;const auto value=p->state->fence->GetCompletedValue();
      if(value==UINT64_MAX || (!p->unknownSubmission && value>=p->serial)) {delete p;it=pending->erase(it);}
      else ++it;
    }
    if(retired)pending->push_back(retired);
    if(pending->size()>=4)gpuEnabled=false;
  }
  void Poll() {
    const auto completed=state->fence->GetCompletedValue();
    if(completed==UINT64_MAX) { usable=false; return; }
    for(unsigned i=0;i<slots.size();++i) {
      auto& s=slots[i]; if(!s.end || s.end>completed) continue;
      void* bytes{}; D3D12_RANGE range{size_t(i*16),size_t(i*16+16)};
      if(SUCCEEDED(readback->Map(0,&range,&bytes))) {
        uint64_t pair[2]; memcpy(pair,static_cast<char*>(bytes)+i*16,16);
        D3D12_RANGE written{};readback->Unmap(0,&written);
        if(pair[1]>=pair[0]) Log("gpu_queue_span","\"output\":"+std::to_string(state->output)+",\"frame\":"+std::to_string(s.frame)
            +",\"queue\":\""+state->queueName+"\",\"duration_us\":"+std::to_string(double(pair[1]-pair[0])*1.e6/frequency)
            +",\"end_gpu_tick\":"+std::to_string(pair[1])+",\"observed_cpu_qpc\":"+std::to_string(Now())
            +",\"fence\":"+std::to_string(s.end));
      }
      s.begin=s.end=0;
    }
  }
};
QueueTrace::QueueTrace(ID3D12Device* device,ID3D12CommandQueue* queue,uint64_t output,const char* queueName) {
  Impl::Reap();
  if(!GpuTraceEnabled()) return;
  auto p=std::make_unique<Impl>();p->queue=queue;p->state=std::make_shared<Progress>();p->state->output=output;
  p->state->queueName=queueName;
  if(FAILED(queue->GetTimestampFrequency(&p->frequency)) || !p->frequency
      || FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&p->state->fence)))) return;
  D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=16;
  if(FAILED(device->CreateQueryHeap(&query,IID_PPV_ARGS(&p->queries)))) return;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=128;desc.Height=1;
  desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&p->readback)))) return;
  for(auto& s:p->slots) for(unsigned stage=0;stage<2;++stage) {
    if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator[stage])))
        || FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator[stage].Get(),nullptr,IID_PPV_ARGS(&s.list[stage])))) return;
    s.list[stage]->Close();
  }
  for(unsigned i=0;i<progress.size();++i) { std::shared_ptr<Progress> empty;
    if(progress[i].compare_exchange_strong(empty,p->state)) {p->registry=i;break;}
  }
  p->usable=true;impl=p.release();
  UINT64 gpu{},cpu{};
  if(SUCCEEDED(queue->GetClockCalibration(&gpu,&cpu))) Log("gpu_queue_clock","\"output\":"+std::to_string(output)
      +",\"queue\":\""+queueName+"\",\"gpu_tick\":"+std::to_string(gpu)+",\"cpu_qpc\":"+std::to_string(cpu)
      +",\"gpu_frequency\":"+std::to_string(impl->frequency)+",\"cpu_frequency\":"+std::to_string(Frequency()));
}
QueueTrace::~QueueTrace() {
  if(!impl) return;
  if(impl->registry<progress.size()) progress[impl->registry].store({});
  const auto complete=impl->state->fence->GetCompletedValue();
  // Never release an allocator/query/readback still referenced by the GPU,
  // and never add a diagnostic wait to a game. A failed signal is unknown.
  if(impl->unknownSubmission || (complete!=UINT64_MAX && complete<impl->serial)) {
    const auto output=impl->state->output;
    Impl::Reap(impl); // Reap completed arenas on the next output; never wait here.
    Log("gpu_trace_retirement_pending","\"output\":"+std::to_string(output));return;
  }
  delete impl;
}
void QueueTrace::Begin(uint32_t frame) {
  if(!impl || !impl->usable || !GpuTraceEnabled()) return;
  auto& p=*impl;p.Poll();if(!p.usable || p.active<p.slots.size()) return;
  for(unsigned i=0;i<p.slots.size();++i) if(!p.slots[i].begin) {
    auto& s=p.slots[i];s.frame=frame;
    if(FAILED(s.allocator[0]->Reset()) || FAILED(s.list[0]->Reset(s.allocator[0].Get(),nullptr))) {p.usable=false;return;}
    s.list[0]->EndQuery(p.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,i*2);
    if(FAILED(s.list[0]->Close())) {p.usable=false;return;}
    ID3D12CommandList* list=s.list[0].Get();p.queue->ExecuteCommandLists(1,&list);
    s.begin=++p.serial;p.state->begin=s.begin;p.state->frame=frame;
    if(FAILED(p.queue->Signal(p.state->fence.Get(),s.begin))) {p.usable=false;p.unknownSubmission=true;return;}
    p.active=i;return;
  }
}
void QueueTrace::End() {
  if(!impl || !impl->usable || impl->active>=impl->slots.size()) return;
  auto& p=*impl;const auto i=p.active;auto& s=p.slots[i];p.active=~0u;
  if(FAILED(s.allocator[1]->Reset()) || FAILED(s.list[1]->Reset(s.allocator[1].Get(),nullptr))) {p.usable=false;return;}
  s.list[1]->EndQuery(p.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,i*2+1);
  s.list[1]->ResolveQueryData(p.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,i*2,2,p.readback.Get(),i*16);
  if(FAILED(s.list[1]->Close())) {p.usable=false;return;}
  ID3D12CommandList* list=s.list[1].Get();p.queue->ExecuteCommandLists(1,&list);
  s.end=++p.serial;p.state->end=s.end;
  if(FAILED(p.queue->Signal(p.state->fence.Get(),s.end))) {p.usable=false;p.unknownSubmission=true;return;}
  p.Poll();
}
}
extern "C" __declspec(dllexport) void MFGAXeStallTraceEnable(bool cpu,bool gpu) {
  mfgunlock::xemfg::native::ConfigureTrace(cpu,gpu);mfgunlock::xemfg::native::StartTraceWorker();
}
