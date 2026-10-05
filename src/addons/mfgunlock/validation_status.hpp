/* SPDX-License-Identifier: MIT
 * Pure MFG request/runtime validation classification.
 *
 * A successful SetOptions call, a driver-reported live multiplier and a
 * provider-reported generated presentation are different evidence levels.
 * Keep them separate so integrations which do not poll slDLSSGGetState after
 * enabling frame generation are not left in a misleading "Pending" state.
 */
#pragma once

#include <cstdint>

namespace mfgunlock::validation {

enum class Stage : uint32_t {
  kWaitingForGameRequest = 0,
  kNativeRequestObserved,
  kRequestAccepted,
  kDriverMultiplierActive,
  kProviderOutputConfirmed,
  kRequestRejected,
  kRuntimeError,
};

struct Evidence {
  bool game_request_seen = false;
  bool request_accepted = false;
  bool request_rejected = false;
  bool runtime_error = false;
  bool driver_status_available = false;
  uint32_t driver_multiplier = 0;
  bool provider_state_seen = false;
  uint32_t provider_presentations = 0;
};

inline constexpr Stage Classify(const Evidence& evidence) {
  if (evidence.runtime_error) return Stage::kRuntimeError;
  if (evidence.request_rejected) return Stage::kRequestRejected;
  if (evidence.request_accepted && evidence.provider_state_seen &&
      evidence.provider_presentations > 1)
    return Stage::kProviderOutputConfirmed;
  if (evidence.driver_status_available && evidence.driver_multiplier >= 2)
    return Stage::kDriverMultiplierActive;
  if (evidence.request_accepted) return Stage::kRequestAccepted;
  if (evidence.game_request_seen) return Stage::kNativeRequestObserved;
  return Stage::kWaitingForGameRequest;
}

inline constexpr bool HasAcceptedRequest(Stage stage) {
  return stage == Stage::kRequestAccepted ||
         stage == Stage::kDriverMultiplierActive ||
         stage == Stage::kProviderOutputConfirmed;
}

inline constexpr bool HasLiveRuntimeEvidence(Stage stage) {
  return stage == Stage::kDriverMultiplierActive ||
         stage == Stage::kProviderOutputConfirmed;
}

}  // namespace mfgunlock::validation
