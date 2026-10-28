// SPDX-License-Identifier: MIT
// Streamline DLSS-G plugin patches (plan 6.3): the legacy software-flip
// (flip metering) compatibility patch and the plugin's own frame-count
// ceiling, applied when sl.dlss_g is mapped and restored on unload.
//
// A fragment of addon.cpp, included inside its anonymous namespace: it uses
// the configuration and state declared there and is not a standalone header.
#ifndef MFGUNLOCK_ADDON_FRAGMENTS
#error "addon_streamline.inl is included only by addon.cpp"
#endif

// ------------------------------------------------- flip metering (sl.dlss_g)
//
// Some older integrations can freeze at 3x/4x after the capability gate is
// opened because their selected flip-metering path never completes on Ada.
// Current Streamline providers normally pace correctly without any mutation,
// so this compatibility path is opt-in rather than part of the unlock.
//
// Streamline already ships the fallback. sl.dlss_g/ngx.cpp logs
// "FG1 DLL has been detected: forcing flip-metering off." and writes a flag on
// the DLSS-G context, dropping it onto the software RSYNC pacer in rsync.cpp.
// That is the path Ada needs.
//
// Two things make this impossible to hardcode, both learned the hard way:
//
//  1. The plugin in bin/x64 is usually NOT the one running. Streamline
//     OTA-updates its plugins into
//     C:\ProgramData\NVIDIA\NGX\models\sl_dlss_g_0\versions\<n>\files\<hash>.dll
//     so GetModuleHandleW(L"sl.dlss_g.dll") finds nothing and a name-based patch
//     silently does nothing at all -- no error, no log line, no effect.
//
//  2. The flag's offset AND ITS POLARITY differ between builds. The game-folder
//     build clears [ctx+0x38bc] to mean "flip metering off"; the OTA build sets
//     [ctx+0x44f8] to 1 to mean the same thing. A hardcoded value is a coin flip
//     that silently does the opposite half the time.
//
// So derive everything from the binary: find the module carrying the marker
// string, find the code that logs it, and read the (offset, value) the fallback
// itself writes. That pair IS the wanted state, whatever its polarity. Then flip
// every other site writing that offset to match.

constexpr char kFlipMarker[] = "FG1 DLL has been detected";

struct FlipSite {
  unsigned char* address;
  unsigned char original[7];
  unsigned char length;
};

std::atomic_bool g_flip_meter_patched{false};
std::vector<FlipSite> g_flip_meter_sites;
std::atomic<unsigned int> g_flip_meter_offset{0};
std::atomic<unsigned int> g_flip_meter_value{0};
std::atomic<int> g_flip_meter_attempts{0};
constexpr int kMaxFlipMeterAttempts = 4000;

bool ModuleImage(HMODULE mod, unsigned char** out_base, const IMAGE_NT_HEADERS64** out_nt) {
  if (mod == nullptr) return false;
  auto* base = reinterpret_cast<unsigned char*>(mod);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
  *out_base = base;
  *out_nt = nt;
  return true;
}

// ------------------------------------------- Streamline's own frame ceiling
//
// The plugin starts with its own compiled maximum, then lowers it to the value
// reported by NGX:
//
//     BA 03 00 00 00   mov   edx, 3
//     3B CA            cmp   ecx, edx
//     0F 42 D1         cmovb edx, ecx      ; edx = min(count, 3)
//
// `ecx` is the device maximum cached by the Streamline wrapper, not the game's
// current request. Most games observe the rewritten NGX gates early enough for
// it to be 5. STALKER 2 does not: its wrapper caches 1, so this CMOV reduces the
// otherwise-valid compiled maximum back to one generated frame. The native UI
// can then expose 3x/4x, but slDLSSGSetOptions rejects either with
// eErrorInvalidState (38).
//
// Turn the conditional move into `cmovb edx, edx` by changing only its ModRM
// byte (D1 -> D2). This is an atomic one-byte code patch and leaves the
// instruction boundary intact. The immediate stays in place as a hard bound:
// old plugins remain capped at their compiled 3 generated frames (4x), while
// newer plugins keep their compiled 5 (6x). The
// opt-in RaiseFrameCeiling setting may still raise an old plugin's immediate,
// but is deliberately separate because doing so is not safe in every game.

