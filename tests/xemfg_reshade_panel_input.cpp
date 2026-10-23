// SPDX-License-Identifier: MIT
// Production addon UI in an ordinary ReShade runtime with XeMFG unavailable.
// Never sends input unless this executable's own test window is foreground.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <deps/imgui/imgui.h>
#include <reshade.hpp>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static reshade::api::effect_runtime* runtime{};
static unsigned opens{},closes{},commands{},guiFrames{};
static bool selectPanel{};
static void Require(bool b,const char* why){if(!b) throw std::runtime_error(why);}
static void Check(HRESULT h,const char* why){Require(SUCCEEDED(h),why);}
static void Init(reshade::api::effect_runtime* r){runtime=r;}
static bool Change(reshade::api::effect_runtime*,bool open,reshade::api::input_source){(open?opens:closes)++;return false;}
static void Gui(reshade::api::effect_runtime*){
  ++guiFrames;
  if(selectPanel) ImGui::SetWindowFocus("MFG Unlock");
}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
  if(m==WM_KEYDOWN && w==VK_F23) ++commands;
  return DefWindowProcW(h,m,w,l);
}
int main(int argc,char** argv){try{
  const bool manual=argc>1;
  const auto reshade=LoadLibraryW(L".\\dxgi.dll");Require(reshade,"ReShade load");
  Require(reshade::register_addon(GetModuleHandleW(nullptr),reshade),"observer registration");
  reshade::register_event<reshade::addon_event::init_effect_runtime>(&Init);
  reshade::register_event<reshade::addon_event::reshade_open_overlay>(&Change);
  reshade::register_event<reshade::addon_event::reshade_overlay>(&Gui);
  auto create=reinterpret_cast<HRESULT(WINAPI*)(REFIID,void**)>(GetProcAddress(reshade,"CreateDXGIFactory1"));
  ComPtr<IDXGIFactory4> factory; Check(create(IID_PPV_ARGS(&factory)),"factory");
  ComPtr<ID3D12Device> device;
  for(UINT n=0;!device;++n){ComPtr<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(n,&adapter)==DXGI_ERROR_NOT_FOUND)break;
    DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(d.VendorId==0x10de)D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device));}
  Require(device!=nullptr,"NVIDIA device");
  auto addon=GetModuleHandleW(L"renodx-mfgunlock-native.addon64");Require(addon,"addon load");
  auto draws=reinterpret_cast<uint64_t(*)()>(GetProcAddress(addon,"MFGAXePanelDraws"));Require(draws,"panel counter");
  ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
  WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.lpszClassName=L"XeMFGRealPanelInput";wc.hInstance=GetModuleHandleW(nullptr);RegisterClassW(&wc);
  HWND window=CreateWindowW(wc.lpszClassName,L"MFG Unlock isolated UI test",WS_OVERLAPPEDWINDOW,80,80,1100,900,nullptr,nullptr,wc.hInstance,nullptr);Require(window,"window");
  ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);
  DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=1050;desc.Height=800;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=3;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ComPtr<IDXGISwapChain1> base;Check(factory->CreateSwapChainForHwnd(queue.Get(),window,&desc,nullptr,nullptr,&base),"swapchain");
  ComPtr<IDXGISwapChain3> chain;Check(base.As(&chain),"chain");base.Reset();Require(runtime,"ordinary runtime not created");
  ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
  HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};
  auto pump=[](){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);};
  auto tick=[&](){pump();Check(chain->Present(0,0),"present");queue->Signal(fence.Get(),++serial);if(fence->GetCompletedValue()<serial){fence->SetEventOnCompletion(serial,event);Require(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"GPU timeout");}Sleep(5);};
  ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);
  for(unsigned i=0;i<20;++i)tick();
  if(GetForegroundWindow()!=window){
    DWORD pid{};const auto fg=GetForegroundWindow();GetWindowThreadProcessId(fg,&pid);
    std::cerr<<"foreground="<<fg<<" pid="<<pid<<" own="<<window<<" pid="<<GetCurrentProcessId()
        <<" visible="<<IsWindowVisible(window)<<" enabled="<<IsWindowEnabled(window)<<" iconic="<<IsIconic(window)<<std::endl;
  }
  Require(GetForegroundWindow()==window,"test window not foreground; no input sent");
  auto press=[&](WORD key){
    Require(GetForegroundWindow()==window,"test lost focus; no input sent");
    INPUT input{};input.type=INPUT_KEYBOARD;input.ki.wVk=key;Require(SendInput(1,&input,sizeof(input))==1,"key down");
    for(unsigned i=0;i<8;++i)tick();input.ki.dwFlags=KEYEVENTF_KEYUP;Require(SendInput(1,&input,sizeof(input))==1,"key up");for(unsigned i=0;i<8;++i)tick();
  };
  auto overlayStatus=reinterpret_cast<bool(*)(uint64_t*,uint32_t)>(GetProcAddress(addon,"MFGAXeOverlayStatus"));
  uint64_t counts[4]{};Require(overlayStatus && overlayStatus(counts,sizeof(counts)),"overlay status ABI");
  Require(manual?counts[0]>counts[1]:counts[0]==0,"unexpected output runtime path");
  selectPanel=true;
  for(unsigned cycle=0;cycle<8;++cycle){
    press(VK_F23);auto before=commands;Require(before==cycle+1,"closed overlay retained commands");
    press(VK_HOME);Require(opens==cycle+1 && closes==cycle,"Home failed to open");
    Require(draws()>0,"production MFG tab was not drawn");
    press(VK_F23);Require(commands==before,"overlay did not capture commands");
    press(VK_TAB); // Navigate a real production control before closing.
    press(VK_HOME);Require(opens==cycle+1 && closes==cycle+1,"Home failed to close");
  }
  press(VK_F23);Require(commands==9,"last close retained commands");
  std::cout<<"PASS: production panel draws="<<draws()<<" GUI frames="<<guiFrames<<" opens="<<opens<<" closes="<<closes<<" commands="<<commands<<"; manual_runtime="<<manual<<"; ReShade input capture/release\n";
  chain.Reset();CloseHandle(event);DestroyWindow(window);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
