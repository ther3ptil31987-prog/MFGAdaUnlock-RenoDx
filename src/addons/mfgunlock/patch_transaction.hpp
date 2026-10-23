// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mfgunlock::patching {
// Callers must own the module lifetime and establish a safe execution boundary.
// This transaction provides preflight, write ownership and failure recovery;
// it does not make concurrent instruction execution safe.
struct Write {
  uint8_t* address = nullptr;
  std::vector<uint8_t> original, replacement;
  bool applied = false;
};
inline bool Copy(void* dst, const void* src, size_t size) {
  std::memcpy(dst, src, size);
  return true;
}
struct Operations {
  decltype(&VirtualProtect) protect = &VirtualProtect;
  bool (*copy)(void*, const void*, size_t) = &Copy;
};
// Deterministic fault injection for the offline harness only. Never changed by
// the addon. Each test runs in its own process.
inline Operations operations{};
inline bool Readable(const void* address, size_t size) {
  const auto begin = reinterpret_cast<uintptr_t>(address);
  if (!begin || !size || begin > UINTPTR_MAX - size) return false;
  auto at = begin;
  while (at < begin + size) {
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(at), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) ||
        !((memory.Protect & 0xff) & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                      PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    const auto end = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    if (end <= at) return false;
    at = end;
  }
  return true;
}
inline Write Bytes(void* address, const void* replacement, size_t size) {
  auto* at = static_cast<uint8_t*>(address);
  auto* value = static_cast<const uint8_t*>(replacement);
  if (!Readable(at, size) || !replacement) return {};
  return {at, {at, at + size}, {value, value + size}, false};
}
inline bool Restore(std::vector<Write>& writes, bool executable = false) {
  bool ok = true;
  for (auto it = writes.rbegin(); it != writes.rend(); ++it) {
    auto& write = *it;
    if (!write.applied) continue;
    const size_t size = write.original.size();
    if (size != write.replacement.size() || !Readable(write.address, size)) { ok = false; continue; }
    if (std::memcmp(write.address, write.original.data(), size) == 0) {
      write.applied = false;
      continue;
    }
    if (std::memcmp(write.address, write.replacement.data(), size) != 0) {
      ok = false;  // Another writer owns these bytes now. Never overwrite it.
      continue;
    }
    DWORD previous = 0, ignored = 0;
    if (!operations.protect(write.address, size,
                            executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE,
                            &previous)) { ok = false; continue; }
    const bool copied = operations.copy(write.address, write.original.data(), size);
    if (copied) write.applied = false;
    else ok = false;
    if (!operations.protect(write.address, size, previous, &ignored)) ok = false;
    if (executable) FlushInstructionCache(GetCurrentProcess(), write.address, size);
  }
  return ok && std::none_of(writes.begin(), writes.end(),
                            [](const Write& w) { return w.applied; });
}
inline bool Apply(std::vector<Write>& writes, bool executable = false) {
  if (writes.empty()) return false;
  std::vector<DWORD> protections(writes.size());
  for (size_t i = 0; i < writes.size(); ++i) {
    const auto& w = writes[i];
    if (!w.address || w.applied || w.original.empty() ||
        w.original.size() != w.replacement.size() ||
        !Readable(w.address, w.original.size()) ||
        std::memcmp(w.address, w.original.data(), w.original.size()) != 0) return false;
    const auto begin = reinterpret_cast<uintptr_t>(w.address);
    if (begin > UINTPTR_MAX - w.original.size()) return false;
    for (size_t j = 0; j < i; ++j) {
      const auto other = reinterpret_cast<uintptr_t>(writes[j].address);
      if (begin < other + writes[j].original.size() &&
          other < begin + w.original.size()) return false;
    }
  }
  // Make every site writable before changing any bytes. Reverse restoration
  // is necessary when sites share a page and later protects observe RWX.
  size_t opened = 0;
  for (; opened < writes.size(); ++opened) {
    auto& w = writes[opened];
    if (!operations.protect(w.address, w.original.size(),
          executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE,
          &protections[opened])) break;
  }
  bool ok = opened == writes.size();
  if (ok) {
    for (auto& w : writes) {
      if (std::memcmp(w.address, w.original.data(), w.original.size()) != 0) {
        ok = false; break;
      }
      w.applied = true;
      if (!operations.copy(w.address, w.replacement.data(), w.replacement.size())) {
        ok = false; break;
      }
    }
    if (!ok) Restore(writes, executable);
  }
  while (opened != 0) {
    --opened;
    DWORD ignored = 0;
    if (!operations.protect(writes[opened].address, writes[opened].original.size(),
                            protections[opened], &ignored)) ok = false;
  }
  if (executable) for (const auto& w : writes)
    FlushInstructionCache(GetCurrentProcess(), w.address, w.original.size());
  if (!ok) Restore(writes, executable);
  return ok;
}
}  // namespace mfgunlock::patching
