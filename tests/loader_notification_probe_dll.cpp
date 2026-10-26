// SPDX-License-Identifier: MIT
// Probe DLL for loader_notification_probe (plan 5.3). Its DllMain copies an
// architecture constant, as a DLSS-G snippet reads its gate, so the probe can
// tell whether a patch made in the load notification was visible to it.
#include <windows.h>

extern "C" {
// Placed in its own read-only section so the probe can find and patch it.
#pragma section(".gate", read)
__declspec(allocate(".gate")) __declspec(dllexport) extern const volatile unsigned int kProbeArchitecture = 0x1b0;
// Its own writable section, so the probe reads exactly this state.
#pragma section(".probe", read, write)
struct ProbeState { volatile unsigned int attached; volatile unsigned int seen; };
__declspec(allocate(".probe")) __declspec(dllexport) ProbeState g_probe_state = {0, 0};
// An absolute address in code needs a base relocation, so the probe can tell
// whether relocations are already applied when the notification runs.
__declspec(dllexport) __declspec(noinline) const void* ProbeStateAddress() { return &g_probe_state; }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_probe_state.seen = kProbeArchitecture;
    g_probe_state.attached = 1;
  }
  return TRUE;
}
