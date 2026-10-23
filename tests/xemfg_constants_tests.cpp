// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/native_constants.hpp"
#include "../src/addons/mfgunlock/xemfg/native_input_policy.hpp"
#include <limits>
#include <iostream>
#include <stdexcept>
using namespace mfgunlock::xemfg::native;
void Require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try {
  sl::Resource resource;
  Require(ResourceHeaderSupported(resource),"known resource header rejected");
  resource.structVersion=0;Require(ResourceHeaderSupported(resource),"known GUID with empty version rejected");
  resource.structType={};Require(ResourceHeaderSupported(resource),"empty resource header rejected");
  resource.structVersion=2;Require(!ResourceHeaderSupported(resource),"future resource ABI inferred from empty GUID");
  resource.structVersion=1;Require(!ResourceHeaderSupported(resource),"wrong typed resource accepted");
  resource.structVersion=0;resource.next=&resource;Require(!ResourceHeaderSupported(resource),"unversioned chain accepted");
  resource.next=nullptr;resource.structType.data1=123;Require(!ResourceHeaderSupported(resource),"foreign unversioned resource accepted");
  sl::Resource newer; newer.structVersion=2;Require(ResourceHeaderSupported(newer),"newer known resource not read through its prefix");
  sl::Constants c;
  c.cameraPos={2,3,4};c.cameraRight={1,0,0};c.cameraUp={0,1,0};c.cameraFwd={0,0,1};
  for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j) reinterpret_cast<float*>(&c.cameraViewToClip)[i*4+j]=i==j?1.f:0.f;
  c.cameraMotionIncluded=sl::eTrue;c.motionVectors3D=sl::eFalse;c.depthInverted=sl::eTrue;
  c.motionVectorsJittered=sl::eFalse;c.reset=sl::eFalse;c.mvecScale={-1.f/1280,1.f/720};c.jitterOffset={.25f,-.5f};
  xefg_swapchain_frame_constant_data_t data{};
  Require(TranslateConstants(c,true,false,data)==0,"valid camera rejected");
  Require(data.viewMatrix[12]==-2 && data.viewMatrix[13]==-3 && data.viewMatrix[14]==-4,"view translation");
  Require(data.motionVectorScaleX==c.mvecScale.x && data.jitterOffsetY==-.5f,"motion/jitter changed");
  for(const auto version:{1u,2u}) {c.structVersion=version;Require(TranslateConstants(c,true,false,data)==0,"known ABI rejected");}
  // Append-only ABI: a newer Constants is read through its known prefix; a
  // foreign type or version zero is still refused.
  c.structVersion=3;Require(TranslateConstants(c,true,false,data)==0,"newer ABI not read through its prefix");
  c.structVersion=0;Require(TranslateConstants(c,true,false,data)==ConstantMetadata,"versionless constants accepted");
  c.structType.data1^=1;c.structVersion=2;Require(TranslateConstants(c,true,false,data)==ConstantMetadata,"foreign constants accepted");
  c.structType.data1^=1;
  c.cameraMotionIncluded=sl::eFalse;Require(TranslateConstants(c,true,false,data)&ConstantCameraMotion,"camera motion guessed");c.cameraMotionIncluded=sl::eTrue;
  Require(TranslateConstants(c,false,false,data)==ConstantDepth,"depth init mismatch unreported");
  Require(TranslateConstants(c,true,true,data)==ConstantJitterMode,"jitter init mismatch unreported");
  c.cameraPos.x=std::numeric_limits<float>::quiet_NaN();Require(TranslateConstants(c,true,false,data)&ConstantView,"NaN camera accepted");c.cameraPos.x=2;
  c.mvecScale.x=sl::INVALID_FLOAT;Require(TranslateConstants(c,true,false,data)&ConstantMotionScale,"unset scale accepted");
  std::cout<<"PASS: constants ABI, signed scales, camera transform and independent rejection reasons\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
