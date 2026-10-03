#ifndef WORLD_MAP_C_MAP_COLLIDE_HPP
#define WORLD_MAP_C_MAP_COLLIDE_HPP

#include "world/WorldFacets.hpp"
#include <tempest/box/CAaBox.hpp>
#include <cstdint>

// ref: FUN_007a5f20
// The world's collision triangles inside `box`: buildings, terrain, chunk liquid and doodads, by
// query mask (0x100 terrain, 0xf0 buildings, 0xf doodads, 0x30000 liquid). False when part of the
// box is in a tile or building still loading. `sweep` is the mover's own box, for the walls the
// 0x80000000 mask puts around what is not loaded.
bool MapQueryBoxFacets(const CAaBox& sweep, const CAaBox& box, CFacetList& list, uint32_t flags, uint32_t* hitFlags);

// ref: FUN_007a3e40
void MapAddBoxFacets(const CAaBox& box, CFacetList& list);

#endif
