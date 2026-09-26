#ifndef WORLD_TERRAIN_HPP
#define WORLD_TERRAIN_HPP

#include <tempest/Vector.hpp>
#include <tempest/Box.hpp>

class CM2Model;

// A first-cut terrain renderer: it loads the ADT tiles around the camera and draws each map chunk
// as a shaded, base-textured height mesh through the UI shaders. Multi-layer texture blending,
// water, and the real terrain shader system come later.
void TerrainLoad(const char* mapName, int32_t mapID);
void TerrainUnload();
void TerrainUpdate(const C3Vector& cameraPos);
// Refresh the view, frustum, fog state and doodad visibility for this frame. TerrainRender calls
// it itself if the frame has not already; call it early when something has to be culled or drawn
// before the terrain pass, as the shadow map is.
void TerrainUpdateView();

void TerrainRender();
// Weather: state from SMSG_WEATHER (effectType 1 rain, 2 snow, 3 sand/mist, else clear); the
// particle field is simulated and drawn around the camera in the transparent block.
void TerrainSetWeather(int32_t effectType, float intensity, const float* color, const char* texture, bool abrupt);
void WeatherRender();
// Detail (ground effect) doodads: the grass/pebble batches of the chunks near the camera, drawn
// after the opaque models (reference: FUN_007984a0 after M2 pass 0).
// Underwater overlay: a tinted, scrolling liquid-texture sheet in front of the camera while it is
// submerged (reference: CMap FUN_0079ca70 after the transparent block).
void UnderwaterOverlayRender();

// Shared render inputs for the passes that draw world geometry outside Terrain.cpp
class CGxShader;
class C44Matrix;
const C44Matrix& TerrainViewProjT();
bool TerrainFogActive();
void TerrainUiShaders(CGxShader*& vs, CGxShader*& ps);


// Is a world position inside an interior WMO room? Answered the reference's way, by dropping a
// segment and asking whether the surface below belongs to a non-exterior group.
bool TerrainPointIsIndoors(const C3Vector& pos);

#endif
