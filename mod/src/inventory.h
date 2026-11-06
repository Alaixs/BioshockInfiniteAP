#pragma once

// Giving items from XItemDatabase.ItemLookupTable.
//
//   giveitem <id>  -> "called XPlayerController.ServerTransactLootItem(...)" or "err ..."
//   giveonce <variable>_<id>  -> gives <id> unless that save variable is set, then sets it:
//                  "giveonce done <arg>", or "giveonce wait <arg>" while the item isn't in memory.
//                  For items given outside the queue (upgrades only exist in some levels).
//   infusionpick <token>  Infusion to choose (token: its save variable). Opens the game's choice
//                  screen or, when it can't, a message offering F9 health, F10 shield, F11 salts.
//                  "err choice pending" until the player chooses, then "infusionpick done|wait
//                  <token>" until the item is in memory and given.
//
// The choice screen needs a container: the mod spawns an invisible Pickup_Base_StableTonic (in
// every level) under Booker with the three Loot_StableTonic_*_Bonus_1 choices and opens it like a
// bottle. Accept is intercepted (the mod gives the item itself); the container is shut down when
// the screen closes. Cancel brings the screen back 20 s later.
//
// ServerTransactLootItem(id, 1, Money, 0) gives an item like a free purchase but crashes if the
// item isn't in memory. Infusion items are in nearly every level's packages except the start of
// Town Center (S_TWN_P), and the game can't load them on demand, so we check first and reply an
// error: the client retries.
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::inventory {

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& arg, const void* controller, const Reply& reply);

// Every frame: Infusion choice screen and choice keys.
void Tick(const void* controller);

}  // namespace bsap::inventory
