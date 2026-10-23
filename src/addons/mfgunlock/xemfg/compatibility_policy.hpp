// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace mfgunlock::xemfg::native::compatibility {
enum class Evidence : uint32_t { Unavailable, Inferred, Confirmed, Invalid };
enum class Failure : uint32_t {
  None, GameDisabled, NoOutput, PresentationOwnership, DeviceLost, UnsupportedFormat,
  NoFrame, PresentIdentity, NativeGenerationActive, LatencyOptions, MissingDepth,
  MissingMotion, MissingConstants, InvalidConstants, InputRejected, SourceWait,
  MarkerOrder, MarkerSdk, MissingMarkers, UiComposition, SdkSubmission, Retired, DisplayMode
};
inline const char* Name(Evidence value) noexcept {
  switch(value) {case Evidence::Confirmed:return "confirmed";case Evidence::Inferred:return "inferred";
    case Evidence::Invalid:return "invalid";default:return "unavailable";}
}
inline const char* Name(Failure value) noexcept {
  switch(value) {
    case Failure::None:return "ready";
    case Failure::GameDisabled:return "game_fg_disabled";
    case Failure::NoOutput:return "no_output";
    case Failure::PresentationOwnership:return "presentation_not_owned";
    case Failure::DeviceLost:return "device_lost";
    case Failure::UnsupportedFormat:return "unsupported_output_format";
    case Failure::NoFrame:return "no_matching_frame";
    case Failure::PresentIdentity:return "present_id_rejected";
    case Failure::NativeGenerationActive:return "native_fg_disable_unconfirmed";
    case Failure::LatencyOptions:return "latency_options_unavailable";
    case Failure::MissingDepth:return "depth_unavailable";
    case Failure::MissingMotion:return "motion_vectors_unavailable";
    case Failure::MissingConstants:return "constants_unavailable";
    case Failure::InvalidConstants:return "constants_incompatible";
    case Failure::InputRejected:return "input_rejected";
    case Failure::SourceWait:return "source_wait_not_completed";
    case Failure::MarkerOrder:return "marker_order_invalid";
    case Failure::MarkerSdk:return "marker_sdk_rejected";
    case Failure::MissingMarkers:return "required_markers_unavailable";
    case Failure::UiComposition:return "requested_ui_composition_unavailable";
    case Failure::SdkSubmission:return "sdk_submission_rejected";
    case Failure::Retired:return "output_retired";
    case Failure::DisplayMode:return "unsupported_display_mode";
  }
  return "unknown";
}
inline const char* Description(Failure value) noexcept {
  switch(value) {
    case Failure::None:return "Compatible frame inputs received";
    case Failure::GameDisabled:return "The game has paused frame generation";
    case Failure::NoOutput:return "Waiting for a compatible DirectX 12 output";
    case Failure::PresentationOwnership:return "Presentation ownership was not established; restart with early addon loading";
    case Failure::DeviceLost:return "The graphics device was lost; the original DirectX error was preserved";
    case Failure::UnsupportedFormat:return "This output format is not supported by the XeMFG bridge";
    case Failure::NoFrame:return "Waiting for inputs associated with this source frame";
    case Failure::PresentIdentity:return "The source Present ID was not accepted";
    case Failure::NativeGenerationActive:return "The game has not acknowledged disabling native DLSS frame generation";
    case Failure::LatencyOptions:return "Waiting for compatible low-latency settings from the game";
    case Failure::MissingDepth:return "Waiting for a depth input for this frame";
    case Failure::MissingMotion:return "Waiting for motion vectors for this frame";
    case Failure::MissingConstants:return "Waiting for camera and motion constants for this frame";
    case Failure::InvalidConstants:return "Camera or motion conventions do not match the initialized output";
    case Failure::InputRejected:return "An input was rejected; copy the compatibility report for its first failure";
    case Failure::SourceWait:return "The source-frame latency wait did not complete before its markers";
    case Failure::MarkerOrder:return "The game's latency markers arrived in an unsupported order";
    case Failure::MarkerSdk:return "The latency SDK rejected a marker for this frame";
    case Failure::MissingMarkers:return "Waiting for the required source-frame latency markers";
    case Failure::UiComposition:return "UI Composition needs a compatible HUD-less input for this frame";
    case Failure::SdkSubmission:return "The XeFG SDK rejected this frame's submission";
    case Failure::Retired:return "This output session has ended";
    case Failure::DisplayMode:return "XeMFG needs a windowed output; the game's display mode was preserved";
  }
  return "Compatibility status unavailable";
}
// Values describe actual calls/receipts. Inferred metadata is never admission.
struct Admission {
  bool requested{}, output{}, ownsPresentation{}, deviceLost{}, formatSupported{};
  bool frame{}, presentIdentity{}, nativeOff{}, latencyReady{}, depth{}, motion{};
  bool constants{}, constantsValid{}, rejected{}, sourceWait{}, requiredMarkers{}, composition{};
  Failure firstInputFailure{Failure::None};
  bool presentationSupported{true};
};
inline Failure Blocker(const Admission& a) noexcept {
  if(a.deviceLost)return Failure::DeviceLost;
  if(!a.output)return Failure::NoOutput;
  if(!a.ownsPresentation)return Failure::PresentationOwnership;
  if(!a.presentationSupported)return Failure::DisplayMode;
  if(!a.requested)return Failure::GameDisabled;
  if(!a.formatSupported)return Failure::UnsupportedFormat;
  if(!a.frame)return Failure::NoFrame;
  if(!a.presentIdentity)return Failure::PresentIdentity;
  if(!a.nativeOff)return Failure::NativeGenerationActive;
  if(!a.latencyReady)return Failure::LatencyOptions;
  if(!a.depth)return Failure::MissingDepth;
  if(!a.motion)return Failure::MissingMotion;
  if(!a.constants)return Failure::MissingConstants;
  if(!a.constantsValid)return Failure::InvalidConstants;
  if(a.rejected)return a.firstInputFailure==Failure::None?Failure::InputRejected:a.firstInputFailure;
  if(!a.sourceWait)return Failure::SourceWait;
  if(!a.requiredMarkers)return Failure::MissingMarkers;
  if(!a.composition)return Failure::UiComposition;
  return Failure::None;
}
// A resource can only remain attached to the output/epoch captured at API entry.
inline bool CurrentLease(uint64_t entryEpoch,uint64_t currentEpoch,bool retired) noexcept {
  return !retired && entryEpoch==currentEpoch;
}
}
