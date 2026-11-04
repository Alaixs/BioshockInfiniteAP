#pragma once

// The Archipelago logo as an extra game icon (no existing icon is replaced).
//
// Item icons are XItemDatabase.AtlassedIcons entries { texture, rectangle }, picked by each item's
// AtlassedIconIndex. We create a 128x128 texture, write the logo, upload it (UpdateResource) and
// append an entry pointing to it, which also keeps it from the garbage collector.
//
// Game thread only.

namespace bsap::aptexture {

// Index of the Archipelago icon in AtlassedIcons (created on the first call), or -1.
// context: any valid object (for the game's calls and allocations).
int IconIndex(const void* context);

// Login screen (the game's "2K account" screen): the Archipelago logo instead of the star
// emblem at the top of the frame. The picture comes from the XTwoKAccountMenu_I79 texture; we
// create a modified copy (ap_emblem.h) and the two textures swap their GPU resources.
// Redone each time the main menu reloads (checked once per second).
bool ReplaceAccountEmblem(const void* context);

// Picture of the "Boys of Silence" trap (ap_screamer.h): texture created once and kept alive by
// an entry added to AtlassedIcons, like the logo. nullptr while not possible yet. Its GPU
// resource takes a moment to be ready: create it before it is needed.
const void* ScreamerTexture(const void* context);

}  // namespace bsap::aptexture
