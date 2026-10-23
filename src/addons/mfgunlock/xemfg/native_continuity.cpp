// SPDX-License-Identifier: MIT
// Rebind a retained native output to the NEW Intel presentation queue. No
// third-party reference is released. Fail rather than guess ownership/layout.
#include "native_continuity.hpp"
#include "native_profiles.hpp"
#include "vendor/overlay_native.h"
#include "vendor/port/port_support.h"
#include <atomic>
#include <map>
#include <mutex>
#include <memory>
#include <vector>
namespace mfgunlock::xemfg::native {
using Microsoft::WRL::ComPtr;
namespace {
std::atomic<int> mode{-1};
constexpr auto property=L"MFGAdaUnlock.XeMFG.RetiredNative.1";
struct Entry {HWND window{};bool ready{};ComPtr<IDXGISwapChain4> chain;ComPtr<ID3D12CommandQueue> queue;};
struct Registry {std::mutex lock;std::map<HWND,std::shared_ptr<Entry>> entries;};
Registry& Cache(){static auto* value=new Registry;return *value;}
void RemoveCookie(const std::shared_ptr<Entry>& entry) {
  if(GetPropW(entry->window,property)==entry.get()) RemovePropW(entry->window,property);
}
void Put(std::shared_ptr<Entry> entry) {
  if(!IsWindow(entry->window))return;
  std::vector<std::shared_ptr<Entry>> dropped;
  {auto& cache=Cache();std::lock_guard guard(cache.lock);
    if(auto old=cache.entries.find(entry->window);old!=cache.entries.end()) {RemoveCookie(old->second);dropped.push_back(std::move(old->second));cache.entries.erase(old);}
    while(cache.entries.size()>=2) {auto old=cache.entries.begin();RemoveCookie(old->second);
      if(!old->second->ready)new std::shared_ptr<Entry>(std::move(old->second));
      else dropped.push_back(std::move(old->second));cache.entries.erase(old);
    }
    if(SetPropW(entry->window,property,entry.get()))cache.entries.emplace(entry->window,std::move(entry));
  } // Native COM releases may call an overlay; never hold registryLock across them.
}
}
void ConfigureNativeContinuity(int value) noexcept {mode=value;}
bool NativeContinuityEnabled() noexcept {
  // Default: profiles.json (retain_native_output, e.g. while the Steam overlay
  // holds references); the INI value 0/1 overrides it.
  return mode==1 || (mode<0 && CurrentPolicy().retainNativeOutput.value_or(false));
}
void RetainNativeOutput(ComPtr<IDXGISwapChain4> chain,ID3D12CommandQueue* queue) {
  HWND window{};DWORD pid{};
  if(!chain || !queue || FAILED(chain->GetHwnd(&window)) || !GetWindowThreadProcessId(window,&pid) || pid!=GetCurrentProcessId())return;
  auto entry=std::make_shared<Entry>();entry->window=window;entry->chain=std::move(chain);entry->queue=queue;Put(std::move(entry));
  xess_mod::WriteLog("native_output_retained","\"hwnd\":"+std::to_string(uintptr_t(window)));
}
void ClearRetainedNative(HWND window) {
  std::vector<std::shared_ptr<Entry>> dropped;
  {auto& cache=Cache();std::lock_guard guard(cache.lock);
    for(auto it=cache.entries.begin();it!=cache.entries.end();) {
      if(window && it->first!=window){++it;continue;}
      RemoveCookie(it->second);
      if(!it->second->ready)new std::shared_ptr<Entry>(std::move(it->second));
      else dropped.push_back(std::move(it->second));it=cache.entries.erase(it);
    }
  }
}
void ConfirmNativeRetirement(HWND window,bool success) {
  auto& cache=Cache();std::lock_guard guard(cache.lock);auto it=cache.entries.find(window);
  if(it==cache.entries.end())return;
  if(success)it->second->ready=true;
  else {RemoveCookie(it->second);new std::shared_ptr<Entry>(std::move(it->second));cache.entries.erase(it);}
}
HRESULT ReuseNativeOutput(IUnknown* source,HWND window,const DXGI_SWAP_CHAIN_DESC1* requested,IDXGISwapChain1** out) {
  if(!NativeContinuityEnabled())return DXGI_ERROR_NOT_FOUND;
  std::shared_ptr<Entry> entry;
  {auto& cache=Cache();std::lock_guard guard(cache.lock);auto it=cache.entries.find(window);
    if(it==cache.entries.end())return DXGI_ERROR_NOT_FOUND;
    if(!it->second->ready)return E_FAIL;
    entry=std::move(it->second);cache.entries.erase(it);
  }
  if(GetPropW(window,property)!=entry.get())return DXGI_ERROR_NOT_FOUND;
  RemoveCookie(entry);
  entry->chain->AddRef();const auto remaining=entry->chain->Release(); // own balanced probe only
  if(remaining==1)return DXGI_ERROR_NOT_FOUND;
  ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Device> before,after,nativeBefore,nativeAfter;
  DXGI_SWAP_CHAIN_DESC1 old{};HRESULT result=DXGI_ERROR_INVALID_CALL;bool attemptedResize{};
  if(requested && out && source && SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&queue)))
      && queue->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT
      && SUCCEEDED(entry->queue->GetDevice(IID_PPV_ARGS(&before))) && SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&after)))
      && single_overlay::native::Unwrap(before.Get(),nativeBefore) && single_overlay::native::Unwrap(after.Get(),nativeAfter)
      && nativeBefore.Get()==nativeAfter.Get() && SUCCEEDED(entry->chain->GetDesc1(&old))
      && old.SwapEffect==requested->SwapEffect && old.Scaling==requested->Scaling
      // An HWND chain reports IGNORE for an opaque UNSPECIFIED request.
      // Neither implies premultiplied/straight composition. Preserve every
      // other immutable field, and never reuse a composition alpha mode.
      && (old.AlphaMode==requested->AlphaMode || (old.AlphaMode==DXGI_ALPHA_MODE_IGNORE && requested->AlphaMode==DXGI_ALPHA_MODE_UNSPECIFIED))
      && old.SampleDesc.Count==requested->SampleDesc.Count && old.SampleDesc.Quality==requested->SampleDesc.Quality
      && old.Flags==requested->Flags && old.BufferUsage==requested->BufferUsage) {
    const auto count=requested->BufferCount?requested->BufferCount:old.BufferCount;
    const auto mask=queue->GetDesc().NodeMask?queue->GetDesc().NodeMask:1;
    if(count>=2 && count<=16) {
      attemptedResize=true;
      std::vector<UINT> masks(count,mask);std::vector<IUnknown*> queues(count,queue.Get());
      result=entry->chain->ResizeBuffers1(count,requested->Width,requested->Height,requested->Format,requested->Flags,masks.data(),queues.data());
      if(SUCCEEDED(result)) result=entry->chain.CopyTo(out);
    }
  }
  xess_mod::WriteLog("native_output_reused","\"hwnd\":"+std::to_string(uintptr_t(window))
      +",\"result\":"+std::to_string(result)+",\"native_refs\":"+std::to_string(remaining)
      +",\"old_flags\":"+std::to_string(old.Flags)+",\"requested_flags\":"+std::to_string(requested?requested->Flags:0)
      +",\"old_usage\":"+std::to_string(old.BufferUsage)+",\"requested_usage\":"+std::to_string(requested?requested->BufferUsage:0)
      +",\"old_scaling\":"+std::to_string(old.Scaling)+",\"requested_scaling\":"+std::to_string(requested?requested->Scaling:0));
  xess_mod::WriteLog("native_output_reuse_validation","\"resize_attempted\":"+std::string(attemptedResize?"true":"false")
      +",\"same_device\":"+(nativeBefore && nativeBefore.Get()==nativeAfter.Get()?"true":"false")
      +",\"old_effect\":"+std::to_string(old.SwapEffect)+",\"new_effect\":"+std::to_string(requested?requested->SwapEffect:0)
      +",\"old_alpha\":"+std::to_string(old.AlphaMode)+",\"new_alpha\":"+std::to_string(requested?requested->AlphaMode:0));
  if(FAILED(result))Put(std::move(entry));
  return result;
}
}
