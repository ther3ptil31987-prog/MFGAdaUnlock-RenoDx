/* SPDX-License-Identifier: MIT
 * Pure construction and classification helpers for DLSS-G memory telemetry.
 * No API calls, allocation ownership changes, or resource retention.
 */
#pragma once

#include <cstring>
#include <cstdint>

#include <sl_dlss_g.h>

namespace mfgunlock::memorypolicy {

enum class GraphicsApi : uint32_t {
  kUnknown = 0,
  kD3D11,
  kD3D12,
  kVulkan,
};

inline constexpr bool SupportsDxgiBudget(GraphicsApi api) {
  return api == GraphicsApi::kD3D11 || api == GraphicsApi::kD3D12;
}

enum class EstimateReadiness : uint32_t {
  kReady = 0,
  kMissingOptions,
  kMissingColor,
  kMissingMotionVectors,
  kMissingDepth,
  kMissingBackBuffers,
  kMissingUiInputs,
};

struct TextureInfo {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
  uint32_t lifecycle = 0;
  bool present = false;
};

struct EstimateInputs {
  uint32_t viewport = 0;
  bool options_valid = false;
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
  float dynamic_target_fps = 0.0f;
  TextureInfo swapchain{};
  TextureInfo motion{};
  TextureInfo depth{};
  TextureInfo hudless{};
  TextureInfo ui{};
};

struct EstimatePlan {
  sl::DLSSGOptions options{};
  EstimateReadiness readiness = EstimateReadiness::kMissingOptions;
  uint64_t signature = 0;
  uint64_t comparison_signature = 0;
  uint32_t volatile_inputs = 0;
  bool ui_recomposition = false;
};

inline uint64_t HashValue(uint64_t hash, uint64_t value) {
  for (unsigned int byte = 0; byte < 8; ++byte) {
    hash ^= static_cast<uint8_t>(value >> (byte * 8));
    hash *= 1099511628211ull;
  }
  return hash;
}

inline uint32_t FloatBits(float value) {
  static_assert(sizeof(float) == sizeof(uint32_t));
  uint32_t result = 0;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

inline float BitsFloat(uint32_t value) {
  float result = 0.0f;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

inline uint64_t OptionsSignature(const sl::DLSSGOptions& options,
                                 bool include_ui) {
  uint64_t hash = 1469598103934665603ull;
  hash = HashValue(hash, static_cast<uint32_t>(options.mode));
  hash = HashValue(hash, options.numFramesToGenerate);
  const uint32_t request_bit =
      static_cast<uint32_t>(sl::DLSSGFlags::eRequestVRAMEstimate);
  hash = HashValue(hash, static_cast<uint32_t>(options.flags) & ~request_bit);
  hash = HashValue(hash, options.dynamicResWidth);
  hash = HashValue(hash, options.dynamicResHeight);
  hash = HashValue(hash, options.numBackBuffers);
  hash = HashValue(hash, options.mvecDepthWidth);
  hash = HashValue(hash, options.mvecDepthHeight);
  hash = HashValue(hash, options.colorWidth);
  hash = HashValue(hash, options.colorHeight);
  hash = HashValue(hash, options.colorBufferFormat);
  hash = HashValue(hash, options.mvecBufferFormat);
  hash = HashValue(hash, options.depthBufferFormat);
  hash = HashValue(hash, static_cast<uint32_t>(options.queueParallelismMode));
  hash = HashValue(hash, FloatBits(options.dynamicTargetFrameRate));
  if (include_ui) {
    hash = HashValue(hash, options.hudLessBufferFormat);
    hash = HashValue(hash, options.uiBufferFormat);
    hash = HashValue(
        hash, static_cast<uint32_t>(options.enableUserInterfaceRecomposition));
  }
  return hash;
}

inline uint32_t PreferredWidth(const TextureInfo& resource) {
  return resource.present ? resource.width : 0;
}

inline uint32_t PreferredHeight(const TextureInfo& resource) {
  return resource.present ? resource.height : 0;
}

inline uint32_t PreferredFormat(const TextureInfo& resource) {
  return resource.present ? resource.format : 0;
}

inline EstimatePlan BuildEstimatePlan(const EstimateInputs& input) {
  EstimatePlan result{};
  if (!input.options_valid) return result;

  result.options.mode = static_cast<sl::DLSSGMode>(input.mode);
  result.options.numFramesToGenerate = input.generated_frames;
  result.options.flags = static_cast<sl::DLSSGFlags>(input.flags);
  result.options.dynamicResWidth = input.dynamic_width;
  result.options.dynamicResHeight = input.dynamic_height;
  result.options.numBackBuffers = input.back_buffers;
  result.options.mvecDepthWidth =
      input.mvec_depth_width != 0
          ? input.mvec_depth_width
          : (PreferredWidth(input.motion) != 0
                 ? PreferredWidth(input.motion)
                 : PreferredWidth(input.depth));
  result.options.mvecDepthHeight =
      input.mvec_depth_height != 0
          ? input.mvec_depth_height
          : (PreferredHeight(input.motion) != 0
                 ? PreferredHeight(input.motion)
                 : PreferredHeight(input.depth));
  result.options.colorWidth =
      input.color_width != 0 ? input.color_width
                             : PreferredWidth(input.swapchain);
  result.options.colorHeight =
      input.color_height != 0 ? input.color_height
                              : PreferredHeight(input.swapchain);
  result.options.colorBufferFormat =
      input.color_format != 0 ? input.color_format
                              : PreferredFormat(input.swapchain);
  result.options.mvecBufferFormat =
      input.mvec_format != 0 ? input.mvec_format
                             : PreferredFormat(input.motion);
  result.options.depthBufferFormat =
      input.depth_format != 0 ? input.depth_format
                              : PreferredFormat(input.depth);
  result.options.hudLessBufferFormat =
      input.hudless_format != 0 ? input.hudless_format
                                : PreferredFormat(input.hudless);
  result.options.uiBufferFormat =
      input.ui_format != 0 ? input.ui_format : PreferredFormat(input.ui);
  result.options.queueParallelismMode =
      static_cast<sl::DLSSGQueueParallelismMode>(
          input.queue_parallelism_mode);
  result.ui_recomposition = input.ui_recomposition == 1;
  result.options.enableUserInterfaceRecomposition =
      result.ui_recomposition ? sl::Boolean::eTrue : sl::Boolean::eFalse;
  result.options.dynamicTargetFrameRate = input.dynamic_target_fps;

  const auto volatile_resource = [](const TextureInfo& resource) {
    return resource.present &&
           resource.lifecycle !=
               static_cast<uint32_t>(sl::ResourceLifecycle::eValidUntilPresent);
  };
  result.volatile_inputs += volatile_resource(input.motion);
  result.volatile_inputs += volatile_resource(input.depth);
  if (result.ui_recomposition) {
    result.volatile_inputs += volatile_resource(input.hudless);
    result.volatile_inputs += volatile_resource(input.ui);
  }

  if (result.options.colorWidth == 0 || result.options.colorHeight == 0 ||
      result.options.colorBufferFormat == 0) {
    result.readiness = EstimateReadiness::kMissingColor;
  } else if (result.options.mvecDepthWidth == 0 ||
             result.options.mvecDepthHeight == 0 ||
             result.options.mvecBufferFormat == 0) {
    result.readiness = EstimateReadiness::kMissingMotionVectors;
  } else if (result.options.depthBufferFormat == 0) {
    result.readiness = EstimateReadiness::kMissingDepth;
  } else if (result.options.numBackBuffers == 0) {
    result.readiness = EstimateReadiness::kMissingBackBuffers;
  } else if (result.ui_recomposition &&
             (result.options.hudLessBufferFormat == 0 ||
              result.options.uiBufferFormat == 0)) {
    result.readiness = EstimateReadiness::kMissingUiInputs;
  } else {
    result.readiness = EstimateReadiness::kReady;
  }
  result.signature = OptionsSignature(result.options, true);
  result.comparison_signature = OptionsSignature(result.options, false);
  return result;
}

}  // namespace mfgunlock::memorypolicy
