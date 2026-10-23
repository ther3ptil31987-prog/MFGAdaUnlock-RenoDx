// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/native_reframework.hpp"
#include <iostream>
#include <stdexcept>
using namespace mfgunlock::xemfg::native::reframework;
void Check(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
BOOL WINAPI FailedEnumeration(HANDLE,HMODULE*,DWORD,LPDWORD) {SetLastError(ERROR_PARTIAL_COPY);return FALSE;}
struct TestProxy: IUnknown {
  ULONG references{1};
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    *out=nullptr;if(iid!=IID_IUnknown)return E_NOINTERFACE;*out=this;AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
  ULONG STDMETHODCALLTYPE Release() override {return --references;}
};
int wmain(int argc,wchar_t** argv) {
  try {
    Check(argc==3,"expected V1 and legacy fixtures");
    auto peer=Resolve();Check(!peer->info.present && Safe(Prepare(peer,nullptr,nullptr,nullptr)),"absent peer changed legacy behavior");peer.reset();
    auto old=LoadLibraryW(argv[2]);Check(old!=nullptr,"legacy fixture");
    peer=Resolve();Check(peer->info.present && !peer->info.v1 && !peer->info.knownMonitorIssue,"unknown legacy mislabeled");
    Check(Safe(Prepare(peer,nullptr,nullptr,nullptr)),"missing export blocked legacy path");
    FreeLibrary(old);Check(GetModuleHandleW(argv[2])!=nullptr,"peer did not hold module lease");peer.reset();
    Check(GetModuleHandleW(argv[2])==nullptr,"peer permanently pinned foreign module");
    auto module=LoadLibraryW(argv[1]);Check(module!=nullptr,"V1 fixture");
    auto configure=reinterpret_cast<void(*)(uint32_t,void(*)())>(GetProcAddress(module,"REFTestConfigure"));
    auto count=reinterpret_cast<uint32_t(*)()>(GetProcAddress(module,"REFTestCalls"));
    TestProxy proxy;auto* context=reinterpret_cast<void*>(42);auto hwnd=reinterpret_cast<HWND>(43);
    peer=Resolve();Check(peer->info.present && peer->info.v1 && !peer->info.sha256.empty(),"capability/identity discovery");
    Check(Prepare(peer,nullptr,context,hwnd)==Result::Blocked && count()==0,"invalid input called peer");
    for(auto value:{0u,1u,2u,99u,0xfffffffeu}) {
      configure(value,nullptr);peer=Resolve(peer);
      const auto expected=value==0?Result::SafeNotTracked:value==1?Result::SafeDetached:value==2?Result::Blocked:value==99?Result::InvalidResult:Result::Exception;
      Check(Prepare(peer,&proxy,context,hwnd)==expected,"ABI return/exception classification");
      Check(Safe(expected)==(value<=1),"unknown/failed result authorized release");
      Check(proxy.references==1,"public proxy not alive through callback or foreign reference stolen");
      Check(Snapshot().result==expected,"cached UI result");
    }
    Check(count()==5,"callback repeated or skipped");
    old=LoadLibraryW(argv[2]);Check(old!=nullptr,"second module");
    auto ambiguous=Resolve();Check(ambiguous->info.result==Result::Ambiguous && !Safe(Prepare(ambiguous,&proxy,context,hwnd)) && count()==5,"multiple peers not refused");
    FreeLibrary(old);ambiguous.reset();
    auto failed=Resolve(peer,&FailedEnumeration);
    Check(failed==peer && failed->info.result==Result::DiscoveryFailed && !Safe(Prepare(failed,&proxy,context,hwnd)) && count()==5,"failed discovery discarded lease or called stale callback");
    failed.reset();
    FreeLibrary(module);Check(GetModuleHandleW(argv[1])!=nullptr,"V1 lease lost before retirement");peer.reset();
    Check(GetModuleHandleW(argv[1])==nullptr,"V1 lease leak");
    SetKnownMonitorIssues({"70a2a3c24030d8bc3695554e7f929fd1b0b4b757fb5ba2c9d34f42d7155005ac"}); // from profiles.json
    Check(KnownMonitorIssue("70a2a3c24030d8bc3695554e7f929fd1b0b4b757fb5ba2c9d34f42d7155005ac") && !KnownMonitorIssue("unknown"),"warning identity overgeneralized");
    std::cout<<"PASS: absent/legacy/V1 discovery, exact ABI results, exceptions, ambiguity, discovery failure, proxy ownership, module unload and bounded warning identity\n";
    return 0;
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
