/*
 * DLSS Super Resolution preset selected in the panel (1.6, plan B).
 * SPDX-License-Identifier: MIT
 *
 * The game passes its presets to slDLSSSetOptions, one per quality mode.
 * With a preset selected, the DLSS-G backend forwards a copy of the game's
 * options with every mode's preset replaced; the game's structure is never
 * written. Changing the options is enough to apply it during play: sl.dlss
 * compares the mode, the output size and the five presets on every
 * evaluation and recreates the DLSS feature when any changed (Streamline
 * 2.7 to 2.14), resetting one frame of history.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>

namespace mfgunlock::dlsspreset {

// 0 keeps the game's presets; otherwise an sl::DLSSPreset value. Only the
// current presets are offered: J and K (transformer), L (ultra performance)
// and M (performance). A-D were removed, E-F are deprecated, G-I and N-O
// revert to the default.
inline bool IsSelectable(uint32_t preset) {
  return preset == 0 || (preset >= static_cast<uint32_t>(sl::DLSSPreset::ePresetJ) &&
                         preset <= static_cast<uint32_t>(sl::DLSSPreset::ePresetM));
}

// Bytes of DLSSOptions that a caller of each structure version provides. The
// presets are part of version 1; version 2 added useAutoExposure and version
// 3 alphaUpscalingEnabled. 0 for a version this build does not know.
inline size_t KnownSize(uint32_t version) {
  switch (version) {
    case sl::kStructVersion1: return offsetof(sl::DLSSOptions, useAutoExposure);
    case sl::kStructVersion2: return offsetof(sl::DLSSOptions, alphaUpscalingEnabled);
    case sl::kStructVersion3: return sizeof(sl::DLSSOptions);
    default: return 0;
  }
}

// The game's preset for the mode it selected.
inline sl::DLSSPreset GamePreset(const sl::DLSSOptions& options) {
  switch (options.mode) {
    case sl::DLSSMode::eMaxPerformance: return options.performancePreset;
    case sl::DLSSMode::eBalanced: return options.balancedPreset;
    case sl::DLSSMode::eMaxQuality: return options.qualityPreset;
    case sl::DLSSMode::eUltraPerformance: return options.ultraPerformancePreset;
    case sl::DLSSMode::eUltraQuality: return options.ultraQualityPreset;
    case sl::DLSSMode::eDLAA: return options.dlaaPreset;
    default: return sl::DLSSPreset::eDefault;
  }
}

// Copies the bytes the caller provides into `out` and sets every mode's
// preset. Returns false, and the game's options are forwarded unchanged, when
// nothing is selected, DLSS is off or the structure version is unknown.
inline bool Override(const sl::DLSSOptions& in, uint32_t preset, sl::DLSSOptions& out) {
  if (preset == 0 || !IsSelectable(preset) || in.mode == sl::DLSSMode::eOff) return false;
  const size_t size = KnownSize(in.structVersion);
  if (size == 0) return false;
  std::memcpy(static_cast<void*>(&out), &in, size);
  const auto value = static_cast<sl::DLSSPreset>(preset);
  out.dlaaPreset = out.qualityPreset = out.balancedPreset = out.performancePreset =
      out.ultraPerformancePreset = out.ultraQualityPreset = value;
  return true;
}

enum class Status : uint32_t {
  kNotSeen,     // the game has not called slDLSSSetOptions
  kGame,        // the game's presets are used (nothing selected, or DLSS off)
  kApplied,     // the selected preset was forwarded and accepted
  kUnknownAbi,  // a newer options structure: forwarded unchanged
  kRejected,    // Streamline refused the options; the game's were forwarded
};

inline std::atomic<uint32_t> g_selected{0};
inline std::atomic<bool> g_wrapped{false};
inline std::atomic<uint32_t> g_status{static_cast<uint32_t>(Status::kNotSeen)};
inline std::atomic<uint32_t> g_mode{0};
inline std::atomic<uint32_t> g_game_preset{0};
inline std::atomic<uint32_t> g_applied_preset{0};
// DLSS Ray Reconstruction (plan B.6). Its presets were added in version 3 of
// DLSSDOptions, and sl.dlss_d compares them (and recreates the feature) only
// for a version-3 caller; versions 1 and 2 have no preset to change. The
// selectable presets are D (default transformer), E (transformer) and F (the
// DLSS 4.5 model, offered by the NVIDIA App; Streamline 2.14 headers still
// list F as reverting to the default, which is what an older Ray
// Reconstruction runtime does with it).
inline bool IsSelectableRr(uint32_t preset) {
  return preset == 0 || (preset >= static_cast<uint32_t>(sl::DLSSDPreset::ePresetD) &&
                         preset <= static_cast<uint32_t>(sl::DLSSDPreset::ePresetF));
}

inline sl::DLSSDPreset GamePresetRr(const sl::DLSSDOptions& options) {
  if (options.structVersion < sl::kStructVersion3) return sl::DLSSDPreset::eDefault;
  switch (options.mode) {
    case sl::DLSSMode::eMaxPerformance: return options.performancePreset;
    case sl::DLSSMode::eBalanced: return options.balancedPreset;
    case sl::DLSSMode::eMaxQuality: return options.qualityPreset;
    case sl::DLSSMode::eUltraPerformance: return options.ultraPerformancePreset;
    case sl::DLSSMode::eUltraQuality: return options.ultraQualityPreset;
    case sl::DLSSMode::eDLAA: return options.dlaaPreset;
    default: return sl::DLSSDPreset::eDefault;
  }
}

// As Override, for a version-3 DLSSDOptions only.
inline bool OverrideRr(const sl::DLSSDOptions& in, uint32_t preset, sl::DLSSDOptions& out) {
  if (preset == 0 || !IsSelectableRr(preset) || in.mode == sl::DLSSMode::eOff ||
      in.structVersion != sl::kStructVersion3)
    return false;
  std::memcpy(static_cast<void*>(&out), &in, sizeof(out));
  const auto value = static_cast<sl::DLSSDPreset>(preset);
  out.dlaaPreset = out.qualityPreset = out.balancedPreset = out.performancePreset =
      out.ultraPerformancePreset = out.ultraQualityPreset = value;
  return true;
}

inline std::atomic<uint32_t> g_rr_selected{0};
inline std::atomic<bool> g_rr_wrapped{false};
inline std::atomic<uint32_t> g_rr_status{static_cast<uint32_t>(Status::kNotSeen)};
inline std::atomic<uint32_t> g_rr_version{0};
inline std::atomic<uint32_t> g_rr_game_preset{0};
inline std::atomic<uint32_t> g_rr_applied_preset{0};

// Games that create DLSS through NGX themselves (plan B.3): a feature
// creation was seen, and the preset given to the last one (0 = the game's).
inline std::atomic<bool> g_ngx_seen{false};
inline std::atomic<uint32_t> g_ngx_preset{0};

}  // namespace mfgunlock::dlsspreset
