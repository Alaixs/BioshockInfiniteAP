#pragma once

// Traps (negative Archipelago items): a nuisance for a few seconds.
//
//   trap <type> [seconds]  -> "ok trap <type> <seconds>"; type:
//     disarm     weapons, Vigors and melee blocked (XPlayerController.bWeaponInputDisabledByScript,
//                the lock the story scripts use); the guns it put away are taken out again;
//     jam        firearms blocked, Vigors still usable (StartFireWeapon is ignored);
//     novigor    Vigors blocked, weapons still usable (StartFirePlasmid is ignored);
//     walk       walking only: no sprint, no jump (Pawn.SprintEnter/ExitInputThreshold above the
//                maximum input, StartSprint ignored, a sprint in progress stopped, Pawn.JumpZ at 0);
//                Sky-Lines still work;
//     lizstrike  Elizabeth tosses nothing (ammo, salts, health, coins): her toss chance bonuses
//                (XAIElizabethController.*TossChanceBonus) are set very low; she opens no Tear
//                (XPerformSubstantiateObject is ignored).
//   trap scream -> the Boys of Silence scream (Comstock House Wwise bank loaded for the scream).
//   trap stop  -> ends every trap.
// A trap already active is extended. Everything is restored as it was at the end. While a vending
// screen or the Infusion choice is open: "err trap deferred: screen open".
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::traps {

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& line, const void* controller, const Reply& reply);

// Every frame: keeps the active traps applied and restores the original state when they end.
void Tick(const void* controller);

// Before a ProcessEvent: true if the call must be ignored (fire, sprint or Tear blocked by a trap).
bool Intercept(const void* self, const void* func);

// Scream picture to draw now (after "trap scream"): the texture, or nullptr; *opacity from 1
// (full) to 0 (end of the fade).
const void* ScreamerImage(float* opacity);

// Active traps, for the debug overlay: "Disarmed 12 s, ..." (empty if none).
std::wstring ActiveSummary();

}  // namespace bsap::traps
