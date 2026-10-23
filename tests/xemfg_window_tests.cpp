// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/native_window.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace mfgunlock::xemfg::native;
std::weak_ptr<WindowControl> resizeController;
bool retireDuringResize{}, reentryObserved{};
unsigned windowMutations{};
LRESULT CALLBACK WndProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
  // WM_WINDOWPOSCHANGED can also arrive asynchronously from the desktop.
  // Count synchronous style writes and separately check final placement.
  if(message==WM_STYLECHANGED) ++windowMutations;
  if(message==WM_SIZE && retireDuringResize) {
    retireDuringResize=false;
    if(auto controller=resizeController.lock()) {
      reentryObserved=controller->Snapshot().available;
      controller->Retire();
    }
  }
  return DefWindowProcW(hwnd,message,w,l);
}
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void Pump() { MSG msg{}; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&msg); }
int main() {
  try {
    struct Query {
      HRESULT result{S_OK}; BOOL exclusive{TRUE}; unsigned calls{};
      HRESULT GetFullscreenState(BOOL* value,void*) { ++calls;*value=exclusive;return result; }
    } query;
    auto observed=ReadFullscreen(&query);
    Require(observed.known && observed.exclusive && query.calls==1,"fullscreen output read before query");
    query.exclusive=FALSE;observed=ReadFullscreen(&query);
    Require(observed.known && !observed.exclusive && query.calls==2,"windowed query not observed");
    query.result=E_FAIL;query.exclusive=TRUE;observed=ReadFullscreen(&query);
    Require(!observed.known && !observed.exclusive && query.calls==3,"failed query trusted fullscreen output");
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"MFGWindowControlTest";
    RegisterClassW(&wc);
    const auto hwnd=CreateWindowW(wc.lpszClassName,L"window control",WS_OVERLAPPEDWINDOW,45,60,640,360,nullptr,nullptr,wc.hInstance,nullptr);
    Require(hwnd,"create window");
    const DWORD originalStyle=DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)); RECT before{}; GetWindowRect(hwnd,&before);
    auto controller=WindowControl::Bind(hwnd,false,true); Require(bool(controller),"bind hook");
    // Request from another thread. It must remain pending and unchanged until
    // the owning thread dispatches its queued message.
    std::thread render([&]{controller->Request(true);}); render.join();
    Require(controller->Snapshot().pending && DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE))==originalStyle,"request changed window off-thread");
    Pump(); auto state=controller->Snapshot();
    Require(state.borderless && state.coversMonitor && state.managed && !state.pending && !state.error,"borderless not applied");
    const auto appliedMutations=windowMutations;
    controller->Request(true); Pump(); controller->Request(true); Pump();
    Require(windowMutations==appliedMutations,"repeated borderless request mutated a compatible window");
    controller->Request(false); Pump(); state=controller->Snapshot(); RECT restored{}; GetWindowRect(hwnd,&restored);
    std::cout<<"restore managed="<<state.managed<<" error="<<state.error<<" style="<<std::hex
        <<DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE))<<" original="<<originalStyle<<std::dec
        <<" rect="<<restored.left<<','<<restored.top<<','<<restored.right<<','<<restored.bottom
        <<" original="<<before.left<<','<<before.top<<','<<before.right<<','<<before.bottom<<'\n';
    Require(!state.managed && !state.error && DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE))==originalStyle
        && EqualRect(&before,&restored),"original placement/style not restored");
    controller->Request(true); Pump();
    // Preserve a subsequent change made by the game/another addon.
    SetWindowLongPtrW(hwnd,GWL_STYLE,GetWindowLongPtrW(hwnd,GWL_STYLE)|WS_THICKFRAME);
    controller->Request(false); Pump();
    Require(controller->Snapshot().error==ERROR_BUSY && (GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_THICKFRAME),"foreign style overwritten");
    const auto foreignMutations=windowMutations;
    RECT foreignRect{}; GetWindowRect(hwnd,&foreignRect);
    controller->Request(true,false); Pump();
    RECT afterForeign{}; GetWindowRect(hwnd,&afterForeign); const auto foreign=controller->Snapshot();
    Require(windowMutations==foreignMutations && EqualRect(&foreignRect,&afterForeign)
        && !foreign.managed && !foreign.pending && foreign.error==ERROR_BUSY,"automatic revalidation overrode foreign ownership");
    controller->Retire();
    auto second=WindowControl::Bind(hwnd,true,true); Require(bool(second),"retired generation still owns HWND"); Pump();
    second->Retire(); Pump(); Require(!second->Snapshot().available,"retirement did not remove hook");
    auto reentrant=WindowControl::Bind(hwnd,false,true); Require(bool(reentrant),"reentrant bind");
    resizeController=reentrant; retireDuringResize=true;
    reentrant->Request(true); Pump();
    Require(reentryObserved && !reentrant->Snapshot().available,"resize reentry/retirement did not complete");
    // A destroyed window must not be reported as active or touched later.
    auto dead=WindowControl::Bind(hwnd,true); Require(bool(dead),"bind dead-window test");
    DestroyWindow(hwnd); dead->Retire(); Require(!dead->Snapshot().available,"destroyed HWND reported active");
    MONITORINFO monitor{sizeof(MONITORINFO)}; GetMonitorInfoW(MonitorFromPoint(POINT{},MONITOR_DEFAULTTOPRIMARY),&monitor);
    const auto& r=monitor.rcMonitor;
    const auto native=CreateWindowW(wc.lpszClassName,L"native borderless",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,wc.hInstance,nullptr);
    auto nativeController=WindowControl::Bind(native,false,true);
    Require(nativeController && nativeController->Snapshot().mode==DisplayMode::NativeBorderless,"native borderless not detected");
    const auto nativeMutations=windowMutations;
    nativeController->Request(true); Pump(); nativeController->Request(false); Pump();
    Require(windowMutations==nativeMutations && !nativeController->Snapshot().managed,"native borderless adopted or modified");
    nativeController->ObserveFullscreen(true,true); nativeController->Request(true); Pump();
    Require(nativeController->Snapshot().mode==DisplayMode::Exclusive && nativeController->Snapshot().error==ERROR_NOT_SUPPORTED
        && windowMutations==nativeMutations,"exclusive fullscreen changed from owner message");
    nativeController->ObserveFullscreen(false,false); nativeController->Request(true); Pump();
    Require(nativeController->Snapshot().mode==DisplayMode::Unknown && windowMutations==nativeMutations,"unknown DXGI state modified");
    nativeController->Retire(); DestroyWindow(native);
    std::cout<<"PASS: owner-thread borderless, pending state, placement restore, foreign ownership and retirement\n";
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
