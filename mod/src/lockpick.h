#pragma once

// Lockpicking behind an Archipelago item.
//
// Locked doors and safes are XReactiveActors with IsLockpickingActor, NumLockpicksRequired and
// HasBeenLockpicked. Using one calls XPlayerController.ServerStartUsingButtonUseTarget(UsableObject).
// While locked, that call is ignored for locks that cost lockpicks and are not picked yet, and a
// message explains why. Free locks (0 picks, the story path) are never blocked.
//
//   lockpick lock | unlock -> "ok lockpick locked|unlocked" (unlocked when the mod starts)
//   lockpick               -> current state
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::lockpick {

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& arg, const Reply& reply);

// Before a ProcessEvent: true if the call must be ignored (paid lock used while locked).
bool Intercept(const void* self, const void* func, const void* parms);

}  // namespace bsap::lockpick
