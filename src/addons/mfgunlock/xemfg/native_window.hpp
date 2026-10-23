// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace mfgunlock::xemfg::native {
enum class DisplayMode : uint32_t { Unknown, Windowed, NativeBorderless, ManagedBorderless, Exclusive };
struct FullscreenObservation { bool known{},exclusive{}; };
template<class Chain> FullscreenObservation ReadFullscreen(Chain* chain) {
  BOOL exclusive=FALSE;
  // Sequence the query before reading its output. Function arguments may be
  // evaluated right-to-left: Observe(SUCCEEDED(Query(&x)),x) reads stale x.
  const auto result=chain->GetFullscreenState(&exclusive,nullptr);
  return {SUCCEEDED(result),SUCCEEDED(result) && exclusive!=FALSE};
}
struct WindowStatus {
  bool available{}, requested{}, pending{}, borderless{}, coversMonitor{}, managed{};
  uint32_t error{};
  DisplayMode mode{DisplayMode::Unknown};
};
// Window changes run on the HWND's owning thread, through its message pump.
// Neither the overlay nor Intel's presentation worker calls SetWindowPos.
class WindowControl : public std::enable_shared_from_this<WindowControl> {
public:
  static std::shared_ptr<WindowControl> Bind(HWND hwnd, bool force, bool dxgiKnown=false, bool exclusive=false);
  static LRESULT CALLBACK MessageHook(int code, WPARAM w, LPARAM l);
  void Request(bool force,bool explicitRequest=true);
  void ObserveFullscreen(bool known,bool exclusive);
  void Retire();
  WindowStatus Snapshot();
private:
  HWND hwnd{};
  std::atomic<HHOOK> hook{};
  DWORD thread{};
  uintptr_t generation{};
  std::mutex lock;
  bool requested{}, pending{}, managed{}, retiring{}, applying{};
  bool dxgiKnown{},exclusive{};
  bool relinquished{};
  DWORD error{};
  LONG_PTR originalStyle{}, originalEx{}, appliedStyle{}, appliedEx{};
  WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
  RECT appliedRect{};
  void Apply();
};
}
