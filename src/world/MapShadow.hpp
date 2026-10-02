#ifndef WORLD_MAP_SHADOW_HPP
#define WORLD_MAP_SHADOW_HPP

#include <cstdint>
#include <tempest/Plane.hpp>
#include <tempest/Vector.hpp>

class CGxTex;

// The map's half of the shadow map (reference MapShadow.cpp): what casts into the maps and how it
// is drawn, through the four callbacks ShadowMap.cpp calls. See src/world/ShadowMap.hpp for the
// maps themselves.

// The direction the shadow light travels, exaggerated and clamped the reference's way.
C3Vector MapShadowLightDirection();

// Where the map is centred: the camera, or whatever object the camera is tracking.
// ref: FUN_007bb3e0
C3Vector MapShadowFocus();

// Clear one shadow target pair to white through a viewport rectangle, restoring the colour
// target and the viewport afterwards. ref: FUN_007bb830
void MapShadowClearTarget(CGxTex* color, CGxTex* depth, const float* viewport);

// Register the callbacks and switches and make the maps. ref: FUN_007bd3a0
void MapShadowInitialize();

// The per-frame driver: direction, focus, intensity, maps, light matrices. ref: FUN_007bb570
void MapShadowRender();

// The player's height plus two, which decides whether an interior caster is below the player
// (DAT_00d25304). Written with the plane by MapShadowSetupPlane.
extern float g_mapShadowHeight;

// The plane through the player the map object shaders sample against, and the height above.
// The plane itself is ShadowMap.cpp's g_mapShadowPlane. ref: FUN_007bb670
void MapShadowSetupPlane(const C3Vector& playerPos);

#endif
