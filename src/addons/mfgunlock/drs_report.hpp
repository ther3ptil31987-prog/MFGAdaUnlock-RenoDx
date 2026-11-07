// SPDX-License-Identifier: MIT
#pragma once
// 1.9.0: a read-only look at the NVIDIA driver profile for the support report:
// the NVIDIA app's frame generation and Streamline overrides and the Dynamic
// MFG keys, for this game's profile and the global (base) profile. It runs only
// when someone saves or copies the report. It opens a DRS session, loads the
// settings, reads, and destroys the session: nothing is ever written.
//
// Entry points come from nvapi_QueryInterface (no nvapi import). Structure
// layouts are nvapi.h R5xx: NVDRS_SETTING_V1 (12320 bytes, also used by
// drs_override.hpp) and NVDRS_APPLICATION_V4 (20492 bytes).
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "./drs_override.hpp"

namespace mfgunlock::drsreport {

struct Key {
  uint32_t id;
  const char* name;
};
// Names as the NVIDIA app and NVIDIA Profile Inspector describe them.
inline constexpr Key kKeys[] = {
    {0x10E41E03, "DLSS-FG override enabled"},
    {0x10E41E06, "Streamline override enabled"},
    {0x10308298, "DLSS-G mode (4 = Dynamic)"},
    {0x104D6667, "multi-frame count (generated frames)"},
    {0x10562D0F, "Dynamic maximum (generated frames)"},
    {0x10CF4125, "Dynamic target FPS"},
    {0x104399DC, "Dynamic denied"},
};

constexpr uint32_t kApplicationSize = 20492;
constexpr uint32_t kApplicationVersion4 = kApplicationSize | (4u << 16);

inline const char* LocationName(uint32_t location) {
  switch (location) {
    case 0: return "this profile";
    case 1: return "global profile";
    case 2: return "base profile";
    case 3: return "driver default";
    default: return "?";
  }
}

inline std::string Describe() {
  using QueryFn = void*(__cdecl*)(uint32_t);
  using InitializeFn = int(__cdecl*)();
  using CreateSessionFn = int(__cdecl*)(void**);
  using DestroySessionFn = int(__cdecl*)(void*);
  using LoadSettingsFn = int(__cdecl*)(void*);
  using GetBaseProfileFn = int(__cdecl*)(void*, void**);
  using FindApplicationFn = int(__cdecl*)(void*, const wchar_t*, void**, void*);
  using GetSettingFn = int(__cdecl*)(void*, void*, uint32_t, void*);

  std::string out = "driver_profile (read only):\n";
  HMODULE nvapi = GetModuleHandleW(L"nvapi64.dll");
  if (nvapi == nullptr) nvapi = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto query = nvapi ? reinterpret_cast<QueryFn>(GetProcAddress(nvapi, "nvapi_QueryInterface")) : nullptr;
  if (query == nullptr) return out + "  NVAPI not available\n";
  const auto initialize = reinterpret_cast<InitializeFn>(query(0x0150e828));
  const auto create = reinterpret_cast<CreateSessionFn>(query(0x0694d52e));
  const auto destroy = reinterpret_cast<DestroySessionFn>(query(0xdad9cff8));
  const auto load = reinterpret_cast<LoadSettingsFn>(query(0x375dbd6b));
  const auto base_profile = reinterpret_cast<GetBaseProfileFn>(query(0xda8466a0));
  const auto find = reinterpret_cast<FindApplicationFn>(query(0xeee566b2));
  // The addon's own GetSetting hook, when installed, changes only its own
  // Dynamic answer; read past it so the report shows the stored profile.
  auto get = drsoverride::g_real_get_setting != nullptr ? drsoverride::g_real_get_setting
                                                        : reinterpret_cast<GetSettingFn>(query(0x73bf8338));
  if (!initialize || !create || !destroy || !load || !base_profile || !find || !get)
    return out + "  NVAPI DRS functions not available\n";
  if (initialize() != 0) return out + "  NVAPI could not initialize\n";
  void* session = nullptr;
  if (create(&session) != 0 || session == nullptr) return out + "  no DRS session\n";
  struct Close {
    DestroySessionFn destroy;
    void* session;
    ~Close() { destroy(session); }
  } close{destroy, session};
  if (load(session) != 0) return out + "  the driver settings could not be loaded\n";

  std::vector<uint8_t> setting(drsoverride::kSettingSize);
  const auto dump = [&](void* profile, const char* label) {
    out += std::string("  ") + label + ":\n";
    for (const auto& key : kKeys) {
      std::fill(setting.begin(), setting.end(), uint8_t{0});
      std::memcpy(setting.data(), &drsoverride::kSettingVersion1, sizeof(uint32_t));
      const int status = get(session, profile, key.id, setting.data());
      char line[160];
      if (status != 0) {
        std::snprintf(line, sizeof(line), "    0x%08X %s: not set\n", key.id, key.name);
      } else {
        uint32_t value = 0, location = 0;
        std::memcpy(&value, setting.data() + drsoverride::kCurrentValueOffset, sizeof(value));
        std::memcpy(&location, setting.data() + drsoverride::kSettingLocationOffset, sizeof(location));
        std::snprintf(line, sizeof(line), "    0x%08X %s: %u (from %s)\n", key.id, key.name, value,
                      LocationName(location));
      }
      out += line;
    }
  };

  // NvAPI_UnicodeString: the driver may read all 2048 characters.
  std::vector<wchar_t> exe(2048, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, exe.data(), MAX_PATH);
  std::vector<uint8_t> application(kApplicationSize);
  std::memcpy(application.data(), &kApplicationVersion4, sizeof(uint32_t));
  void* game_profile = nullptr;
  if (length != 0 && length < MAX_PATH && find(session, exe.data(), &game_profile, application.data()) == 0 &&
      game_profile != nullptr)
    dump(game_profile, "this game's profile");
  else
    out += "  this game's profile: none (the global profile applies)\n";
  void* global = nullptr;
  if (base_profile(session, &global) == 0 && global != nullptr) dump(global, "global (base) profile");
  return out;
}

}  // namespace mfgunlock::drsreport
