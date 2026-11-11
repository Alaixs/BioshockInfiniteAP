#pragma once

// Hook on UObject::ProcessEvent: sees every UnrealScript call and can block some of them.
//
// Installed from the game thread on the first tick: installing it at process start (from
// DllMain or the init thread) freezes the game. Calls from other threads pass through untouched.

namespace bsap::pehook {

// Call on every tick (game thread). Installs the hook the first time.
bool EnsureInstalled();

}  // namespace bsap::pehook
