#pragma once

// Cheat Engine style search, by field name (debug bridge commands):
//   find 5      -> every Int/Float/Byte field equal to 5, in every object of the game
//   refine 10   -> among those, the ones now equal to 10
// Each candidate is printed as "#index.Field", usable as is by get / set / watch.
//
// Game thread only. A find walks ~90,000 objects: the game freezes for a moment.

#include <functional>
#include <string>

namespace bsap::search {

using Reply = std::function<void(const std::string&)>;

void Find(double value, const Reply& reply);
void Refine(double value, const Reply& reply);
void List(const Reply& reply);
void Objects(const std::string& classFilter, const Reply& reply);

}  // namespace bsap::search
