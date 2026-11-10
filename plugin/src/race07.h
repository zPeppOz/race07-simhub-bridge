#pragma once

#include "rf1_shared.h"

namespace race07 {

// Recognises the running executable and resolves the game addresses.
bool Init();
// Converts the live game state into the rFactor 1 shared layout; false while
// no session is loaded. Must run on the game thread (per-frame callback).
bool Read(rf1::Shared& out);

}  // namespace race07
