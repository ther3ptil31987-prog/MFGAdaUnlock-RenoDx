// SPDX-License-Identifier: MIT
#include "native_window.hpp"
#include <map>
#include <vector>

namespace mfgunlock::xemfg::native {
namespace {
std::mutex registryLock;
std::map<HWND,std::shared_ptr<WindowControl>> windows;
std::atomic<uintptr_t> nextGeneration{1};
UINT ControlMessage() { static const UINT value=RegisterWindowMessageW(L"MFGAdaUnlock.XeMFG.WindowControl.1"); return value; }
constexpr auto property=L"MFGAdaUnlock.XeMFG.WindowOwner.1";
LONG_PTR Style(HWND hwnd,int index) { return DWORD(GetWindowLongPtrW(hwnd,index)); }
bool SetStyle(HWND hwnd,int index,LONG_PTR value) {
  SetLastError(0);
  return SetWindowLongPtrW(hwnd,index,value)!=0 || GetLastError()==0;
}
bool SameRect(const RECT& a,const RECT& b) {
  return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
}
struct PhysicalCoordinates {
  DPI_AWARENESS_CONTEXT old{SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)};
  ~PhysicalCoordinates() { if(old) SetThreadDpiAwarenessContext(old); }
};
bool Geometry(HWND hwnd,bool& borderless,bool& covers) {
  PhysicalCoordinates coordinates;
  MONITORINFO monitor{sizeof(MONITORINFO)}; RECT outer{},client{};
  const auto style=Style(hwnd,GWL_STYLE),ex=Style(hwnd,GWL_EXSTYLE);
  borderless=!(style&(WS_CAPTION|WS_THICKFRAME)) && !(ex&(WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE|WS_EX_DLGMODALFRAME));
  if(!GetWindowRect(hwnd,&outer) || !GetClientRect(hwnd,&client)
      || !GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor)) return false;
  SetLastError(0);
  if(!MapWindowPoints(hwnd,nullptr,reinterpret_cast<POINT*>(&client),2) && GetLastError()) return false;
  covers=SameRect(outer,monitor.rcMonitor) && SameRect(client,monitor.rcMonitor);
  return GetDpiForWindow(hwnd)!=0;
}
}
std::shared_ptr<WindowControl> WindowControl::Bind(HWND hwnd,bool force,bool known,bool fullscreen) {
  DWORD pid{}; const DWORD tid=GetWindowThreadProcessId(hwnd,&pid);
  if(!tid || pid!=GetCurrentProcessId() || !ControlMessage()) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return {}; }
  auto value=std::shared_ptr<WindowControl>(new WindowControl);
  value->hwnd=hwnd; value->thread=tid; value->generation=nextGeneration++;
  value->dxgiKnown=known; value->exclusive=fullscreen;
  HMODULE module{};
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
      reinterpret_cast<LPCWSTR>(&MessageHook),&module)) return {};
  {
    std::lock_guard guard(registryLock);
    // Never replace a still-owned/restoring generation of the same HWND.
    if(auto it=windows.find(hwnd); it!=windows.end()) {
      if(GetPropW(hwnd,property)==it->second.get()) { SetLastError(ERROR_ALREADY_EXISTS); return {}; }
      if(it->second->hook) UnhookWindowsHookEx(it->second->hook);
      windows.erase(it); // The old HWND was destroyed; its property is gone.
    }
    // This thread belongs to our own process (validated above). A local hook
    // needs no DLL-name lookup/injection. Keep the explicit module pin for
    // lifetime, but do not ask User32 to resolve the addon's custom extension.
    value->hook=SetWindowsHookExW(WH_GETMESSAGE,&MessageHook,nullptr,tid);
    if(!value->hook) return {};
    if(!SetPropW(hwnd,property,value.get())) { UnhookWindowsHookEx(value->hook); return {}; }
    windows.emplace(hwnd,value);
  }
  if(force) value->Request(true);
  return value;
}
void WindowControl::Request(bool force,bool explicitRequest) {
  std::lock_guard guard(lock);
  if(retiring || !IsWindow(hwnd)) return;
  if(relinquished && force && !explicitRequest) return;
  if(explicitRequest) relinquished=false;
  requested=force; pending=true; error=0;
  if(!PostMessageW(hwnd,ControlMessage(),generation,0)) { error=GetLastError(); pending=false; }
}
void WindowControl::ObserveFullscreen(bool known,bool fullscreen) {
  std::lock_guard guard(lock);
  dxgiKnown=known; exclusive=fullscreen;
}
WindowStatus WindowControl::Snapshot() {
  std::lock_guard guard(lock);
  WindowStatus status{};
  status.available=hook && IsWindow(hwnd) && GetPropW(hwnd,property)==this; status.requested=requested;
  status.pending=pending; status.managed=managed; status.error=error;
  if(status.available) {
    const bool geometry=Geometry(hwnd,status.borderless,status.coversMonitor);
    if(dxgiKnown && exclusive) status.mode=DisplayMode::Exclusive;
    else if(dxgiKnown && geometry) status.mode=status.borderless && status.coversMonitor?
        (managed?DisplayMode::ManagedBorderless:DisplayMode::NativeBorderless):DisplayMode::Windowed;
  }
  return status;
}
void WindowControl::Apply() {
  // Called ONLY by WH_GETMESSAGE, without the bridge/SDK/registry locks.
  bool force{}, owned{},known{},fullscreen{}; LONG_PTR oldStyle{},oldEx{},ourStyle{},ourEx{};
  WINDOWPLACEMENT saved{sizeof(WINDOWPLACEMENT)}; RECT ourRect{};
  {
    std::lock_guard guard(lock);
    if(!pending || GetCurrentThreadId()!=thread) return;
    force=requested; owned=managed; pending=false; applying=true;
    known=dxgiKnown; fullscreen=exclusive;
    oldStyle=originalStyle; oldEx=originalEx; saved=placement;
    ourStyle=appliedStyle; ourEx=appliedEx; ourRect=appliedRect;
  }
  // Window APIs can synchronously enter the game's WndProc. No mutex is held
  // across them: a resize handler may wait for a rendering thread/overlay.
  DWORD failure{};
  PhysicalCoordinates coordinates;
  if(!IsWindow(hwnd) || GetPropW(hwnd,property)!=this) failure=ERROR_INVALID_WINDOW_HANDLE;
  else if(force) {
    bool borderless{},covers{};
    RECT now{}; GetWindowRect(hwnd,&now);
    if(owned && (Style(hwnd,GWL_STYLE)!=ourStyle || Style(hwnd,GWL_EXSTYLE)!=ourEx || !SameRect(now,ourRect))) {
      // Relinquish on any foreign transition; never overwrite it in a retry.
      failure=ERROR_BUSY; owned=false;
    }
    else if(!known || fullscreen) failure=ERROR_NOT_SUPPORTED;
    else if(!Geometry(hwnd,borderless,covers)) failure=ERROR_INVALID_DATA;
    else if(borderless && covers) {
      // The desired state is already satisfied. In particular, do NOT adopt
      // native styles/placement or emit WM_SIZE / SWP_FRAMECHANGED here.
      std::lock_guard guard(lock); applying=false; managed=owned; error=0; return;
    }
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if(!failure && !GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor)) failure=GetLastError();
    if(!owned && !failure) {
      oldStyle=Style(hwnd,GWL_STYLE); oldEx=Style(hwnd,GWL_EXSTYLE);
      if(!GetWindowPlacement(hwnd,&saved)) failure=GetLastError();
      else if(!(oldStyle&WS_VISIBLE)) saved.showCmd=SW_HIDE;
    }
    const auto style=(Style(hwnd,GWL_STYLE)&~LONG_PTR(WS_OVERLAPPEDWINDOW))|WS_POPUP;
    const auto ex=Style(hwnd,GWL_EXSTYLE)&~LONG_PTR(WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE|WS_EX_DLGMODALFRAME);
    if(!failure && (!SetStyle(hwnd,GWL_STYLE,style) || !SetStyle(hwnd,GWL_EXSTYLE,ex))) failure=GetLastError();
    if(!failure) {
      const auto& r=monitor.rcMonitor;
      if(!SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,
          SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER)) failure=GetLastError();
    }
    if(failure) {
      // Roll back only if this attempt actually changed styles.
      if(oldStyle && !owned && failure!=ERROR_BUSY && failure!=ERROR_NOT_SUPPORTED && failure!=ERROR_INVALID_DATA) {
        SetStyle(hwnd,GWL_STYLE,oldStyle); SetStyle(hwnd,GWL_EXSTYLE,oldEx);
        SetWindowPlacement(hwnd,&saved);
        SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
      }
    } else {
      owned=true; ourStyle=style; ourEx=ex; ourRect=monitor.rcMonitor;
    }
  } else if(owned) {
    RECT now{}; GetWindowRect(hwnd,&now);
    // If the game/another addon took ownership, don't overwrite their change.
    if(!known || fullscreen || Style(hwnd,GWL_STYLE)!=ourStyle || Style(hwnd,GWL_EXSTYLE)!=ourEx
        || !SameRect(now,ourRect)) { failure=ERROR_BUSY; owned=false; }
    else if(!SetStyle(hwnd,GWL_STYLE,oldStyle) || !SetStyle(hwnd,GWL_EXSTYLE,oldEx)
        || !SetWindowPlacement(hwnd,&saved)
        || !SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED)) {
      failure=GetLastError();
    } else owned=false;
  }
  std::lock_guard guard(lock);
  error=failure; managed=owned; originalStyle=oldStyle; originalEx=oldEx; placement=saved;
  if(failure==ERROR_BUSY) relinquished=true;
  appliedStyle=ourStyle; appliedEx=ourEx; appliedRect=ourRect;
  applying=false;
}
void WindowControl::Retire() {
  {
    std::lock_guard guard(lock);
    retiring=true; requested=false; pending=managed || applying; error=0;
    if(pending && IsWindow(hwnd) && PostMessageW(hwnd,ControlMessage(),generation,0)) return;
    // A dead window has no styles to restore. Keep a live pending controller
    // if posting failed; never call window-changing APIs from this thread.
    if(pending && IsWindow(hwnd)) { error=GetLastError(); return; }
  }
  std::lock_guard registry(registryLock);
  if(hook) UnhookWindowsHookEx(hook); hook=nullptr;
  if(GetPropW(hwnd,property)==this) RemovePropW(hwnd,property);
  if(auto it=windows.find(hwnd); it!=windows.end() && it->second.get()==this) windows.erase(it);
}
LRESULT CALLBACK WindowControl::MessageHook(int code,WPARAM w,LPARAM l) {
  if(code>=0 && w==PM_REMOVE) {
    auto* message=reinterpret_cast<MSG*>(l);
    if(message && message->message==ControlMessage()) {
      std::shared_ptr<WindowControl> value;
      { std::lock_guard guard(registryLock);
        if(auto it=windows.find(message->hwnd); it!=windows.end()) value=it->second;
      }
      if(value && value->generation==message->wParam) {
        // Consume only our own generation's private request.
        message->message=WM_NULL;
        value->Apply();
        bool retiring{}; { std::lock_guard guard(value->lock); retiring=value->retiring && !value->pending; }
        if(retiring) {
          std::lock_guard guard(registryLock);
          if(value->hook) UnhookWindowsHookEx(value->hook); value->hook=nullptr;
          if(GetPropW(value->hwnd,property)==value.get()) RemovePropW(value->hwnd,property);
          if(auto it=windows.find(value->hwnd); it!=windows.end() && it->second==value) windows.erase(it);
        }
      }
    }
  }
  return CallNextHookEx(nullptr,code,w,l);
}
}
