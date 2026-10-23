// SPDX-License-Identifier: MIT
#pragma once
#include <sl.h>
namespace mfgunlock::xemfg::native {
// Some SL callers zero-initialize Resource's metadata instead of calling its
// constructor. The D3D prefix has the same layout. Admit only that exact empty
// header (or the known GUID with version zero). With the known GUID, a newer
// version is read through its V1 prefix (structures are append-only); an
// unknown GUID never infers a future ABI. The caller must still have succeeded
// upstream, and the native COM texture, device, extent, state and
// producer-list lifetime are validated separately.
inline bool ResourceHeaderSupported(const sl::Resource& resource) noexcept {
  if(resource.structVersion>=1) return resource.structType==sl::Resource::s_structType;
  const sl::StructType empty{};
  return resource.structVersion==0 && !resource.next
      && (resource.structType==sl::Resource::s_structType || resource.structType==empty);
}
}