constexpr unsigned char kCeilingTarget = 5;  // generated frames == 6x

std::atomic_bool g_ceiling_patched{false};
std::atomic_bool g_ceiling_plugin_seen{false};
HMODULE g_ceiling_module = nullptr;
std::wstring g_ceiling_module_path;
unsigned char* g_ceiling_site = nullptr;
unsigned char g_ceiling_original = 0;
unsigned char g_ceiling_cmov_original = 0;
unsigned int g_ceiling_compiled = 0;
unsigned int g_ceiling_effective = 0;
std::vector<HMODULE> g_ceiling_rejected_modules;
SRWLOCK g_streamline_maintenance_lock = SRWLOCK_INIT;

bool CeilingPatchStillOwned() {
  if (!g_ceiling_patched.load(std::memory_order_acquire) ||
      g_ceiling_module == nullptr || g_ceiling_site == nullptr ||
      !mfgunlock::runtimeversion::IsMappedImage(g_ceiling_module)) {
    return false;
  }
  std::vector<wchar_t> current_path(32768);
  const DWORD length = GetModuleFileNameW(
      g_ceiling_module, current_path.data(),
      static_cast<DWORD>(current_path.size()));
  if (length == 0 || length >= current_path.size() ||
      _wcsicmp(current_path.data(), g_ceiling_module_path.c_str()) != 0) {
    return false;
  }
  unsigned char* base = nullptr;
  const IMAGE_NT_HEADERS64* nt = nullptr;
  if (!ModuleImage(g_ceiling_module, &base, &nt)) return false;
  const uintptr_t offset = reinterpret_cast<uintptr_t>(g_ceiling_site) -
                           reinterpret_cast<uintptr_t>(base);
  if (offset > nt->OptionalHeader.SizeOfImage ||
      nt->OptionalHeader.SizeOfImage - offset < 10) {
    return false;
  }
  return g_ceiling_site[0] == 0xBA && g_ceiling_site[2] == 0 &&
         g_ceiling_site[3] == 0 && g_ceiling_site[4] == 0 &&
         g_ceiling_site[5] == 0x3B && g_ceiling_site[6] == 0xCA &&
         g_ceiling_site[7] == 0x0F && g_ceiling_site[8] == 0x42 &&
         g_ceiling_site[9] == 0xD2;
}

void AbandonStaleFrameCountCeiling() {
  g_ceiling_module = nullptr;
  g_ceiling_module_path.clear();
  g_ceiling_site = nullptr;
  g_ceiling_original = 0;
  g_ceiling_cmov_original = 0;
  g_ceiling_compiled = 0;
  g_ceiling_effective = 0;
  g_ceiling_patched.store(false, std::memory_order_release);
  mfgunlock::framecount::g_advertised_max_generated.store(
      0, std::memory_order_release);
}

