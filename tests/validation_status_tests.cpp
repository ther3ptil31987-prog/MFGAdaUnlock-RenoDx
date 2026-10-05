// SPDX-License-Identifier: MIT
#include <cstdlib>
#include <iostream>

#include "../src/addons/mfgunlock/validation_status.hpp"

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition     \
                << '\n';                                                       \
      return EXIT_FAILURE;                                                      \
    }                                                                           \
  } while (false)

int main() {
  using mfgunlock::validation::Classify;
  using mfgunlock::validation::Evidence;
  using mfgunlock::validation::HasAcceptedRequest;
  using mfgunlock::validation::HasLiveRuntimeEvidence;
  using mfgunlock::validation::Stage;

  CHECK(Classify({}) == Stage::kWaitingForGameRequest);

  Evidence evidence{};
  evidence.game_request_seen = true;
  CHECK(Classify(evidence) == Stage::kNativeRequestObserved);

  // Regression: a game may accept SetOptions and never poll GetState again.
  evidence.request_accepted = true;
  CHECK(Classify(evidence) == Stage::kRequestAccepted);
  CHECK(HasAcceptedRequest(Classify(evidence)));
  CHECK(!HasLiveRuntimeEvidence(Classify(evidence)));

  evidence.driver_status_available = true;
  evidence.driver_multiplier = 3;
  CHECK(Classify(evidence) == Stage::kDriverMultiplierActive);
  CHECK(HasLiveRuntimeEvidence(Classify(evidence)));

  evidence.provider_state_seen = true;
  evidence.provider_presentations = 3;
  CHECK(Classify(evidence) == Stage::kProviderOutputConfirmed);

  evidence.request_rejected = true;
  CHECK(Classify(evidence) == Stage::kRequestRejected);

  evidence.runtime_error = true;
  CHECK(Classify(evidence) == Stage::kRuntimeError);

  // Zero/one provider presentations are samples, not generated-output proof.
  evidence = {};
  evidence.game_request_seen = true;
  evidence.request_accepted = true;
  evidence.provider_state_seen = true;
  evidence.provider_presentations = 1;
  CHECK(Classify(evidence) == Stage::kRequestAccepted);

  // A successful provider sample must not hide explicit request rejection.
  evidence.request_rejected = true;
  evidence.provider_presentations = 4;
  CHECK(Classify(evidence) == Stage::kRequestRejected);

  // Provider counts from an earlier request must not confirm a request which
  // is no longer active.
  evidence = {};
  evidence.game_request_seen = true;
  evidence.provider_state_seen = true;
  evidence.provider_presentations = 4;
  CHECK(Classify(evidence) == Stage::kNativeRequestObserved);

  std::cout << "MFG validation status tests passed\n";
  return EXIT_SUCCESS;
}
