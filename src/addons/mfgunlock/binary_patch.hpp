// SPDX-License-Identifier: MIT
#pragma once
#include "binary_profiles.hpp"
#include "module_session.hpp"
#include "patch_transaction.hpp"
namespace mfgunlock::binarypatch {
inline const binaryprofiles::Profile* ProfileFor(HMODULE module, uint32_t kind) {
  const auto* session = modulesession::Acquire(module);
  if (!session || !modulesession::Validate(*session)) return nullptr;
  for (const auto& profile : binaryprofiles::kProfiles) {
    if (profile.kind == kind && profile.timestamp == session->identity.timestamp &&
        profile.image_size == session->identity.image_size &&
        profile.code_hash == session->identity.code_hash) return &profile;
  }
  return nullptr;
}
inline bool Plan(HMODULE module, const binaryprofiles::Profile& profile,
                 std::vector<patching::Write>& writes, uint8_t raised_ceiling = 0) {
  if (!writes.empty() || profile.count == 0 || profile.count > 4) return false;
  auto* base = reinterpret_cast<uint8_t*>(module);
  for (size_t i = 0; i < profile.count; ++i) {
    const auto& site = profile.sites[i];
    if (!site.size || site.size > sizeof(site.before) || site.rva > profile.image_size ||
        site.size > profile.image_size - site.rva ||
        std::memcmp(base + site.rva, site.before, site.size) != 0) {
      writes.clear(); return false;
    }
    if (profile.kind == 0) {
      // Preserve the existing atomic one-byte gate write. The full instruction
      // and executable content were validated, but no larger memcpy is needed.
      const size_t offset = site.before[0] == 0x3d ? 1 : 2;
      writes.push_back(patching::Bytes(base + site.rva + offset, site.after + offset, 1));
    } else if (profile.kind == 1 && site.size == 7 && site.before[0] == 0xc6 && site.after[0] == 0xc6) {
      // Keep the one-byte immediate update used by the compatible legacy path.
      writes.push_back(patching::Bytes(base + site.rva + 6, site.after + 6, 1));
    } else if (profile.kind == 2) {
      // The ceiling window identifies ten bytes, but only changed one-byte
      // immediates/ModRM are written, preserving the baseline write granularity.
      for (size_t offset = 0; offset < site.size; ++offset) {
        uint8_t value = site.after[offset];
        if (offset == 1 && raised_ceiling && site.before[offset] < raised_ceiling)
          value = raised_ceiling;
        if (value != site.before[offset])
          writes.push_back(patching::Bytes(base + site.rva + offset, &value, 1));
      }
    } else writes.push_back(patching::Bytes(base + site.rva, site.after, site.size));
  }
  return true;
}
inline bool Apply(HMODULE module, std::vector<patching::Write>& writes) {
  const auto* session = modulesession::Acquire(module);
  if (!session || !modulesession::Validate(*session)) return false;
  const bool result = patching::Apply(writes, true);
  modulesession::RecordCode(*session, writes);
  return result;
}
}  // namespace mfgunlock::binarypatch
