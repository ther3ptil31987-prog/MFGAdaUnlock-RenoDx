// SPDX-License-Identifier: MIT
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "../src/addons/mfgunlock/blackwell.hpp"
#include "../src/addons/mfgunlock/midpoint.hpp"
#include "../src/addons/mfgunlock/thin_geometry.hpp"

namespace {

size_t CountArchGates(HMODULE module) {
  auto* base = reinterpret_cast<unsigned char*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

  size_t count = 0;
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
    const unsigned char* start = base + section->VirtualAddress;
    const size_t size = section->Misc.VirtualSize;
    for (size_t offset = 0; offset + 6 <= size; ++offset) {
      if (start[offset] == 0x3D && start[offset + 1] == 0xB0 &&
          start[offset + 2] == 0x01 && start[offset + 3] == 0x00 &&
          start[offset + 4] == 0x00) {
        ++count;
        continue;
      }
      if (start[offset] == 0x81 && start[offset + 1] >= 0xF8 &&
          start[offset + 1] <= 0xFF && start[offset + 2] == 0xB0 &&
          start[offset + 3] == 0x01 && start[offset + 4] == 0x00 &&
          start[offset + 5] == 0x00) {
        ++count;
      }
    }
  }
  return count;
}

bool Contains(HMODULE module, const char* needle) {
  auto* base = reinterpret_cast<unsigned char*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  const size_t needle_length = std::strlen(needle);
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
    const unsigned char* start = base + section->VirtualAddress;
    const size_t size = section->Misc.VirtualSize;
    for (size_t offset = 0; offset + needle_length <= size; ++offset) {
      if (std::memcmp(start + offset, needle, needle_length) == 0) return true;
    }
  }
  return false;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 2 && argc != 3) {
    std::wcerr << L"usage: provider_probe <nvngx_dlssg.dll> [--refined|--border|--geometry-v2|--geometry-v2-border|--adaptive|--adaptive-inpaint-temporal]\n";
    return 2;
  }
  const std::wstring mode = argc == 3 ? argv[2] : L"";
  if (!mode.empty() && mode != L"--refined" && mode != L"--border" &&
      mode != L"--geometry-v2" && mode != L"--geometry-v2-border" &&
      mode != L"--adaptive" && mode != L"--adaptive-inpaint-temporal") {
    std::wcerr << L"unknown probe mode: " << mode << L'\n';
    return 2;
  }
  const bool adaptive = mode == L"--adaptive" ||
                        mode == L"--adaptive-inpaint-temporal";
  const bool refined = !mode.empty() && !adaptive;
  const bool geometry_v2 = mode == L"--geometry-v2" ||
                           mode == L"--geometry-v2-border";
  mfgunlock::thingeometry::g_refinement_enabled = refined;
  mfgunlock::thingeometry::g_adaptive_quality_enabled = adaptive;
  if (adaptive) {
    mfgunlock::thingeometry::g_adaptive_quality_profile =
        mfgunlock::adaptivequality::Profile::kLuminanceDirectionalV3;
  }
  mfgunlock::thingeometry::g_border_confidence_enabled =
      mode == L"--border" || mode == L"--geometry-v2-border";
  mfgunlock::blackwell::g_refinement_enabled = refined;
  mfgunlock::blackwell::g_adaptive_quality_enabled = adaptive;
  if (adaptive) {
    mfgunlock::blackwell::g_adaptive_quality_profile =
        mfgunlock::adaptivequality::Profile::kLuminanceDirectionalV3;
    mfgunlock::blackwell::g_adaptive_quality_v3_oriented_geometry = true;
    mfgunlock::blackwell::g_adaptive_quality_v3_inpaint_mode =
        mode == L"--adaptive-inpaint-temporal" ? 2u : 0u;
  }
  mfgunlock::blackwell::g_geometry_confidence_v2_enabled = geometry_v2;
  HMODULE module = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
  if (module == nullptr) {
    std::wcerr << L"LoadLibraryExW failed: " << GetLastError() << L'\n';
    return 3;
  }

  std::cout << "arch_gates=" << CountArchGates(module) << '\n';
  std::cout << "has_flip_meter_marker="
            << (Contains(module, "FG1 DLL has been detected") ? "yes" : "no") << '\n';
  std::cout << "d3d12_provider_export="
            << (GetProcAddress(module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl")
                    ? "yes" : "no")
            << '\n';
  std::cout << "vulkan_provider_export="
            << (GetProcAddress(module, "NVSDK_NGX_VULKAN_PopulateDeviceParameters_Impl")
                    ? "yes" : "no")
            << '\n';

  std::vector<mfgunlock::midpoint::Patch> patches;
  void* allocation = nullptr;
  std::string detail;
  std::vector<mfgunlock::blackwell::Patch> blackwell_patches;
  std::vector<void*> blackwell_allocations;
  mfgunlock::blackwell::Result blackwell_result;
  std::string blackwell_detail;
  const bool blackwell_supported = mfgunlock::blackwell::Apply(
      module, blackwell_patches, blackwell_allocations, blackwell_result,
      blackwell_detail, true,
      adaptive ? mfgunlock::blackwell::SilhouetteGuardMode::Balanced
               : mfgunlock::blackwell::SilhouetteGuardMode::Off,
      adaptive);
  std::cout << "blackwell_framework_supported=" << (blackwell_supported ? "yes" : "no")
            << '\n';
  std::cout << "blackwell_motion_vector="
            << (blackwell_result.motion_vector ? "yes" : "no") << '\n';
  std::cout << "blackwell_inpaint=" << (blackwell_result.inpaint ? "yes" : "no") << '\n';
  std::cout << "blackwell_inpaint_decision="
            << (blackwell_result.inpaint_decision ? "yes" : "no") << '\n';
  std::cout << "adaptive_geometry="
            << (blackwell_result.adaptive_geometry ? "applied" : "not-applied")
            << '\n';
  std::cout << "adaptive_inpaint_decision="
            << (blackwell_result.adaptive_inpaint_decision ? "applied" : "not-applied")
            << '\n';
  std::cout << "adaptive_inpaint_variant="
            << mfgunlock::blackwell::AdaptiveInpaintVariantName(
                   blackwell_result.adaptive_inpaint_variant)
            << '\n';
  std::cout << "blackwell_detail=" << blackwell_detail << '\n';

  std::vector<mfgunlock::thingeometry::Redirect> thin_redirects;
  mfgunlock::thingeometry::Result thin_result;
  std::string thin_provider_version;
  const bool thin_supported = mfgunlock::thingeometry::Apply(
      module, {true, true}, thin_redirects, thin_result,
      thin_provider_version);
  std::cout << "thin_geometry_provider=" << thin_provider_version << '\n';
  std::cout << "thin_geometry_redirect_supported="
            << (thin_supported ? "yes" : "no") << '\n';
  std::cout << "validated_warp_blend="
            << (thin_result.validated_warp_blend.applied ? "applied" : "not-applied")
            << ": " << thin_result.validated_warp_blend.detail << '\n';
  std::cout << "previous_scatter="
            << (thin_result.previous_scatter.applied ? "applied" : "not-applied")
            << ": " << thin_result.previous_scatter.detail << '\n';
  std::cout << "intermediate_scatter="
            << (blackwell_result.intermediate_scatter ? "applied" : "not-applied")
            << '\n';
  mfgunlock::thingeometry::Restore(thin_redirects);
  mfgunlock::blackwell::Restore(blackwell_patches, blackwell_allocations);

  mfgunlock::blackwell::Result silhouette_result;
  std::string silhouette_detail;
  const bool silhouette_supported = mfgunlock::blackwell::Apply(
      module, blackwell_patches, blackwell_allocations, silhouette_result,
      silhouette_detail, true,
      refined ? mfgunlock::blackwell::SilhouetteGuardMode::Balanced :
                mfgunlock::blackwell::SilhouetteGuardMode::Aggressive);
  std::cout << "silhouette_guard_framework_supported="
            << (silhouette_supported ? "yes" : "no") << '\n';
  std::cout << "silhouette_guard="
            << (silhouette_result.silhouette_guard ? "applied" : "not-applied")
            << '\n';
  std::cout << "silhouette_guard_fallback="
            << (silhouette_result.silhouette_guard_fallback ? "yes" : "no")
            << '\n';
  std::cout << "silhouette_guard_selected="
            << mfgunlock::blackwell::SilhouetteGuardName(
                   silhouette_result.silhouette_guard_mode_selected)
            << '\n';
  std::cout << "geometry_confidence_v2="
            << (silhouette_result.geometry_confidence_v2 ? "applied" : "not-applied")
            << '\n';
  std::cout << "silhouette_guard_detail=" << silhouette_detail << '\n';
  mfgunlock::blackwell::Restore(blackwell_patches, blackwell_allocations);

  const bool temporal_supported =
      mfgunlock::midpoint::Apply(module, patches, allocation, detail);
  std::cout << "temporal_profile_supported=" << (temporal_supported ? "yes" : "no")
            << '\n';
  std::cout << "temporal_detail=" << detail << '\n';
  mfgunlock::midpoint::Restore(patches, allocation);
  FreeLibrary(module);
  return temporal_supported && (!refined ||
      (silhouette_result.refined_geometry && thin_result.validated_warp_blend.applied)) &&
      (!geometry_v2 || silhouette_result.geometry_confidence_v2) &&
      (!adaptive ||
       (blackwell_result.adaptive_geometry &&
        blackwell_result.adaptive_inpaint_decision &&
        thin_result.validated_warp_blend.applied)) ? 0 : 1;
}
