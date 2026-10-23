// SPDX-License-Identifier: MIT
// Content identities only; no NVIDIA instructions or payloads are distributed.
// Reproduced by tools/build_compatibility_profiles.py from installed providers.
#pragma once
#include <cstddef>
#include <cstdint>
namespace mfgunlock::kernelidentity {
struct Identity { uint32_t text, shared, registers; size_t bytes; uint64_t hash; };
inline constexpr Identity kOriginals[] = {
    {36992,7776,39,39968,0x9642092def23b3dfull},
    {14464,3920,23,17568,0x1ba6454ab039f9ddull},
    {12032,784,48,15136,0xc4a5eb4a8694f835ull},
    {36992,7776,39,39712,0xdd991f74dc082e37ull},
    {14464,3920,23,17440,0xb0c33a7b62325f66ull},
    {12032,784,48,15008,0x393c0362ab39ca20ull},
    {12032,784,48,15008,0xba2ad98ffeafb4f5ull},
};
inline bool Known(uint32_t text, uint32_t shared, uint32_t registers,
                  size_t size, uint64_t hash) {
  for (const auto& id : kOriginals)
    if (id.text == text && id.shared == shared && id.registers == registers &&
        id.bytes == size && id.hash == hash) return true;
  return false;
}
}  // namespace mfgunlock::kernelidentity
