// SPDX-License-Identifier: MIT
#include "native_frame_packet.hpp"
#include "native_constants.hpp"

namespace mfgunlock::xemfg::native::packet {
namespace {
// Plan 4.2: jittered motion is held for conversion at Present instead of
// being tagged; a volatile tag is copied now, on the producer's list.
xefg_swapchain_result_t StageMotion(Output& o,Frame& frame,ID3D12CommandList* producer,ID3D12Resource* resource,
    const xefg_swapchain_d3d12_resource_data_t& data,uint32_t id) {
  const DejitterRegion region{uint32_t(data.resourceBase.x),uint32_t(data.resourceBase.y),data.resourceSize.x,data.resourceSize.y};
  if(data.validity!=XEFG_SWAPCHAIN_RV_ONLY_NOW) {
    frame.motionSource=resource; frame.motionState=data.incomingState; frame.motionRegion=region;
    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
  }
  ComPtr<ID3D12GraphicsCommandList> graphics; ComPtr<ID3D12Resource> staged;
  if(!producer || FAILED(producer->QueryInterface(IID_PPV_ARGS(&graphics)))
      || !o.dejitter->Capture(graphics.Get(),resource,data.incomingState,region,id,staged))
    return XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT;
  frame.motionSource=std::move(staged); frame.motionState=D3D12_RESOURCE_STATE_COMMON;
  frame.motionRegion={0,0,region.width,region.height};
  return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}
}
void SubmitBackbufferRegion(Output& o,Frame& frame,uint32_t id,const DejitterRegion& e) {
  // Plan 4.3: only the extent is used; XeFG reads its own swapchain buffer.
  const bool empty=!e.width || !e.height;
  const bool inside=!empty && uint64_t(e.x)+e.width<=o.width && uint64_t(e.y)+e.height<=o.height;
  frame.backbufferRegion=inside?e:DejitterRegion{};
  o.backbufferRegion=frame.backbufferRegion;
  if(!empty && o.backbufferLogBudget<4) {
    ++o.backbufferLogBudget;
    xess_mod::WriteLog("native_backbuffer_region","\"id\":"+std::to_string(id)+",\"accepted\":"+(inside?"true":"false")
        +",\"x\":"+std::to_string(e.x)+",\"y\":"+std::to_string(e.y)+",\"width\":"+std::to_string(e.width)
        +",\"height\":"+std::to_string(e.height)+",\"output_width\":"+std::to_string(o.width)+",\"output_height\":"+std::to_string(o.height));
  }
}
void SubmitResource(Output& o,Frame& frame,uint32_t id,const Resource& t) {
  const auto type=t.type;
  const auto bit=1u<<type;
  auto& receipt=frame.resourceReceipts[uint32_t(type)];
  receipt.observed=true;receipt.lifecycle=t.rawLifecycle;
  const bool optional=type==XEFG_SWAPCHAIN_RES_HUDLESS_COLOR || type==XEFG_SWAPCHAIN_RES_UI;
  const char* rejection="metadata";
  D3D12_RESOURCE_DESC observed{};
  int sdkResult{};
  const auto reject=[&](uint32_t issues) {
    receipt.rejection=rejection;receipt.accepted=false;receipt.sdkResult=sdkResult;
    if(!optional || (frame.resources&bit)) {
      frame.failed=true;
      if(frame.firstFailure==compatibility::Failure::None)frame.firstFailure=compatibility::Failure::InputRejected;
    }
    // Cannot retract an earlier successful tag for this same frame: bypass
    // that batch instead. Otherwise omit only the incompatible optional tag.
    if(optional) {
      frame.hudIssues|=issues; ++State().hudRejected;
      if(o.hudLogBudget<8) { ++o.hudLogBudget; xess_mod::WriteLog("native_hud_filtered","\"id\":"+std::to_string(id)+",\"type\":"+std::to_string(type)+",\"issues\":"+std::to_string(issues)); }
    }
    if(o.rejectedTagLogBudget<12) {
      ++o.rejectedTagLogBudget;
      xess_mod::WriteLog("native_tag_rejected","\"id\":"+std::to_string(id)+",\"type\":"+std::to_string(type)
          +",\"reason\":\""+rejection+"\",\"lifecycle\":"+std::to_string(t.rawLifecycle)
          +",\"tag_version\":"+std::to_string(t.tagVersion)+",\"resource_version\":"+std::to_string(t.resourceVersion)
          +",\"state\":"+std::to_string(t.state)
          +",\"format\":"+std::to_string(observed.Format)+",\"width\":"+std::to_string(observed.Width)
          +",\"height\":"+std::to_string(observed.Height)+",\"array_size\":"+std::to_string(observed.DepthOrArraySize)
          +",\"extent_x\":"+std::to_string(t.extent.x)+",\"extent_y\":"+std::to_string(t.extent.y)
          +",\"extent_width\":"+std::to_string(t.extent.width)+",\"extent_height\":"+std::to_string(t.extent.height)
          +",\"command\":"+std::string(t.producer?"true":"false")+",\"sdk_result\":"+std::to_string(sdkResult));
    }
  };
  if(!t.native) {
    if(frame.resources&bit) { rejection="tag_removed_after_admission"; reject(HudConflict); }
    receipt.accepted=false;receipt.rejection="removed";
    frame.resources&=~bit; return;
  }
  if(!t.headerValid || t.state==UINT_MAX) { reject(HudMetadata); return; }
  ComPtr<ID3D12Resource> resource;
  if(!single_overlay::native::Unwrap(t.native,resource)) {
    rejection="resource_unwrap";
    reject(HudMetadata); return;
  }
  const auto desc=resource->GetDesc();
  observed=desc;
  receipt.width=uint32_t(desc.Width);receipt.height=desc.Height;receipt.format=uint32_t(desc.Format);
  receipt.incomingState=t.state;
  ComPtr<ID3D12Device> device, nativeDevice; resource->GetDevice(IID_PPV_ARGS(&device));
  single_overlay::native::Unwrap(device.Get(),nativeDevice);
  ComPtr<IUnknown> identity, expectedIdentity;
  if(nativeDevice) nativeDevice.As(&identity); o.device.As(&expectedIdentity);
  if(identity.Get()!=expectedIdentity.Get() || desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D
      || desc.SampleDesc.Count!=1 || desc.DepthOrArraySize!=1) { rejection="device_or_shape"; reject(HudMetadata); return; }
  xefg_swapchain_d3d12_resource_data_t data{};
  data.type=type; data.pResource=resource.Get();
  data.incomingState=static_cast<D3D12_RESOURCE_STATES>(t.state);
  data.resourceBase={t.extent.x,t.extent.y};
  data.resourceSize={t.extent.width?t.extent.width:uint32_t(desc.Width),t.extent.height?t.extent.height:desc.Height};
  if(uint64_t(data.resourceBase.x)+data.resourceSize.x>desc.Width
      || uint64_t(data.resourceBase.y)+data.resourceSize.y>desc.Height) { rejection="extent"; reject(HudExtent); return; }
  if(optional) {
    // XeFG requires HUD-less and backbuffer resolutions to match (plan 4.3).
    const auto& area=o.backbufferRegion;
    const auto issues=CheckHudInput(desc,o.format,area.width?area.width:o.width,area.width?area.height:o.height,
        data.resourceBase.x,data.resourceBase.y,data.resourceSize.x,data.resourceSize.y);
    if(issues) { rejection="optional_hud"; reject(issues); return; }
  }
  const unsigned hudSlot=type==XEFG_SWAPCHAIN_RES_UI?1:0;
  ComPtr<IUnknown> allocation;
  HudTagIdentity hudIdentity{};
  if(optional) {
    if(FAILED(resource.As(&allocation))) { rejection="allocation_identity"; reject(HudMetadata); return; }
    hudIdentity={reinterpret_cast<uintptr_t>(allocation.Get()),uint32_t(data.incomingState),t.rawLifecycle,
        data.resourceBase.x,data.resourceBase.y,data.resourceSize.x,data.resourceSize.y};
    if(ConflictingHudTag(frame.hudTags[hudSlot],hudIdentity)) {
      rejection="conflicting_hud_tag"; reject(HudConflict); return;
    }
  }
  ComPtr<ID3D12CommandList> list;
  if(t.lifecycle==Lifecycle::UntilPresent) data.validity=XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
  else if(t.lifecycle==Lifecycle::OnlyNow) {
    // Intel's documented ONLY_NOW contract records the copy at the tag,
    // using the producer's list and restoring its resource state. A COM
    // AddRef alone would not preserve the contents of a volatile resource.
    if(!t.producer || !single_overlay::native::UnwrapProducerCommand(t.producer,list)
        || list->GetType()==D3D12_COMMAND_LIST_TYPE_COPY) { rejection="volatile_command"; reject(HudMetadata); return; }
    ComPtr<ID3D12Device> producerDevice,nativeProducerDevice;
    ComPtr<IUnknown> producerIdentity;
    if(FAILED(list->GetDevice(IID_PPV_ARGS(&producerDevice)))
        || !single_overlay::native::Unwrap(producerDevice.Get(),nativeProducerDevice)
        || FAILED(nativeProducerDevice.As(&producerIdentity)) || producerIdentity!=expectedIdentity) {
      rejection="producer_device"; reject(HudMetadata); return;
    }
    data.validity=XEFG_SWAPCHAIN_RV_ONLY_NOW;
  } else { rejection="lifecycle"; reject(HudMetadata); return; }
  // A second tag of an accepted type in the same frame (games that tag for
  // DLSS-SR and DLSS-G separately): XeFG keeps the first and refuses the
  // rest, so the first is kept here too and the frame is not failed.
  if(frame.resources&bit) {
    receipt.accepted=true; receipt.rejection=nullptr;
    if(o.duplicateTagLogBudget<4) {
      ++o.duplicateTagLogBudget;
      xess_mod::WriteLog("native_tag_duplicate","\"id\":"+std::to_string(id)+",\"type\":"+std::to_string(type)
          +",\"lifecycle\":"+std::to_string(t.rawLifecycle));
    }
    return;
  }
  const auto code=[&] {
    if(type==XEFG_SWAPCHAIN_RES_MOTION_VECTOR && o.dejitter) return StageMotion(o,frame,list.Get(),resource.Get(),data,id);
    RuntimeCall trace("xefg_tag_resource",o.diagnosticOutput,id);
    const auto r=State().api.xefgSwapChainD3D12TagFrameResource(o.fg,list.Get(),id,&data);trace.Result(r);return r;
  }();
  if(id<6) xess_mod::WriteLog("native_tag","\"type\":"+std::to_string(type)+",\"result\":"+std::to_string(code)
      +",\"format\":"+std::to_string(desc.Format)+",\"width\":"+std::to_string(data.resourceSize.x)
      +",\"height\":"+std::to_string(data.resourceSize.y)+",\"lifecycle\":"+std::to_string(t.rawLifecycle));
  if(code<0) { sdkResult=code; rejection="sdk_tag"; frame.failed=true; reject(HudMetadata); }
  else {
    receipt.accepted=true;receipt.rejection=nullptr;receipt.sdkResult=code;
    receipt.width=data.resourceSize.x;receipt.height=data.resourceSize.y;
    frame.resources|=bit;
    if(optional) { frame.hudTags[hudSlot]=hudIdentity; frame.hudAllocations[hudSlot]=std::move(allocation); }
    if(optional && !(o.hudKindsSeen&bit)) {
      o.hudKindsSeen|=bit;
      xess_mod::WriteLog("native_hud_input","\"type\":"+std::to_string(type)+",\"format\":"+std::to_string(desc.Format)
          +",\"output_format\":"+std::to_string(o.format)+",\"width\":"+std::to_string(data.resourceSize.x)
          +",\"height\":"+std::to_string(data.resourceSize.y)+",\"encoding_verified\":false");
    }
    if(type==XEFG_SWAPCHAIN_RES_MOTION_VECTOR) {
      frame.motionWidth=data.resourceSize.x; frame.motionHeight=data.resourceSize.y;
    }
  }
}
Conventions SubmitConstants(Frame& frame,uint32_t issues,const xefg_swapchain_frame_constant_data_t& data,
    const Conventions& stated) {
  frame.constants=true;
  frame.constantIssues=issues;
  frame.data=data;
  frame.valid=issues==0;
  // Plan 3.3: the game's own constants state its conventions. A mismatch
  // with this output's init flags is learned for the next output; this one
  // stays in passthrough (the flags are fixed at Intel context creation).
  Conventions learn;
  auto& s=State();
  if((issues&ConstantDepth) && stated.depthInverted && !s.explicitInvert && s.invert!=*stated.depthInverted)
    learn.depthInverted=stated.depthInverted;
  if((issues&ConstantJitterMode) && stated.motionJittered && !s.explicitJitter && s.jitter!=*stated.motionJittered)
    learn.motionJittered=stated.motionJittered;
  return learn;
}
}
