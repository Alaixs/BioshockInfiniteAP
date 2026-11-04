#pragma once

// Vigors locked by Archipelago.
//
//   vigorlock <key>|clear         Vigors Booker must not have (sent by the client: those whose
//                                 Archipelago item has not been received yet)
//   vigorown <key>:<item>|clear   Vigors received from Archipelago (item = XItemDatabase bottle):
//                                 given again if missing (a chapter's loadout script removes them)
//   vigors                        -> "vigor <archetype>" for each of Booker's Vigors, then
//                                 "end vigors N"
//
// Keys: devilskiss, murderofcrows, buckingbronco, charge, undertow, returntosender. Possession
// and Shock Jockey are never locked: the story needs them (Welcome Center, Hall of Heroes).
// Neither is the Bucking Bronco lent by a Fair booth (Plasmid_BuckingBronco_Twn_Fair).
//
// A locked Vigor that lands in the inventory (bottle, loot, a chapter's loadout script) is removed
// (XPawn.DestroyInventoryItem) and reported as "event vigor <key>", the first time being its
// check. If it is in hand the game switches to the next one first: destroying it in hand breaks
// the left hand. A received Vigor is given like a bottle, with the game's tutorial.
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::vigors {

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& arg, const void* controller, const Reply& reply);
void Tick(const void* controller);

}  // namespace bsap::vigors
