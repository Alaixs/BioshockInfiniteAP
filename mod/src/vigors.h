#pragma once

// Vigors locked by Archipelago.
//
//   vigorlock <key>|clear         Vigors Booker must not have (sent by the client: those whose
//                                 Archipelago item has not been received yet)
//   vigorpending <key>|clear      Vigors whose location is not checked yet (sent by the client)
//   vigorown <key>:<item>|clear   Vigors received from Archipelago (item = XItemDatabase bottle):
//                                 given again if missing (a chapter's loadout script removes them)
//   vigors                        -> "vigor <archetype>" for each of Booker's Vigors, then
//                                 "end vigors N"
//
// Keys: murderofcrows, buckingbronco, charge, undertow, returntosender. Possession and Shock Jockey
// are never locked: the story needs them (Welcome Center, Hall of Heroes). Neither is Devil's Kiss:
// the story only goes on once Booker has drunk it. Equipping it starts the "Player Got Devils Kiss"
// pattern (XPatternEvent_PawnWeaponEquipped, S_TWN_FairStreet_Game), whose drinking scene ends with
// DKIntroComplete: the quest moves on and the way out of Raffle Square opens. Taken without drinking
// (0.2.0), the quest stayed on "take the Vigor from the Fireman". Nor is the Bucking Bronco lent by a
// Fair booth (Plasmid_BuckingBronco_Twn_Fair).
//
// The bottle or drop of a locked Vigor, or of one whose location is not checked yet, is taken
// without drinking it (collect) and sends that location: a Vigor received before its bottle is
// found must not leave that bottle vanilla, its location impossible (0.2.2 and older).
// A locked Vigor that still lands in the
// inventory (a chapter's loadout script, a pickup that skips the search hook) is removed
// (XPawn.DestroyInventoryItem) and reported as "event vigor <key>", the first time being its
// check. If it is in hand the game switches to the next one first: destroying it in hand breaks
// the left hand. A received Vigor is given like a free purchase (inventory giveonce): no drinking
// animation, no tutorial. In a level that hasn't loaded it (Murder of Crows in Raffle Square),
// inventory loads every item first.
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::vigors {

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& arg, const void* controller, const Reply& reply);

// Key of the Vigor named by a pickup archetype or loot definition
// (Pickup_Plasmid_MurderofCrows_Founder_MaxAmmo, Loot_Consumable_Plasmid_MurderOfCrows_Founder_AIDrop)
// if it is locked or its location not checked yet, nullptr otherwise. collect takes such a bottle
// itself: no drinking, no tutorial, and the location is sent.
const char* BottleKey(const std::string& name);
void Tick(const void* controller);

}  // namespace bsap::vigors
