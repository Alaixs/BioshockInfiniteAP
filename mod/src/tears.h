#pragma once

// Elizabeth's Tears behind an Archipelago item.
//
// Booker asks Elizabeth to open a Tear with the use key: XPlayerController.XPerformSubstantiateObject
// (then XReactiveActor.StartSubstantiatedState on the Tear). While locked, that call is ignored and
// a message explains why; Tears the story scripts open are not affected.
//
//   tears lock | unlock -> "ok tears locked|unlocked" (unlocked when the mod starts)
//   tears              -> current state
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::tears {

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& arg, const Reply& reply);

// Before a ProcessEvent: true if the call must be ignored (Tear opened while locked).
bool Intercept(const void* self, const void* func);

}  // namespace bsap::tears
