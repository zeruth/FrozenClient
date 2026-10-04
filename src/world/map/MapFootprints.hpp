// Map.cpp's footprints: the decals units leave as they walk (showfootprints, CWorld flag 0x400).
//
// A ring of 0x240 footprints (0x00cf4960, 0x34 bytes each), the first 0x40 for the active player
// and the rest for everyone else, each the ground's facets under the step cut into a textured
// decal that fades out over six seconds. One texture per FootprintTextures.dbc row
// (CMapFootprintTexture, 0x00cfbe64), each drawn as one batch of the steps that use it.
#ifndef WORLD_MAP_MAP_FOOTPRINTS_HPP
#define WORLD_MAP_MAP_FOOTPRINTS_HPP

#include <tempest/Vector.hpp>
#include <cstdint>

// ref: FUN_007a03c0
void FootprintsInitialize();

// ref: FUN_0079fa10
void FootprintsClear();

// ref: FUN_0079f860
void FootprintsDestroy();

// ref: FUN_0079fa70
// A step: texture `texture` of `size` (length, width) at `position`, turned to `facing`, mirrored
// for the other foot, on ground of `terrainType` (whose flag 0x1 takes footprints); the active
// player's steps keep their own part of the ring.
void FootprintAdd(uint32_t texture, const C2Vector& size, const C3Vector& position, float facing, int32_t mirror,
                  int32_t terrainType, int32_t activePlayer);

// ref: FUN_0079fcc0
void FootprintsRender();

#endif
