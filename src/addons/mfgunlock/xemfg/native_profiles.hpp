// SPDX-License-Identifier: MIT
#pragma once
#include "profiles.hpp"
namespace mfgunlock::xemfg::native {
// Embedded profiles.json followed by MFGAdaUnlock.profiles.json from the game
// folder (refused as a whole if invalid). Loaded once.
const profiles::Rules& ProfileRules();
// Policy for this executable and the rule-named modules mapped right now.
profiles::Policy CurrentPolicy();
void LogProfiles() noexcept;
}
