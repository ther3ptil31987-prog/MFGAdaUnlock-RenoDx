/* SPDX-License-Identifier: MIT
 * Opt-in, read-only summary of game-submitted Streamline DLSS-G inputs.
 * No GPU readback, resource retention, address reporting, or per-frame log.
 */
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <sl_consts.h>
#include <sl_core_types.h>
#include <sl_dlss_g.h>

namespace mfgunlock::inputdiag {

inline constexpr size_t kMaxViewports = 8;
inline constexpr uint32_t kMaxTagsPerBatch = 64;
inline constexpr uint32_t kUnusedViewport = (std::numeric_limits<uint32_t>::max)();

enum class Kind : size_t {
  Backbuffer,
  Hudless,
  UiColorAlpha,
  UiAlpha,
  MotionVectors,
  Depth,
  Exposure,
  Count,
};
enum class NativeApi : uint32_t { Unknown, D3D11, D3D12, Vulkan };
inline std::atomic<NativeApi> g_native_api{NativeApi::Unknown};
inline constexpr size_t kKindCount = static_cast<size_t>(Kind::Count);

inline constexpr const char* KindName(Kind kind) {
  switch (kind) {
    case Kind::Backbuffer: return "Backbuffer";
    case Kind::Hudless: return "HUD-less color";
    case Kind::UiColorAlpha: return "UI color + alpha";
    case Kind::UiAlpha: return "UI alpha";
    case Kind::MotionVectors: return "Motion vectors";
    case Kind::Depth: return "Depth";
    case Kind::Exposure: return "Exposure";
    default: return "Unknown";
  }
}

inline int KindIndex(sl::BufferType type) {
  if (type == sl::kBufferTypeBackbuffer) return static_cast<int>(Kind::Backbuffer);
  if (type == sl::kBufferTypeHUDLessColor) return static_cast<int>(Kind::Hudless);
  if (type == sl::kBufferTypeUIColorAndAlpha) return static_cast<int>(Kind::UiColorAlpha);
  if (type == sl::kBufferTypeUIAlpha) return static_cast<int>(Kind::UiAlpha);
  if (type == sl::kBufferTypeMotionVectors) return static_cast<int>(Kind::MotionVectors);
  if (type == sl::kBufferTypeDepth) return static_cast<int>(Kind::Depth);
  if (type == sl::kBufferTypeExposure) return static_cast<int>(Kind::Exposure);
  return -1;
}

struct TagSummary {
  uint64_t sets = 0;
  uint64_t clears = 0;
  uint64_t invalid_base_metadata = 0;
  uint64_t metadata_changes = 0;
  uint64_t native_desc_changes = 0;
  uint64_t native_identity_changes = 0;
  uint32_t width = 0;   // Tagged extent, or whole resource if no extent.
  uint32_t height = 0;
  uint32_t resource_width = 0;
  uint32_t resource_height = 0;
  uint32_t native_format = 0;
  uint32_t native_desc_width = 0;
  uint32_t native_desc_height = 0;
  uint32_t native_desc_format = 0;
  uint32_t lifecycle = 0;
  uint32_t last_frame = 0;
  bool frame_known = false;
  bool has_resource = false;
  bool native_desc_seen = false;
};

struct ConstantsSummary {
  uint64_t calls = 0;
  uint64_t invalid_base_metadata = 0;
  uint64_t game_reset_true = 0;
  uint32_t last_frame = 0;
  uint32_t struct_version = 0;
  int depth_inverted = -1;
  int camera_motion_included = -1;
  int motion_vectors_3d = -1;
  int motion_vectors_dilated = -1;
  int motion_vectors_jittered = -1;
  float mvec_scale_x = sl::INVALID_FLOAT;
  float mvec_scale_y = sl::INVALID_FLOAT;
  float jitter_x = sl::INVALID_FLOAT;
  float jitter_y = sl::INVALID_FLOAT;
};

struct OptionsSummary {
  uint64_t calls = 0;
  uint64_t changes = 0;
  uint64_t invalid_base_metadata = 0;
  uint32_t struct_version = 0;
  uint32_t mode = 0;
  uint32_t generated_frames = 0;
  uint32_t flags = 0;
  uint32_t dynamic_width = 0;
  uint32_t dynamic_height = 0;
  uint32_t back_buffers = 0;
  uint32_t mvec_depth_width = 0;
  uint32_t mvec_depth_height = 0;
  uint32_t color_width = 0;
  uint32_t color_height = 0;
  uint32_t color_format = 0;
  uint32_t mvec_format = 0;
  uint32_t depth_format = 0;
  uint32_t hudless_format = 0;
  uint32_t ui_format = 0;
  uint32_t queue_parallelism_mode = 0;
  int ui_recomposition = -1;
  float dynamic_target_fps = sl::INVALID_FLOAT;
  bool valid = false;
};

struct ViewportSnapshot {
  uint32_t viewport = kUnusedViewport;
  uint32_t format_api = 0; // 0 unknown, 1 DXGI, 2 Vulkan.
  NativeApi native_api = NativeApi::Unknown;
  uint64_t tag_batches = 0;
  uint64_t frame_aware_batches = 0;
  std::array<TagSummary, kKindCount> tags{};
  ConstantsSummary constants{};
  OptionsSummary game_options{};
  OptionsSummary forwarded_options{};
};

struct OutputSnapshot {
  uint64_t samples = 0;
  uint64_t changes = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
  uint32_t color_space = 0;
  bool valid = false;
};

struct State {
  SRWLOCK lock = SRWLOCK_INIT;
  std::atomic<uint32_t> key{kUnusedViewport};
  ViewportSnapshot snapshot{};
  // Native handles are used only to count changes. Never exposed in snapshots.
  std::array<uintptr_t, kKindCount> last_native{};
};

inline std::array<State, kMaxViewports> g_states{};
inline SRWLOCK g_output_lock = SRWLOCK_INIT;
inline OutputSnapshot g_output{};
inline std::atomic_bool g_enabled{false};
inline std::atomic_bool g_viewport_overflow{false};
inline std::atomic<uint64_t> g_dropped_contention{0};
inline std::atomic<uint64_t> g_truncated_batches{0};

inline State* FindOrClaim(uint32_t viewport) {
  if (viewport == kUnusedViewport) return nullptr;
  for (auto& state : g_states) {
    if (state.key.load(std::memory_order_acquire) == viewport) return &state;
  }
  for (auto& state : g_states) {
    uint32_t unused = kUnusedViewport;
    if (state.key.compare_exchange_strong(unused, viewport,
                                          std::memory_order_acq_rel)) {
      return &state;
    }
    if (unused == viewport) return &state;
  }
  g_viewport_overflow.store(true, std::memory_order_relaxed);
  return nullptr;
}

inline bool ValidBaseTag(const sl::ResourceTag& tag) {
  return tag.structType == sl::ResourceTag::s_structType &&
         tag.structVersion == sl::kStructVersion1 &&
         tag.lifecycle >= sl::eOnlyValidNow &&
         tag.lifecycle <= sl::eValidUntilEvaluate;
}

inline bool ValidBaseResource(const sl::Resource& resource) {
  return resource.structType == sl::Resource::s_structType &&
         resource.structVersion == sl::kStructVersion1 &&
         resource.native != nullptr;
}

inline bool DescribeNativeTexture(const sl::Resource& resource,
                                  NativeApi api, uint32_t& width,
                                  uint32_t& height, uint32_t& format) {
  if (resource.type != sl::ResourceType::eTex2d || resource.native == nullptr)
    return false;
  if (api == NativeApi::D3D12) {
    const D3D12_RESOURCE_DESC desc =
        reinterpret_cast<ID3D12Resource*>(resource.native)->GetDesc();
    width = desc.Width > UINT32_MAX ? UINT32_MAX
                                    : static_cast<uint32_t>(desc.Width);
    height = desc.Height;
    format = static_cast<uint32_t>(desc.Format);
    return true;
  }
  if (api == NativeApi::D3D11) {
    D3D11_TEXTURE2D_DESC desc{};
    reinterpret_cast<ID3D11Texture2D*>(resource.native)->GetDesc(&desc);
    width = desc.Width;
    height = desc.Height;
    format = static_cast<uint32_t>(desc.Format);
    return true;
  }
  return false;
}

inline void ObserveTags(uint32_t viewport, const sl::ResourceTag* tags,
                        uint32_t count, bool frame_known, uint32_t frame,
                        uint32_t format_api) {
  if (!g_enabled.load(std::memory_order_relaxed) || tags == nullptr || count == 0)
    return;
  State* state = FindOrClaim(viewport);
  if (state == nullptr) return;
  if (!TryAcquireSRWLockExclusive(&state->lock)) {
    g_dropped_contention.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (!g_enabled.load(std::memory_order_relaxed)) {
    ReleaseSRWLockExclusive(&state->lock);
    return;
  }
  auto& snapshot = state->snapshot;
  snapshot.viewport = viewport;
  snapshot.format_api = format_api;
  snapshot.native_api = g_native_api.load(std::memory_order_relaxed);
  ++snapshot.tag_batches;
  if (frame_known) ++snapshot.frame_aware_batches;
  if (count > kMaxTagsPerBatch)
    g_truncated_batches.fetch_add(1, std::memory_order_relaxed);
  const uint32_t observed = (std::min)(count, kMaxTagsPerBatch);
  for (uint32_t i = 0; i < observed; ++i) {
    const sl::ResourceTag& tag = tags[i];
    const int index = KindIndex(tag.type);
    if (index < 0) continue;
    const size_t slot = static_cast<size_t>(index);
    auto& summary = snapshot.tags[slot];
    summary.frame_known = frame_known;
    summary.last_frame = frame;
    bool invalid = !ValidBaseTag(tag) ||
                   ((tag.extent.width == 0) != (tag.extent.height == 0));
    if (tag.resource == nullptr) {
      summary.invalid_base_metadata += invalid;
      ++summary.clears;
      summary.has_resource = false;
      state->last_native[slot] = 0;
      continue;
    }
    ++summary.sets;
    const sl::Resource& resource = *tag.resource;
    const bool resource_valid = ValidBaseResource(resource);
    invalid = invalid || !resource_valid;
    const uint32_t width = tag.extent.width != 0 ? tag.extent.width : resource.width;
    const uint32_t height = tag.extent.height != 0 ? tag.extent.height : resource.height;
    if (tag.extent.width != 0 && tag.extent.height != 0 &&
        resource.width != 0 && resource.height != 0 &&
        (uint64_t(tag.extent.left) + tag.extent.width > resource.width ||
         uint64_t(tag.extent.top) + tag.extent.height > resource.height)) {
      invalid = true;
    }
    summary.invalid_base_metadata += invalid;
    uint32_t native_width = 0;
    uint32_t native_height = 0;
    uint32_t native_format = 0;
    const bool native_desc_seen =
        resource_valid &&
        DescribeNativeTexture(resource, snapshot.native_api, native_width,
                              native_height, native_format);
    const uint32_t lifecycle = static_cast<uint32_t>(tag.lifecycle);
    if (summary.has_resource &&
        (summary.width != width || summary.height != height ||
         summary.resource_width != resource.width ||
         summary.resource_height != resource.height ||
         summary.native_format != resource.nativeFormat ||
         summary.lifecycle != lifecycle)) {
      ++summary.metadata_changes;
    }
    if (summary.native_desc_seen && native_desc_seen &&
        (summary.native_desc_width != native_width ||
         summary.native_desc_height != native_height ||
         summary.native_desc_format != native_format)) {
      ++summary.native_desc_changes;
    }
    const uintptr_t native = reinterpret_cast<uintptr_t>(resource.native);
    if (summary.has_resource && state->last_native[slot] != 0 &&
        native != 0 && state->last_native[slot] != native) {
      ++summary.native_identity_changes;
    }
    summary.width = width;
    summary.height = height;
    summary.resource_width = resource.width;
    summary.resource_height = resource.height;
    summary.native_format = resource.nativeFormat;
    if (native_desc_seen) {
      summary.native_desc_width = native_width;
      summary.native_desc_height = native_height;
      summary.native_desc_format = native_format;
      summary.native_desc_seen = true;
    }
    summary.lifecycle = lifecycle;
    summary.has_resource = true;
    state->last_native[slot] = native;
  }
  ReleaseSRWLockExclusive(&state->lock);
}

inline void ObserveConstants(uint32_t viewport, const sl::Constants& values,
                             uint32_t frame) {
  if (!g_enabled.load(std::memory_order_relaxed)) return;
  State* state = FindOrClaim(viewport);
  if (state == nullptr) return;
  if (!TryAcquireSRWLockExclusive(&state->lock)) {
    g_dropped_contention.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (!g_enabled.load(std::memory_order_relaxed)) {
    ReleaseSRWLockExclusive(&state->lock);
    return;
  }
  state->snapshot.viewport = viewport;
  auto& summary = state->snapshot.constants;
  ++summary.calls;
  if (values.structType != sl::Constants::s_structType ||
      values.structVersion < sl::kStructVersion1) {
    ++summary.invalid_base_metadata;
    ReleaseSRWLockExclusive(&state->lock);
    return;
  }
  summary.last_frame = frame;
  summary.struct_version = values.structVersion;
  summary.game_reset_true += values.reset == sl::Boolean::eTrue;
  summary.depth_inverted = static_cast<int>(values.depthInverted);
  summary.camera_motion_included = static_cast<int>(values.cameraMotionIncluded);
  summary.motion_vectors_3d = static_cast<int>(values.motionVectors3D);
  summary.motion_vectors_dilated = static_cast<int>(values.motionVectorsDilated);
  summary.motion_vectors_jittered = static_cast<int>(values.motionVectorsJittered);
  summary.mvec_scale_x = values.mvecScale.x;
  summary.mvec_scale_y = values.mvecScale.y;
  summary.jitter_x = values.jitterOffset.x;
  summary.jitter_y = values.jitterOffset.y;
  ReleaseSRWLockExclusive(&state->lock);
}

inline bool SameOptions(const OptionsSummary& previous,
                        const OptionsSummary& current) {
  return previous.struct_version == current.struct_version &&
         previous.mode == current.mode &&
         previous.generated_frames == current.generated_frames &&
         previous.flags == current.flags &&
         previous.dynamic_width == current.dynamic_width &&
         previous.dynamic_height == current.dynamic_height &&
         previous.back_buffers == current.back_buffers &&
         previous.mvec_depth_width == current.mvec_depth_width &&
         previous.mvec_depth_height == current.mvec_depth_height &&
         previous.color_width == current.color_width &&
         previous.color_height == current.color_height &&
         previous.color_format == current.color_format &&
         previous.mvec_format == current.mvec_format &&
         previous.depth_format == current.depth_format &&
         previous.hudless_format == current.hudless_format &&
         previous.ui_format == current.ui_format &&
         previous.queue_parallelism_mode == current.queue_parallelism_mode &&
         previous.ui_recomposition == current.ui_recomposition &&
         previous.dynamic_target_fps == current.dynamic_target_fps;
}

inline void ObserveOptions(uint32_t viewport, const sl::DLSSGOptions& values,
                           bool forwarded) {
  if (!g_enabled.load(std::memory_order_relaxed)) return;
  State* state = FindOrClaim(viewport);
  if (state == nullptr) return;
  if (!TryAcquireSRWLockExclusive(&state->lock)) {
    g_dropped_contention.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (!g_enabled.load(std::memory_order_relaxed)) {
    ReleaseSRWLockExclusive(&state->lock);
    return;
  }
  state->snapshot.viewport = viewport;
  auto& summary = forwarded ? state->snapshot.forwarded_options
                            : state->snapshot.game_options;
  ++summary.calls;
  if (values.structType != sl::DLSSGOptions::s_structType ||
      values.structVersion < sl::kStructVersion1 ||
      values.structVersion > sl::kStructVersion5) {
    ++summary.invalid_base_metadata;
    ReleaseSRWLockExclusive(&state->lock);
    return;
  }

  OptionsSummary current{};
  current.valid = true;
  current.struct_version = values.structVersion;
  current.mode = static_cast<uint32_t>(values.mode);
  current.generated_frames = values.numFramesToGenerate;
  current.flags = static_cast<uint32_t>(values.flags);
  current.dynamic_width = values.dynamicResWidth;
  current.dynamic_height = values.dynamicResHeight;
  current.back_buffers = values.numBackBuffers;
  current.mvec_depth_width = values.mvecDepthWidth;
  current.mvec_depth_height = values.mvecDepthHeight;
  current.color_width = values.colorWidth;
  current.color_height = values.colorHeight;
  current.color_format = values.colorBufferFormat;
  current.mvec_format = values.mvecBufferFormat;
  current.depth_format = values.depthBufferFormat;
  current.hudless_format = values.hudLessBufferFormat;
  current.ui_format = values.uiBufferFormat;
  if (values.structVersion >= sl::kStructVersion3)
    current.queue_parallelism_mode =
        static_cast<uint32_t>(values.queueParallelismMode);
  if (values.structVersion >= sl::kStructVersion4)
    current.ui_recomposition = static_cast<int>(values.enableUserInterfaceRecomposition);
  if (values.structVersion >= sl::kStructVersion5)
    current.dynamic_target_fps = values.dynamicTargetFrameRate;

  if (summary.valid && !SameOptions(summary, current)) ++summary.changes;
  const uint64_t calls = summary.calls;
  const uint64_t changes = summary.changes;
  const uint64_t invalid = summary.invalid_base_metadata;
  summary = current;
  summary.calls = calls;
  summary.changes = changes;
  summary.invalid_base_metadata = invalid;
  ReleaseSRWLockExclusive(&state->lock);
}

inline size_t SnapshotAll(std::array<ViewportSnapshot, kMaxViewports>& output) {
  size_t count = 0;
  for (auto& state : g_states) {
    if (state.key.load(std::memory_order_acquire) == kUnusedViewport) continue;
    if (!TryAcquireSRWLockShared(&state.lock)) continue;
    output[count++] = state.snapshot;
    ReleaseSRWLockShared(&state.lock);
  }
  return count;
}

inline void ObserveOutput(uint32_t width, uint32_t height, uint32_t format,
                          uint32_t color_space) {
  if (!g_enabled.load(std::memory_order_relaxed)) return;
  if (!TryAcquireSRWLockExclusive(&g_output_lock)) {
    g_dropped_contention.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (!g_enabled.load(std::memory_order_relaxed)) {
    ReleaseSRWLockExclusive(&g_output_lock);
    return;
  }
  ++g_output.samples;
  if (g_output.valid &&
      (g_output.width != width || g_output.height != height ||
       g_output.format != format || g_output.color_space != color_space)) {
    ++g_output.changes;
  }
  g_output.width = width;
  g_output.height = height;
  g_output.format = format;
  g_output.color_space = color_space;
  g_output.valid = width != 0 && height != 0 && color_space != 0;
  ReleaseSRWLockExclusive(&g_output_lock);
}

inline bool SnapshotOutput(OutputSnapshot& output) {
  if (!TryAcquireSRWLockShared(&g_output_lock)) return false;
  output = g_output;
  ReleaseSRWLockShared(&g_output_lock);
  return output.valid;
}

inline void Clear() {
  const bool was_enabled = g_enabled.exchange(false, std::memory_order_acq_rel);
  for (auto& state : g_states) {
    AcquireSRWLockExclusive(&state.lock);
    state.snapshot = {};
    state.snapshot.viewport = state.key.load(std::memory_order_relaxed);
    state.last_native = {};
    ReleaseSRWLockExclusive(&state.lock);
  }
  g_dropped_contention.store(0, std::memory_order_relaxed);
  g_truncated_batches.store(0, std::memory_order_relaxed);
  g_viewport_overflow.store(false, std::memory_order_relaxed);
  AcquireSRWLockExclusive(&g_output_lock);
  g_output = {};
  ReleaseSRWLockExclusive(&g_output_lock);
  g_enabled.store(was_enabled, std::memory_order_release);
}

}  // namespace mfgunlock::inputdiag
