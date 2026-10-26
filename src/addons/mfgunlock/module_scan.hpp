/*
 * Marker search in mapped images, with a record of the images already
 * searched (1.5.7). Startup discovery repeats its module walk on every pass
 * of the bounded worker and on device/queue creation; without the record,
 * every pass searched every readable section of every module again.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mfgunlock::modulescan {

inline bool ContainsBytes(const unsigned char* data, size_t size, const char* needle, size_t length) {
  if (data == nullptr || needle == nullptr || length == 0 || size < length) return false;
  const auto* first = reinterpret_cast<const unsigned char*>(needle);
  const std::boyer_moore_horspool_searcher searcher(first, first + length);
  return std::search(data, data + size, searcher) != data + size;
}

// A mapped image as the loader placed it. The timestamp and size tell a module
// apart from another one later mapped at the same address.
struct ImageIdentity {
  HMODULE module = nullptr;
  uint32_t timestamp = 0, image_size = 0;
  bool operator==(const ImageIdentity&) const = default;
};

inline const IMAGE_NT_HEADERS64* ImageHeaders(HMODULE module) {
  MEMORY_BASIC_INFORMATION memory{};
  if (module == nullptr || VirtualQuery(module, &memory, sizeof(memory)) != sizeof(memory) ||
      memory.AllocationBase != module || memory.Type != MEM_IMAGE || memory.State != MEM_COMMIT)
    return nullptr;
  const auto* base = reinterpret_cast<const unsigned char*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return nullptr;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    return nullptr;
  return nt;
}

inline bool ReadIdentity(HMODULE module, ImageIdentity& identity) {
  const auto* nt = ImageHeaders(module);
  if (nt == nullptr) return false;
  identity = {module, nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage};
  return true;
}

// Whether a readable section of the mapped image contains the needle.
inline bool ImageContains(HMODULE module, const char* needle, size_t length) {
  const auto* nt = ImageHeaders(module);
  if (nt == nullptr) return false;
  const auto* base = reinterpret_cast<const unsigned char*>(module);
  const auto size = nt->OptionalHeader.SizeOfImage;
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
    if (section->VirtualAddress > size || section->Misc.VirtualSize > size - section->VirtualAddress) continue;
    if (ContainsBytes(base + section->VirtualAddress, section->Misc.VirtualSize, needle, length)) return true;
  }
  return false;
}

// Images already searched for one marker.
class SearchedImages {
 public:
  bool Contains(const ImageIdentity& identity) const {
    return std::find(images_.begin(), images_.end(), identity) != images_.end();
  }
  void Add(const ImageIdentity& identity) {
    if (!Contains(identity)) images_.push_back(identity);
  }
  size_t size() const { return images_.size(); }

 private:
  std::vector<ImageIdentity> images_;
};

}  // namespace mfgunlock::modulescan
