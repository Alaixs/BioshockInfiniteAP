#pragma once

// Giving items from XItemDatabase.ItemLookupTable.
//
//   giveitem <id>  -> "called XPlayerController.ServerTransactLootItem(...)" or "err ..."
//   giveonce <variable>_<id>  -> gives <id> unless that save variable is set, then sets it:
//                  "giveonce done <arg>", or "giveonce wait <arg>" while the item isn't in memory.
//                  For items given outside the queue (upgrades only exist in some levels).
//   infusionpick <token>  Infusion to choose (token: its save variable). Opens the game's choice
//                  screen (again every 3 s while it can't: another screen open, pause menu...).
//                  "err choice pending" until the player chooses, then "infusionpick done|wait
//                  <token>" until the item is in memory and given.
//   loadpackage <name>  (debug) UObject::LoadPackage: "loadpackage <name> = <package>|failed (<ms> ms)".
//
// The choice screen needs a container: the mod spawns an invisible Pickup_Base_StableTonic (in
// every level) under Booker with the three Loot_StableTonic_*_Bonus_1 choices and opens it like a
// bottle. Accept is intercepted (the mod gives the item itself); the container is shut down when
// the screen closes. Cancel brings the screen back 20 s later.
//
// ServerTransactLootItem(id, 1, Money, 0) gives an item like a free purchase but crashes if the
// item isn't in memory. Each level only loads its own items (no Infusion at the start of Town
// Center, no Charge before its end): for one missing, the mod loads CoalescedItems.xxx, which holds
// them all (UObject::LoadPackage: DynamicLoadObject can't load in seek free mode), then gives it.
// If that still fails, it replies an error: the client retries.
//
// One thing at a time: nothing is given under the loading screen (a Vigor's tutorial would play
// there) nor until Booker has been in game 2 s. The Infusion choice screen waits 5 s after the last
// item given (Infusions aside), then for no other screen during 2 s: the game queues the tutorials
// of the Vigors given (XTrainingMovieScreen, then XModalTrainingTextScreen), and the pause menu,
// weapon wheel...
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::inventory {

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& arg, const void* controller, const Reply& reply);

// Every frame: Infusion choice screen and choice keys.
void Tick(const void* controller);

// An Infusion waits for the player's choice (its screen is open, or comes back soon).
bool ChoicePending();

// Item id (ItemLookupID) of one of the three Infusions of the choice screen, which shows their
// item database name.
bool IsInfusionItem(int id);

}  // namespace bsap::inventory