void PatchFrameCountCeiling(HMODULE mod) {
  if (!mfgunlock::modulesession::Acquire(mod)) return;
  if (g_ceiling_patched.load(std::memory_order_acquire)) {
    if (CeilingPatchStillOwned()) return;
    reshade::log::message(
        reshade::log::level::info,
        "mfgunlock: ceiling bytes are no longer owned by this session; retaining restore records, disabling the advertised override and requiring restart.");
    g_ceiling_patched.store(false, std::memory_order_release);
    mfgunlock::framecount::g_advertised_max_generated.store(0, std::memory_order_release);
    return;
  }
  if (std::find(g_ceiling_rejected_modules.begin(),
                g_ceiling_rejected_modules.end(), mod) !=
      g_ceiling_rejected_modules.end()) {
    return;
  }

  unsigned char* base = nullptr;
  const IMAGE_NT_HEADERS64* nt = nullptr;
  if (!ModuleImage(mod, &base, &nt)) return;
  g_ceiling_plugin_seen.store(true, std::memory_order_release);

  const auto* profile = mfgunlock::binarypatch::ProfileFor(mod, 2);
  if (!profile || profile->count != 1 || !mfgunlock::binarypatch::Plan(mod, *profile, g_ceiling_writes,
      g_raise_ceiling.load(std::memory_order_relaxed) ? kCeilingTarget : 0)) return;
  unsigned char* found = base + profile->sites[0].rva;
  if (!mfgunlock::binarypatch::Apply(mod, g_ceiling_writes)) {
    if (!mfgunlock::pointerredirect::Pending(g_ceiling_writes)) g_ceiling_writes.clear();
    return;
  }
  g_ceiling_module = mod;
  std::vector<wchar_t> module_path(32768);
  const DWORD module_path_length = GetModuleFileNameW(
      mod, module_path.data(), static_cast<DWORD>(module_path.size()));
  g_ceiling_module_path =
      module_path_length != 0 && module_path_length < module_path.size()
          ? std::wstring(module_path.data(), module_path_length)
          : std::wstring();
  g_ceiling_site = found;
  g_ceiling_original = profile->sites[0].before[1];
  g_ceiling_cmov_original = profile->sites[0].before[9];
  g_ceiling_compiled = g_ceiling_original;
  g_ceiling_effective = found[1];
  g_ceiling_patched.store(true, std::memory_order_release);
  mfgunlock::framecount::g_advertised_max_generated.store(g_ceiling_effective,
                                                           std::memory_order_release);

  std::stringstream s;
  s << "mfgunlock: stopped the DLSS-G plugin from lowering its compiled ceiling of "
    << g_ceiling_compiled << " generated frame(s) to the stale NGX device value";
  if (g_ceiling_effective != g_ceiling_compiled) {
    s << "; RaiseFrameCeiling also changed the hard bound to " << g_ceiling_effective;
  }
  s << " (effective maximum " << (g_ceiling_effective + 1) << "x).";
  reshade::log::message(reshade::log::level::info, s.str().c_str());
}

void RestoreFrameCountCeiling() {
  mfgunlock::patching::Restore(g_ceiling_writes, true);
  if (mfgunlock::pointerredirect::Pending(g_ceiling_writes)) return;
  g_ceiling_writes.clear();
  g_ceiling_module = nullptr;
  g_ceiling_module_path.clear();
  g_ceiling_site = nullptr;
  g_ceiling_original = 0;
  g_ceiling_cmov_original = 0;
  g_ceiling_compiled = 0;
  g_ceiling_effective = 0;
  g_ceiling_rejected_modules.clear();
  g_ceiling_plugin_seen.store(false, std::memory_order_release);
  mfgunlock::framecount::g_advertised_max_generated.store(0, std::memory_order_release);
  g_ceiling_patched.store(false, std::memory_order_release);
}

void TryPatchFrameCountCeiling() {
  if (!g_enabled.load(std::memory_order_relaxed)) return;
  if (g_ceiling_patched.load(std::memory_order_acquire)) {
    if (CeilingPatchStillOwned()) {
      mfgunlock::framecount::ObserveStreamlinePluginVersion(g_ceiling_module);
      return;
    }
    AbandonStaleFrameCountCeiling();
  }
  AcquireSRWLockExclusive(&g_streamline_maintenance_lock);
  if (!g_ceiling_patched.load(std::memory_order_relaxed)) {
    if (HMODULE fast = GetModuleHandleW(L"sl.dlss_g.dll")) {
      PatchFrameCountCeiling(fast);
    }
    if (!g_ceiling_patched.load(std::memory_order_relaxed)) {
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
      if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = {};
        me.dwSize = sizeof(me);
        if (Module32FirstW(snap, &me)) {
          do {
            if (me.hModule == g_self_module ||
                !UnsearchedModuleContains(g_ceiling_searched, me.hModule, kFlipMarker,
                                          sizeof(kFlipMarker) - 1)) {
              continue;
            }
            PatchFrameCountCeiling(me.hModule);
            if (g_ceiling_patched.load(std::memory_order_relaxed)) break;
          } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
      }
    }
  }
  ReleaseSRWLockExclusive(&g_streamline_maintenance_lock);
  if (g_ceiling_patched.load(std::memory_order_acquire)) {
    mfgunlock::framecount::ObserveStreamlinePluginVersion(g_ceiling_module);
  }
}

