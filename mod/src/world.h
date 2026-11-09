#pragma once

// Positions and teleport.
//
//   where                    -> "where x y z yaw maps": position, heading (degrees), loaded maps (S_TWN_P,...)
//   poi [Class,Class...]     -> "poi #idx Class x y z yaw Path Name" for each placed actor
//                               (default: the vending machines)
//   tp x y z [yaw]           -> teleports the player (debug)
//   tpto #idx                -> teleports the player in front of actor #idx, facing it (debug)
//   bounds #idx              -> "bounds #idx cx cy cz ex ey ez": the actor's bounding box
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::world {

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& line, const void* controller, const Reply& reply);

// For the debug overlay.
struct Position {
    float x, y, z;
    float yaw;  // degrees (0 = +X, the engine's direction of rotation)
};
// Position of the character and heading of the camera; false without a character (menu, loading).
bool PlayerPosition(const void* controller, Position* out);
// Position and heading of a placed actor.
bool ActorPosition(const void* actor, Position* out);
// Axis-aligned bounding box of an actor (the one of its collision component).
struct Box {
    float x, y, z;     // center
    float ex, ey, ez;  // half extents
};
// False if the actor has no usable one (no component, bounds not computed yet).
bool ActorBounds(const void* actor, Box* out);
// Loaded maps ("S_TWN_P"...), comma separated; "-" if none.
std::string LoadedMaps();
// Teleports the player 1.5 m in front of the actor, facing it (like "tpto").
bool TeleportTo(const void* controller, const void* actor);

}  // namespace bsap::world
