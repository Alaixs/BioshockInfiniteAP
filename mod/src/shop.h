#pragma once

// Vending machines (Dollar Bill, Veni! Vidi! Vigor!, Minuteman's Armory) for Archipelago.
//
// Inspection (debug):
//   vend                         -> machines of the level: id, distance, rows
//   vendstock #M i N             -> stock of row i of machine #M (0 = hidden, -1 = unlimited)
//   vendremove #M i              -> removes row i
//
// Locks (game wares unavailable until the Archipelago item is received):
//   lock Loot_Consumable_Health_HealthKit / unlock ... / locks / lockclear
//   ("Loot_Ammo_Weapon_Pistol*": every name starting like this)
//
// Archipelago rows (checks sold in shops):
//   shopslot <id> <machine> <price> <shown name...>
//   shopsold <id>   (already bought: stays shown as sold, not buyable)
//   shopclear / shops
// When an AP row is bought, the mod blocks the game's purchase, takes the money, plays the
// purchase sound, marks the row sold and sends "event shopbuy <id>".
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::shop {

bool AnyScreenOpen();

// A vending screen is open or just closed: the game crashes if money or inventory change while it
// closes.
bool GrantsBlocked();

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& line, const Reply& reply);
// Every frame: reapplies locks and AP rows (periodically), deferred actions.
void Tick(const void* controller, float x, float y, float z);
// From the ProcessEvent hook: true = block the call.
bool Intercept(void* self, const void* func);

}  // namespace bsap::shop
