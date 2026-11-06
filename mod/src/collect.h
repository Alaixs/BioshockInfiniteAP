#pragma once

// Collectibles placed in the levels (Archipelago locations other than the shops).
//
//   voxophones  -> "voxophone <id> <state>" for each loaded Voxophone, then "end voxophones N"
//   infusions   -> "infusion <id> <state>" for each loaded Infusion bottle, then "end infusions N"
//   infusionap <id>|clear       bottles that are locations (sent by the AP client)
//   gears       -> "gear <id> <state>" for each loaded Gear package, then "end gears N"
//   gearap <id>|clear           Gear packages that are locations
// Picking up an AP bottle or package opens no screen and gives nothing: it is marked looted and its
// check sent.
//
// Voxophone: XLootContainerKActor with a Pickup_AudioLog_* archetype; <id> from its
// LootDefinitionList (Loot_Consumable_AudioLog_<id>). <state> is CurrentLootableState: 4 once
// looted, 1 before.
//
// Infusion bottle: Pickup_UnstableTonic_Upgrades archetype. They all look alike, so <id> is
// "package.object" (S_TWN_Float_B_Loot.XLootContainerKActor_43). Picked up (state 4, Booker within
// 4 m): "event infusion <id>".
//
// Gear package: Pickup_Gear_* archetype, same id. Picked up: "event gear <id>".
//
// Autosave checkpoint (XSeqAct_Checkpoint): "event checkpoint <step>". Songbird destroys the Siphon
// ("TowerDestroyed" in S_CHU_LizTower_Gam): "event goal siphon".
//
// Game thread only.

#include <functional>
#include <string>
#include <vector>

namespace bsap::collect {

using Reply = std::function<void(const std::string&)>;

constexpr int kLootableStateLooted = 4;

bool Command(const std::string& cmd, const std::string& arg, const Reply& reply);

// Every frame: hooks the search screens once, watches checkpoints and the goal, reports AP pickups.
void Tick(const void* controller);

struct Pickup {
    const void* actor;
    std::string id;  // Voxophone: recording (JF_A_Fink_Solution...); Infusion, Gear: package.object
    int state;
};
std::vector<Pickup> Voxophones();
std::vector<Pickup> Infusions();
std::vector<Pickup> Gear();
std::string PlacedId(const void* actor);  // "level package.object name"

}  // namespace bsap::collect
