// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/native_internal.hpp"
#include <map>
#include <chrono>
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace mfgunlock::xemfg::native;
namespace {
struct Allocation final : IUnknown {
  ULONG references{1};
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
    if(!out) return E_POINTER; *out=nullptr;
    if(id!=__uuidof(IUnknown)) return E_NOINTERFACE;
    *out=this; AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { return --references; }
};
void Check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
template<bool Fixed> double Bench(uint32_t frames,uint64_t& checksum) {
  FrameStore<Frame> fixed;
  std::map<uint32_t,Frame> mapped;
  const auto begin=std::chrono::steady_clock::now();
  for(uint32_t id=1;id<=frames;++id) {
    for(uint32_t call=0;call<10;++call) {
      Frame* f;
      if constexpr(Fixed) f=fixed.Acquire(id);
      else { f=&mapped[id]; f->id=id; }
      f->markers|=1u<<(call%7); checksum+=f->markers;
    }
    if constexpr(Fixed) fixed.Prune(id);
    else for(auto i=mapped.begin();i!=mapped.end();) {
      if(i->first+2<id) i=mapped.erase(i); else ++i;
    }
  }
  return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
}
}
int main(int argc,char** argv) {
  try {
    FrameStore<Frame> store;
    Check(!store.Acquire(0),"reserved zero admitted");
    for(uint32_t i=1;i<=16;++i) { auto* f=store.Acquire(i); Check(f && f->id==i,"capacity lost"); f->resources=i; }
    Check(!store.Acquire(17) && store.Find(8)->resources==8,"live slot evicted");
    store.Prune(10);
    Check(!store.Find(7) && store.Find(8) && store.Find(16),"late/future frame retention changed");
    Check(store.Acquire(17)->resources==0,"reused slot inherited tags");
    store.clear(); Check(store.empty(),"reset did not clear entries");
    Allocation allocation;
    store.Acquire(1)->hudAllocations[0]=&allocation;
    Check(allocation.references==2,"HUD allocation identity was not pinned");
    store.Prune(4);
    Check(allocation.references==1,"retired HUD allocation identity was leaked");
    store.Acquire(5)->hudAllocations[0]=&allocation;
    store.clear(); Check(allocation.references==1,"epoch reset retained HUD allocation identity");
    store.Acquire(UINT32_MAX-1); store.Acquire(UINT32_MAX); store.Acquire(1); store.Acquire(2);
    store.Prune(1);
    Check(!store.Find(UINT32_MAX-1) && store.Find(UINT32_MAX) && store.Find(2),"modular age pruning failed");
    if(argc==2) {
      uint64_t baseline{},candidate{}; Bench<false>(10000,baseline); Bench<true>(10000,candidate);
      baseline=candidate=0;
      std::ofstream out(argv[1]); out<<"{\"scope\":\"CPU frame bookkeeping only; not game FPS\",\"frames_per_run\":100000,\"frame_bytes\":"<<sizeof(Frame)<<",\"map_ms\":[";
      double a[3]{},b[3]{};
      for(unsigned i=0;i<3;++i) {
        if(i%2) { b[i]=Bench<true>(100000,candidate); a[i]=Bench<false>(100000,baseline); }
        else { a[i]=Bench<false>(100000,baseline); b[i]=Bench<true>(100000,candidate); }
      }
      Check(baseline==candidate,"benchmark state sequences differ");
      out<<a[0]<<','<<a[1]<<','<<a[2]<<"],\"fixed_ms\":["<<b[0]<<','<<b[1]<<','<<b[2]<<"],\"map_new_nodes_per_run\":100000,\"fixed_heap_nodes_per_run\":0}\n";
    }
    std::cout<<"PASS: bounded capacity, late/future frames, reset, reused state and wrap-safe pruning\n"; return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
