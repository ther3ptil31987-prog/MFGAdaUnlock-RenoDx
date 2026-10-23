// SPDX-License-Identifier: MIT
// Synthetic implementation of the published ABI; contains no REFramework code.
#include <windows.h>
#include <unknwn.h>
#include <atomic>
#include <cstdint>
namespace {
std::atomic<uint32_t> response{1},calls{},callbacks{};
std::atomic<void(*)()> callback{};
DWORD WINAPI Reenter(void*) {if(auto f=callback.load())f();++callbacks;return 0;}
}
extern "C" __declspec(dllexport) void REFTestConfigure(uint32_t value,void(*fn)()) {response=value;callback=fn;}
extern "C" __declspec(dllexport) uint32_t REFTestCalls() {return calls;}
extern "C" __declspec(dllexport) uint32_t REFTestCallbacks() {return callbacks;}
#ifndef REF_LEGACY
extern "C" __declspec(dllexport) uint32_t WINAPI REFramework_XeFG_PreRetireSwapchainV1(IUnknown* proxy,void* context,HWND hwnd) {
  ++calls;
  if(!proxy || !context || !hwnd)return 2;
  IUnknown* keep{};if(FAILED(proxy->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&keep))))return 2;
  keep->Release();
  if(callback) {
    auto thread=CreateThread(nullptr,0,&Reenter,nullptr,0,nullptr);if(!thread)return 2;
    const auto wait=WaitForSingleObject(thread,2000);CloseHandle(thread);if(wait!=WAIT_OBJECT_0)return 2;
  }
  const auto value=response.load();
  if(value==0xfffffffeu)RaiseException(0xe0424242,0,0,nullptr);
  return value;
}
#endif
