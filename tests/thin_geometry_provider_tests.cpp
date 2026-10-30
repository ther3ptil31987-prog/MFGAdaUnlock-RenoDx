// SPDX-License-Identifier: MIT
// 1.7 (plan C.3): the validated warp blend rebuilt from a real 310.9.16
// provider for both GPU targets. Ada retargets NVIDIA's sm_120 PTX to sm_89;
// RTX 50 keeps sm_120, so the driver JIT-compiles the rewritten PTX there.
// Skipped (77) without a local provider: NVIDIA binaries are not in the repo.
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "../src/addons/mfgunlock/thin_geometry.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << "FAILED " << __LINE__ << ": " #x "\n"; return EXIT_FAILURE; } } while (false)
namespace tg = mfgunlock::thingeometry;

namespace {
// The rebuilt fatbin's entry for the target architecture, and its PTX text.
bool FindRebuiltPtx(const std::vector<uint8_t>& fatbin, uint32_t arch,
                    std::string& ptx) {
  size_t cursor = tg::internal::kOuterHeader;
  while (cursor + 64 <= fatbin.size()) {
    const uint32_t header = tg::internal::ReadU32(fatbin.data() + cursor + 4);
    const uint64_t payload = tg::internal::ReadU64(fatbin.data() + cursor + 8);
    if (header < 64 || cursor + header + payload > fatbin.size()) return false;
    if (tg::internal::ReadU16(fatbin.data() + cursor) == tg::internal::kPtxKind &&
        tg::internal::ReadU32(fatbin.data() + cursor + 28) == arch &&
        tg::internal::ReadU32(fatbin.data() + cursor + 16) == 0) {
      ptx.assign(reinterpret_cast<const char*>(fatbin.data() + cursor + header),
                 static_cast<size_t>(payload));
      return true;
    }
    cursor += header + static_cast<size_t>(payload);
  }
  return false;
}
}  // namespace

int main(int argc, char** argv) {
  // Optional argv[2]: a directory that receives the rebuilt PTX per target,
  // so ptxas can check it (tools: ptxas -arch=sm_120 warp-sm120.ptx).
  if (argc < 2 || !std::filesystem::exists(argv[1])) {
    std::cout << "no local 310.9.16 provider; skipped\n";
    return 77;
  }
  std::ifstream input(argv[1], std::ios::binary);
  const std::vector<uint8_t> image{std::istreambuf_iterator<char>(input), {}};
  const auto* profile = tg::internal::ProfileFor(
      tg::Mechanism::ValidatedWarpBlend, tg::internal::BlendLayout::V310916);
  CHECK(profile != nullptr && profile->arch == tg::internal::kBlackwellArch);

  // Find the one fatbin whose sm_120 PTX matches the supported profile.
  const uint8_t* fatbin = nullptr;
  size_t fatbin_size = 0;
  for (size_t offset = 0; offset + 16 <= image.size(); ++offset) {
    if (tg::internal::ReadU32(image.data() + offset) != tg::internal::kFatbinMagic) continue;
    const uint64_t size = tg::internal::ReadU64(image.data() + offset + 8) + 16;
    if (tg::internal::ReadU16(image.data() + offset + 6) != 16 ||
        size > image.size() - offset || size > tg::internal::kMaxFatbinSize) continue;
    size_t entry = 0;
    std::vector<uint8_t> ptx;
    std::string why;
    if (tg::internal::FindPtxEntry(image.data() + offset, static_cast<size_t>(size),
                                   *profile, entry, ptx, why)) {
      CHECK(fatbin == nullptr);
      fatbin = image.data() + offset;
      fatbin_size = static_cast<size_t>(size);
    }
  }
  if (fatbin == nullptr) {
    std::cout << "provider is not 310.9.16; skipped\n";
    return 77;
  }

  tg::g_adaptive_quality_enabled = true;
  tg::g_adaptive_quality_profile =
      mfgunlock::adaptivequality::Profile::kLuminanceDirectionalV3;
  for (const uint32_t arch : {tg::internal::kAdaArch, tg::internal::kBlackwellArch}) {
    std::vector<uint8_t> rebuilt;
    auto version = mfgunlock::adaptivequality::ComponentVersion::kNative;
    std::string why;
    CHECK(tg::internal::BuildRedirectedFatbin(fatbin, fatbin_size, *profile, arch,
                                              rebuilt, version, why));
    CHECK(version == mfgunlock::adaptivequality::ComponentVersion::kV3);
    std::string ptx;
    CHECK(FindRebuiltPtx(rebuilt, arch, ptx));
    const char* target = arch == tg::internal::kAdaArch ? ".target sm_89" : ".target sm_120";
    CHECK(ptx.find(target) != std::string::npos);
    CHECK(ptx.find("MFGUNLOCK_VALIDATED_WARP_BLEND_V3") != std::string::npos);
    if (argc > 2) {
      std::ofstream dump(std::filesystem::path(argv[2]) /
                             (std::string("warp-sm") + std::to_string(arch) + ".ptx"),
                         std::ios::binary);
      dump << ptx.c_str();
    }
    // Every other entry (sm_89 PTX and the sm_89 cubin) survives untouched.
    CHECK(rebuilt.size() > fatbin_size);
    CHECK(std::memcmp(rebuilt.data() + rebuilt.size() - 64,
                      fatbin + fatbin_size - 64, 64) == 0);
  }
  // A PTX entry built for Ada cannot be offered to an RTX 50.
  const auto* ada_only = tg::internal::ProfileFor(tg::Mechanism::PreviousScatter);
  std::vector<uint8_t> rebuilt;
  auto version = mfgunlock::adaptivequality::ComponentVersion::kNative;
  std::string why;
  CHECK(!tg::internal::BuildRedirectedFatbin(fatbin, fatbin_size, *ada_only,
                                             tg::internal::kBlackwellArch,
                                             rebuilt, version, why));
  std::cout << "thin geometry provider tests passed (sm_89 and sm_120)\n";
  return EXIT_SUCCESS;
}
