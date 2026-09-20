#ifndef WORLD_MINIMAP_HPP
#define WORLD_MINIMAP_HPP

#include <cstdint>

// The minimap's tile name table.
//
// Minimap tiles do not live under their own names in the MPQs. Every one of them is stored as an
// MD5-looking filename, and Textures\Minimap\md5translate.trs is the index that maps the readable
// path to the stored one. Nothing can draw a minimap tile without going through it.
//
// This is the data layer only. Frozen has no minimap renderer yet -- CGMinimapFrame is state and
// bindings with no draw -- so nothing calls the lookup below. It is here because every later step
// needs it and it is the one part that can be written and checked without a screen.

// ref: FUN_007f6540
// Read the table. Safe to call more than once; the second call does nothing.
void MinimapLoadTranslate();

void MinimapUnloadTranslate();

// ref: the hash lookup FUN_0055f4d0 wraps
// The stored filename for a readable tile path, or null when the table has no entry. The path is
// matched case-insensitively, since the .trs and the callers disagree about case.
//
// Takes the path as it appears on the LEFT of the .trs, e.g. "Azeroth\map32_48.blp", and returns
// the bare stored name from the right.
const char* MinimapTranslate(const char* tilePath);

// How many entries the table holds. Zero before a successful load.
int32_t MinimapTranslateCount();

// The stored name for one world map tile, or null when the table has no entry for it.
//
// Builds "<map>\\map<x>_<y>.blp" and translates it. The two coordinates are NOT formatted the same
// way -- see the definition, which also records the ten shipped tiles this cannot reach.
const char* MinimapWorldTile(const char* mapName, int32_t x, int32_t y);

// The stored name for one piece of a building's interior minimap, or null.
//
// Builds "<name>_<group>_<x>_<y>.blp". Two thirds of the table is this shape rather than world
// tiles: buildings and dungeons carry their own minimap imagery, keyed by the WMO's own path.
const char* MinimapWmoTexture(const char* wmoName, int32_t group, int32_t x, int32_t y);

#endif
