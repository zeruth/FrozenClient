#ifndef WORLD_DAY_NIGHT_HPP
#define WORLD_DAY_NIGHT_HPP

#include "gx/Texture.hpp"
#include <cstdint>
#include <tempest/Vector.hpp>

// The sky: the gradient dome, the sun and moon discs with their glares, the star and skybox M2s,
// and the azimuthal highlight. This is the reference's DayNight module (queue item 8); the code was
// moved here out of src/world/Terrain.cpp on 2026-09-26, where it had been sitting in the stand-in
// renderer even though it is reference-derived -- the sun direction from FUN_007eea90, the dome's
// zenith table from 0x00a41a90, the highlight from FUN_007f0530. See docs/ref/parity-sky.md.
//
// The move is deliberately a PURE RELOCATION: the code is byte-identical to what ran before, and
// the four externals it used in Terrain.cpp are mirrored here and pushed in by SkySetCameraState,
// so timing and values are unchanged. Pointing it at CWorldScene::s_cameraPos and
// CWorld::IsCameraUnderLiquid() instead is a separate, separately verifiable step -- doing both at
// once would make any regression unattributable, which matters because the sky was only confirmed
// good on screen the day this moved.

// Draw the sky. Skipped entirely while the camera is under liquid.
void SkyRender();

// Free the sky's private M2 scenes (stars and the zone skybox) on map unload.
void SkyRelease();

// The dome's 8x8 white sheet. Exposed because one draw outside this module uses it as a fallback
// texture; it was a file-local static in Terrain.cpp.
HTEXTURE SkyWhiteTexture();

// Push the camera state this module needs. Called where the terrain already tracks it, so the sky
// sees exactly the values, at exactly the moment, it saw before the move.
void SkySetCameraState(const C3Vector& cameraPos, int32_t cameraLiquidKind);

#endif
