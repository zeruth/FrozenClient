#ifndef WORLD_TERRAIN_HPP
#define WORLD_TERRAIN_HPP

#include <tempest/Vector.hpp>

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
void SkyRender();
bool TerrainSphereVisible(const C3Vector& center, float radius);
// LiquidType kind (0 water, 1 ocean, 2 magma, 3 slime) the camera is submerged in, or -1 in air.
int32_t TerrainCameraLiquidKind();
// Blob shadows: call Begin once per frame after the opaque world, Draw per entity, then End.
void BlobShadowsBegin();
void BlobShadowDraw(const C3Vector& pos, float radius);
// The same decal on WMO floors (interiors, bridges, platforms).
void BlobShadowDrawWmo(const C3Vector& pos, float radius);
void BlobShadowsEnd();
// Liquid surfaces: bucket 0 = opaque (magma, slime), drawn inside TerrainRender; bucket 1 = the
// transparent water/ocean, drawn from the world frame's transparent block after M2 pass 2.
void LiquidRender(int32_t bucket);
// Weather: state from SMSG_WEATHER (effectType 1 rain, 2 snow, 3 sand/mist, else clear); the
// particle field is simulated and drawn around the camera in the transparent block.
void TerrainSetWeather(int32_t effectType, float intensity, const float* color, const char* texture, bool abrupt);
void WeatherRender();
// Detail (ground effect) doodads: the grass/pebble batches of the chunks near the camera, drawn
// after the opaque models (reference: FUN_007984a0 after M2 pass 0).
void DetailDoodadRender();
// Underwater overlay: a tinted, scrolling liquid-texture sheet in front of the camera while it is
// submerged (reference: CMap FUN_0079ca70 after the transparent block).
void UnderwaterOverlayRender();

// Shared render inputs for the passes that draw world geometry outside Terrain.cpp
class CGxShader;
class C44Matrix;
const C44Matrix& TerrainViewProjT();
bool TerrainFogActive();
void TerrainUiShaders(CGxShader*& vs, CGxShader*& ps);
// Visit every loaded terrain and WMO doodad model
void TerrainForEachDoodad(void (*fn)(CM2Model* model, void* arg), void* arg);
// True if pos is inside a loaded WMO interior group; fills outAmbient with that WMO's interior light.
uint32_t TerrainAreaIDAt(const C3Vector& pos);

// The light for a unit on a WMO floor: the reference's probe through the interior groups' BSP
// and MOCV sample (CMapEntity::FloorLight). Fills the entity's diffuse and ambient as bytes.
class CImVector;
bool TerrainWmoFloorLightAt(const C3Vector& pos, CImVector* diffuse, CImVector* ambient);

// Is a world position inside an interior WMO room? Shares its containment test with
// TerrainInteriorAmbientAt, and therefore shares that test's known imprecision -- see the
// definition before relying on it for anything but a yes/no the player can shrug at.
bool TerrainPointIsIndoors(const C3Vector& pos);

#endif
