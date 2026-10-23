// SPDX-License-Identifier: MIT
#pragma once
#include "patch_transaction.hpp"

namespace mfgunlock::pointerredirect {
// Validates the whole reference set before any descriptor changes. The caller
// retains the allocation whenever Pending() is true, including failed rollback.
inline bool Pending(const std::vector<patching::Write>& writes) {
  return std::any_of(writes.begin(), writes.end(),
                      [](const auto& w) { return w.applied; });
}
inline bool Apply(const std::vector<uint64_t*>& slots, uint64_t original,
                  void* allocation, std::vector<patching::Write>& writes) {
  if (slots.empty() || slots.size() > 32 || allocation == nullptr || !writes.empty()) return false;
  const uint64_t replacement = reinterpret_cast<uint64_t>(allocation);
  for (auto* slot : slots) {
    if (!patching::Readable(slot, sizeof(*slot)) || *slot != original) { writes.clear(); return false; }
    auto w = patching::Bytes(slot, &replacement, sizeof(replacement));
    writes.push_back(std::move(w));
  }
  return patching::Apply(writes);
}
}  // namespace mfgunlock::pointerredirect
