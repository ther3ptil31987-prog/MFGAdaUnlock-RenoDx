// SPDX-License-Identifier: MIT
#pragma once
#include <sl.h>
#include <xefg_swapchain.h>
#include <cmath>
#include <cstring>
namespace mfgunlock::xemfg::native {
enum ConstantIssue : uint32_t {
  ConstantMetadata=1, ConstantCameraMotion=2, ConstantMotion3D=4,
  ConstantDepth=8, ConstantJitterMode=16, ConstantReset=32,
  ConstantView=64, ConstantProjection=128, ConstantJitter=256, ConstantMotionScale=512
};
inline bool ConstantFinite(float value) noexcept { return std::isfinite(value) && std::abs(value)<1.e30f; }
inline uint32_t TranslateConstants(const sl::Constants& c,bool inverted,bool jittered,
    xefg_swapchain_frame_constant_data_t& data) noexcept {
  // Streamline structures are append-only: a newer Constants starts with the
  // V1/V2 fields read here. A foreign type or version zero is still refused.
  if(c.structType!=sl::Constants::s_structType || c.structVersion<1) return ConstantMetadata;
  uint32_t issues{};
  if(c.cameraMotionIncluded!=sl::eTrue) issues|=ConstantCameraMotion;
  if(c.motionVectors3D!=sl::eFalse) issues|=ConstantMotion3D;
  if(c.depthInverted==sl::eInvalid || (c.depthInverted==sl::eTrue)!=inverted) issues|=ConstantDepth;
  if(c.motionVectorsJittered==sl::eInvalid || (c.motionVectorsJittered==sl::eTrue)!=jittered) issues|=ConstantJitterMode;
  if(c.reset!=sl::eTrue && c.reset!=sl::eFalse) issues|=ConstantReset;
  memcpy(data.projectionMatrix,&c.cameraViewToClip,sizeof(data.projectionMatrix));
  const sl::float3 basis[]={c.cameraRight,c.cameraUp,c.cameraFwd};
  for(unsigned col=0;col<3;++col) {
    const auto b=basis[col];
    data.viewMatrix[col]=b.x; data.viewMatrix[4+col]=b.y; data.viewMatrix[8+col]=b.z;
    data.viewMatrix[12+col]=-(b.x*c.cameraPos.x+b.y*c.cameraPos.y+b.z*c.cameraPos.z);
  }
  data.viewMatrix[3]=data.viewMatrix[7]=data.viewMatrix[11]=0; data.viewMatrix[15]=1;
  data.jitterOffsetX=c.jitterOffset.x; data.jitterOffsetY=c.jitterOffset.y;
  data.motionVectorScaleX=c.mvecScale.x; data.motionVectorScaleY=c.mvecScale.y;
  data.resetHistory=c.reset==sl::eTrue; data.frameRenderTime=0;
  for(float v:data.viewMatrix) if(!ConstantFinite(v)) issues|=ConstantView;
  for(float v:data.projectionMatrix) if(!ConstantFinite(v)) issues|=ConstantProjection;
  if(!ConstantFinite(data.jitterOffsetX) || !ConstantFinite(data.jitterOffsetY)) issues|=ConstantJitter;
  if(!ConstantFinite(data.motionVectorScaleX) || !ConstantFinite(data.motionVectorScaleY)) issues|=ConstantMotionScale;
  return issues;
}
}
