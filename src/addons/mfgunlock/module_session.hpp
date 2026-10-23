// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "patch_transaction.hpp"

namespace mfgunlock::modulesession {
inline uint64_t Hash(const uint8_t* p, size_t n) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < n; ++i) hash = (hash ^ p[i]) * 0x100000001b3ull;
  return hash;
}
struct Identity {
  uint32_t timestamp = 0, image_size = 0;
  uint64_t code_hash = 0;
  bool operator==(const Identity&) const = default;
};
// Hash executable bytes in their preferred-base form, matching the file even
// when the Windows loader has applied DIR64 relocations to executable sections.
inline bool Inspect(HMODULE module, Identity& id, const std::vector<patching::Write>* owned = nullptr) {
  MEMORY_BASIC_INFORMATION memory{};
  if (!module || VirtualQuery(module, &memory, sizeof(memory)) != sizeof(memory) ||
      memory.AllocationBase != module || memory.Type != MEM_IMAGE) return false;
  const auto* base = reinterpret_cast<const uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;
  const size_t size = nt->OptionalHeader.SizeOfImage;
  if (size < 4096 || size > 128u * 1024u * 1024u || nt->FileHeader.NumberOfSections > 96) return false;
  if (reinterpret_cast<const uint8_t*>(IMAGE_FIRST_SECTION(nt) + nt->FileHeader.NumberOfSections) > base + size) return false;
  uint64_t hash = 0xcbf29ce484222325ull;
  const auto delta = reinterpret_cast<uintptr_t>(module) - nt->OptionalHeader.ImageBase;
  const auto reloc = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  if (reloc.VirtualAddress > size || reloc.Size > size - reloc.VirtualAddress) return false;
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const auto& sec = section[i];
    if (!(sec.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    if (sec.VirtualAddress > size || sec.Misc.VirtualSize > size - sec.VirtualAddress) return false;
    std::vector<uint8_t> code(base + sec.VirtualAddress, base + sec.VirtualAddress + sec.Misc.VirtualSize);
    if (owned) for (const auto& write : *owned) {
      const auto at = reinterpret_cast<uintptr_t>(write.address) - reinterpret_cast<uintptr_t>(base);
      if (at < sec.VirtualAddress || at - sec.VirtualAddress > code.size() ||
          write.original.size() > code.size() - (at - sec.VirtualAddress)) continue;
      auto* current = code.data() + at - sec.VirtualAddress;
      if (std::memcmp(current, write.replacement.data(), write.replacement.size()) == 0)
        std::memcpy(current, write.original.data(), write.original.size());
    }
    size_t offset = 0;
    while (offset + sizeof(IMAGE_BASE_RELOCATION) <= reloc.Size) {
      const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(base + reloc.VirtualAddress + offset);
      if (block->SizeOfBlock < 8 || block->SizeOfBlock > reloc.Size - offset || (block->SizeOfBlock & 1)) return false;
      const auto* entries = reinterpret_cast<const WORD*>(block + 1);
      for (size_t j = 0; j < (block->SizeOfBlock - 8) / 2; ++j) {
        const uint64_t at = uint64_t(block->VirtualAddress) + (entries[j] & 4095u);
        if (at < sec.VirtualAddress || at >= uint64_t(sec.VirtualAddress) + code.size()) continue;
        const auto type = entries[j] >> 12;
        if (type == IMAGE_REL_BASED_ABSOLUTE) continue;
        if (type != IMAGE_REL_BASED_DIR64 || at - sec.VirtualAddress + 8 > code.size()) return false;
        uint64_t value = 0;
        std::memcpy(&value, code.data() + at - sec.VirtualAddress, 8);
        value -= delta;
        std::memcpy(code.data() + at - sec.VirtualAddress, &value, 8);
      }
      offset += block->SizeOfBlock;
    }
    for (auto byte : code) hash = (hash ^ byte) * 0x100000001b3ull;
  }
  id = {nt->FileHeader.TimeDateStamp, static_cast<uint32_t>(size), hash};
  return true;
}
struct Session {
  HMODULE module; Identity identity; uint64_t generation;
  mutable SRWLOCK code_lock = SRWLOCK_INIT;
  mutable std::vector<patching::Write> owned_code;
};
inline SRWLOCK lock = SRWLOCK_INIT;
inline std::vector<std::unique_ptr<Session>> sessions;
inline uint64_t next_generation = 0;
// Only modules used by patch/hook targets receive a lease. References are
// intentionally retained until process exit: the host offers no proven safe
// hot-unload boundary for CUDA registrations or callbacks in flight. Bounded,
// no per-frame work, no GET_MODULE_HANDLE_EX_FLAG_PIN for provider modules.
inline const Session* Acquire(HMODULE module) {
  if (!module || !TryAcquireSRWLockExclusive(&lock)) return nullptr;
  struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&lock); } } unlock;
  for (const auto& session : sessions) if (session->module == module) return session->identity.image_size ? session.get() : nullptr;
  if (sessions.size() >= 64) return nullptr;
  HMODULE retained = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          reinterpret_cast<LPCWSTR>(module), &retained) || retained != module) return nullptr;
  Identity id;
  if (!Inspect(module, id)) {
    sessions.push_back(std::unique_ptr<Session>(new Session{module, {}, 0}));
    return nullptr;  // Failed leases are also bounded and never authorize writes.
  }
  sessions.push_back(std::unique_ptr<Session>(new Session{module, id, ++next_generation}));
  return sessions.back().get();
}
inline bool SameGeneration(const Session& session, uint64_t generation) {
  return generation != 0 && session.generation == generation;
}
inline bool Validate(const Session& session) {
  if (!TryAcquireSRWLockShared(&session.code_lock)) return false;
  Identity now{};
  const bool valid = Inspect(session.module, now, &session.owned_code) && now == session.identity;
  ReleaseSRWLockShared(&session.code_lock);
  return valid;
}
inline void RecordCode(const Session& session, const std::vector<patching::Write>& writes) {
  AcquireSRWLockExclusive(&session.code_lock);
  for (const auto& write : writes) {
    if (!write.applied) continue;
    auto it = std::find_if(session.owned_code.begin(), session.owned_code.end(),
                            [&](const auto& w) { return w.address == write.address; });
    if (it == session.owned_code.end()) session.owned_code.push_back(write);
    else *it = write;
  }
  ReleaseSRWLockExclusive(&session.code_lock);
}
}  // namespace mfgunlock::modulesession