// Handles the DLSS-G Streamline plugin wherever it was loaded from. Returns true
// once a module has been dealt with, so the caller stops scanning.
bool TryPatchFlipMeteringInModule(HMODULE mod) {
  if (std::any_of(g_flip_transactions.begin(), g_flip_transactions.end(), [mod](const auto& t) { return t.module == mod; })) return true;
  const auto* profile = mfgunlock::binarypatch::ProfileFor(mod, 1);
  if (!profile) return false;
  ++g_flip_meter_attempts;
  // No module lease (the module is unloading or unidentified): leave it alone,
  // as the architecture gate does.
  const auto* session = mfgunlock::modulesession::Acquire(mod);
  if (session == nullptr) return false;
  CodeTransaction transaction{mod, session->generation, {}};
  if (!mfgunlock::binarypatch::Plan(mod, *profile, transaction.writes) ||
      !mfgunlock::binarypatch::Apply(mod, transaction.writes)) {
    if (mfgunlock::pointerredirect::Pending(transaction.writes)) g_flip_transactions.push_back(std::move(transaction));
    return false;
  }
  for (const auto& write : transaction.writes) {
    FlipSite site{}; site.address = write.address; site.length = static_cast<unsigned char>(write.original.size());
    std::memcpy(site.original, write.original.data(), write.original.size()); g_flip_meter_sites.push_back(site);
  }
  g_flip_transactions.push_back(std::move(transaction));
  g_flip_meter_offset.store(profile->field); g_flip_meter_value.store(profile->value);
  g_flip_meter_patched.store(true, std::memory_order_release);
  reshade::log::message(reshade::log::level::info,
      "mfgunlock: Legacy software pacing committed using exact executable and instruction profile; restart required for changes.");
  return true;
}

void TryPatchFlipMeteringUnlocked() {
  if (g_flip_meter_patched.load(std::memory_order_acquire)) return;
  if (g_flip_meter_attempts.load(std::memory_order_relaxed) >=
      kMaxFlipMeterAttempts) return;

  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
  if (snap == INVALID_HANDLE_VALUE) return;

  MODULEENTRY32W me = {};
  me.dwSize = sizeof(me);
  if (Module32FirstW(snap, &me)) {
    do {
      if (me.hModule == g_self_module) continue;
      // Only the DLSS-G plugin carries the marker. Profiles validate a module
      // by hashing its whole code, so unrelated modules never reach them.
      if (!UnsearchedModuleContains(g_flip_searched, me.hModule, kFlipMarker, sizeof(kFlipMarker) - 1))
        continue;
      if (TryPatchFlipMeteringInModule(me.hModule)) break;
    } while (Module32NextW(snap, &me));
  }
  CloseHandle(snap);
}

void TryPatchFlipMetering() {
  AcquireSRWLockExclusive(&g_streamline_maintenance_lock);
  TryPatchFlipMeteringUnlocked();
  ReleaseSRWLockExclusive(&g_streamline_maintenance_lock);
}

void ProcessLoadedStreamlineDlssgPlugin(HMODULE mod) {
  if (!g_enabled.load(std::memory_order_relaxed)) return;
  if (!TryAcquireSRWLockExclusive(&g_streamline_maintenance_lock)) {
    g_provider_rescan_requested.store(true, std::memory_order_release);
    return;
  }
  PatchFrameCountCeiling(mod);
  if (g_force_flip_meter_off.load(std::memory_order_relaxed)) {
    TryPatchFlipMeteringInModule(mod);
  }
  ReleaseSRWLockExclusive(&g_streamline_maintenance_lock);
}

void RestoreFlipMetering() {
  for (auto& transaction : g_flip_transactions) mfgunlock::patching::Restore(transaction.writes, true);
  std::erase_if(g_flip_transactions, [](const auto& t) { return !mfgunlock::pointerredirect::Pending(t.writes); });
  if (!g_flip_transactions.empty()) return;
  g_flip_meter_sites.clear(); g_flip_meter_patched.store(false, std::memory_order_release);
}
