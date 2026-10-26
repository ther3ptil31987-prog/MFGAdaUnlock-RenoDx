// SPDX-License-Identifier: MIT
// Plan 5.3: what LdrRegisterDllNotification guarantees for the DLSS-G load
// trigger, measured instead of assumed.
//  1. It fires for a module loaded through ntdll!LdrLoadDll directly, which
//     bypasses the kernel32 LoadLibraryW/ExW hooks of loadhook.hpp.
//  2. It fires before the module's DllMain.
//  3. A read-only byte patched in the callback is what DllMain then reads.
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "../src/addons/mfgunlock/module_session.hpp"

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition     \
                << '\n';                                                        \
      std::exit(1);                                                             \
    }                                                                           \
  } while (false)

namespace {
struct UnicodeString { USHORT Length, MaximumLength; PWSTR Buffer; };
struct NotificationData { ULONG Flags; const UnicodeString* FullDllName; const UnicodeString* BaseDllName; PVOID DllBase; ULONG SizeOfImage; };
using Callback = VOID(CALLBACK*)(ULONG, const NotificationData*, PVOID);
using Register = LONG(NTAPI*)(ULONG, Callback, PVOID, PVOID*);
using Unregister = LONG(NTAPI*)(PVOID);
using LdrLoadDll = LONG(NTAPI*)(PWSTR, PULONG, const UnicodeString*, PVOID*);
constexpr ULONG kLoaded = 1;

bool notified = false, attachedBeforeNotification = false, patched = false, inspectedInCallback = false;
mfgunlock::modulesession::Identity identityInCallback;

const IMAGE_SECTION_HEADER* FindSection(BYTE* base, const char* name) {
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    if (strncmp(reinterpret_cast<const char*>(section->Name), name, IMAGE_SIZEOF_SHORT_NAME) == 0) return section;
  return nullptr;
}

VOID CALLBACK Notify(ULONG reason, const NotificationData* data, PVOID) {
  if (reason != kLoaded || !data || !data->BaseDllName || !data->BaseDllName->Buffer) return;
  const std::wstring name(data->BaseDllName->Buffer, data->BaseDllName->Length / sizeof(wchar_t));
  if (_wcsicmp(name.c_str(), L"loader_notification_probe_dll.dll") != 0) return;
  notified = true;
  auto* base = static_cast<BYTE*>(data->DllBase);
  // 4. The code identity the patch profiles use (relocations undone) is the
  //    same here as after the load, so a session acquired in the callback
  //    does not cache a wrong identity.
  inspectedInCallback = mfgunlock::modulesession::Inspect(static_cast<HMODULE>(data->DllBase), identityInCallback);
  // Exports cannot be resolved with GetProcAddress inside the callback; read
  // the attach flag and patch the gate through the section layout instead.
  const auto* state = FindSection(base, ".probe");
  const auto* gate = FindSection(base, ".gate");
  // ProbeState::attached (first field) is set by DllMain.
  if (state) attachedBeforeNotification = *reinterpret_cast<const unsigned int*>(base + state->VirtualAddress) != 0;
  if (gate) {
    auto* value = reinterpret_cast<unsigned int*>(base + gate->VirtualAddress);
    DWORD old = 0;
    if (*value == 0x1b0 && VirtualProtect(value, sizeof(*value), PAGE_READWRITE, &old)) {
      *value = 0x190;
      VirtualProtect(value, sizeof(*value), old, &old);
      patched = true;
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const std::wstring path = std::filesystem::absolute(argv[1]).wstring();
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto reg = reinterpret_cast<Register>(GetProcAddress(ntdll, "LdrRegisterDllNotification"));
  const auto unreg = reinterpret_cast<Unregister>(GetProcAddress(ntdll, "LdrUnregisterDllNotification"));
  const auto load = reinterpret_cast<LdrLoadDll>(GetProcAddress(ntdll, "LdrLoadDll"));
  CHECK(reg && unreg && load);
  PVOID cookie = nullptr;
  CHECK(reg(0, &Notify, nullptr, &cookie) >= 0);

  // ntdll directly, as a loader that does not go through kernel32 would.
  UnicodeString name{USHORT(path.size() * sizeof(wchar_t)), USHORT(path.size() * sizeof(wchar_t)), const_cast<PWSTR>(path.c_str())};
  PVOID handle = nullptr;
  CHECK(load(nullptr, nullptr, &name, &handle) >= 0 && handle);
  unreg(cookie);

  const auto module = static_cast<HMODULE>(handle);
  const auto* state = reinterpret_cast<const unsigned int*>(GetProcAddress(module, "g_probe_state"));
  CHECK(state && state[0] == 1);
  const auto* seen = state + 1;
  CHECK(notified);                       // 1. LdrLoadDll is seen
  CHECK(!attachedBeforeNotification);    // 2. before DllMain
  CHECK(patched && *seen == 0x190);      // 3. DllMain read the patched gate
  mfgunlock::modulesession::Identity identityAfterLoad;
  CHECK(mfgunlock::modulesession::Inspect(module, identityAfterLoad));
  CHECK(inspectedInCallback && identityInCallback == identityAfterLoad);  // 4. same identity
  // The relocated absolute address is the real one (relocations were applied).
  const auto address = reinterpret_cast<const void* (*)()>(GetProcAddress(module, "ProbeStateAddress"));
  CHECK(address && address() == static_cast<const void*>(state));
  std::cout << "PASS: notification for LdrLoadDll, before DllMain, patch visible to DllMain, same code identity\n";
  return 0;
}
