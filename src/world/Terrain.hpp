#ifndef WORLD_TERRAIN_HPP
#define WORLD_TERRAIN_HPP

#include <tempest/Vector.hpp>

// A first-cut terrain renderer: it loads the ADT tiles around the camera and draws each map chunk
// as a shaded, base-textured height mesh through the UI shaders. Multi-layer texture blending,
// water, and the real terrain shader system come later.
void TerrainLoad(const char* mapName, int32_t mapID);
void TerrainUnload();
void TerrainUpdate(const C3Vector& cameraPos);
void TerrainRender();

#endif
