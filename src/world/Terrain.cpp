#include "world/Terrain.hpp"
#include "world/DayNight.hpp"
#include "world/Weather.hpp"
#include "world/map/CMap.hpp"
#include "world/CWorldScene.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/CWorld.hpp"
#include "db/Db.hpp"
#include "world/ParticleFx.hpp"
#include "world/Clouds.hpp"
#include "world/CWorldParam.hpp"
#include "world/map/CMapEntity.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapObjDef.hpp"
#include "console/CVar.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Model.hpp"
#include "model/Model2.hpp"
#include "model/M2Types.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "model/CM2Lighting.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/shader/CGxShader.hpp"
#include "util/CStatus.hpp"
#include "util/SFile.hpp"
#include <storm/String.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <set>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <common/Time.hpp>
#include <common/Handle.hpp>
#include <storm/Memory.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <tempest/quaternion/C4Quaternion.hpp>

namespace {

const float TILE_SIZE = 533.33333f;
const float CHUNK_SIZE = TILE_SIZE / 16.0f;
const float UNIT_SIZE = CHUNK_SIZE / 8.0f;

const int32_t MAX_LAYERS = 4;

// Distance from the map's NW corner to its centre (32 tiles), used to convert the corner-relative
// MDDF/MODF placement coordinates into the world coordinates the terrain and entities share.
const float MAP_CORNER = 32.0f * TILE_SIZE;
const float DEG2RAD = 0.01745329252f;

// The load window is driven by the far clip (see TerrainUpdate): the reference streams enough tiles
// to fill the view distance. At the default-ish far clips the radius is 2, so the pool holds a full
// 5x5 window; a small far clip loads fewer and leaves the extra slots idle.
// 3 rings (a 7x7 window, >= 1600 yards). The world is drawn to the horizon distance rather than to
// farclip, so streaming has to reach far enough that the tile window is not the new hard edge.
const int32_t MAX_TILE_RADIUS = 3;
const int32_t MAX_TILES = (2 * MAX_TILE_RADIUS + 1) * (2 * MAX_TILE_RADIUS + 1); // 25
char s_mapName[128] = { 0 };

// Registry of every placement uniqueId currently loaded, so an object that appears in more than one
// overlapping ADT tile (large WMOs, cross-boundary doodads) is placed exactly once, like the
// reference. The sentinel 0xFFFFFFFF is never registered and always placed.
int32_t s_mapID = -1;
C3Vector s_cameraPos = { 0.0f, 0.0f, 0.0f };

// Per-pixel terrain shader (D3D bytecode compiled from terrain_vs/ps.hlsl)
bool s_terrainShaderTried = false;

// Fallback path for non-D3D backends: the UI shaders, multi-pass per-vertex alpha
CGxShader* s_uiVertexShader[2] = { nullptr, nullptr };
CGxShader* s_uiPixelShader = nullptr;


CImVector s_colorScratch[145];
uint16_t s_holeIndices[768]; // filtered index scratch for chunks that have holes

// Six view-frustum planes (world space) rebuilt each frame from the view-projection
float s_frustum[6][4];

// The frame's world->clip matrix (transposed for the shader constant), kept for the passes that
// draw terrain-projected decals after TerrainRender has returned (blob shadows)
C44Matrix s_viewProjT;
bool s_viewUpdated = false;

// The camera-at-origin view and the projection, kept so each chunk can build its own
// local->clip matrix (see ChunkMatrixT).
C44Matrix s_viewNoTranslate;
C44Matrix s_projNative;

// Whether the current zone's data-driven fog is near enough to be visible within the view distance
bool s_fogActive = false;

// The liquid the camera is submerged in this frame (LiquidType kind, -1 when in air), refreshed in
// TerrainUpdate; the reference keeps the same in DAT_00cd8794 from CWorldScene FUN_00790920
int32_t s_cameraLiquidKind = -1;
int32_t s_cameraLiquidType = -1; // LiquidType id of that surface, for the underwater overlay

// Scatter the chunk's detail doodads (the reference's DetailDoodad batch builder): per 8x8 cell the
// dominant layer's GroundEffectTexture names up to four doodads with weights and an amount; cells
// flagged in noEffectDoodad and holes get none. Positions are deterministic per chunk so a tile
// reloads identically.

// Load a WMO (root + group files), transform all group geometry into world space using the
// placement derived from the MODF entry, and build per-material textured batches. The placement
// convention (local X is up; local Y/Z are the horizontal plane rotated by the yaw) was verified
// against the MODF world-space bounding box.


// The UI shader pair, which is the only program this file still owns. It is what DayNight,
// Weather, OverheadIcons and ParticleFx draw their quads through, via TerrainUiShaders.
//
// The stand-in's own terrain, blob-decal and detail-doodad programs used to be built here too, in
// D3D9 bytecode and again in ARB assembly. Nothing reads them any more: the chunks draw through
// CMap::GetTerrainVertexShader's .bls permutations, the detail doodads through DetailDoodad's own
// state, and the blob decal is gone. They were created on every map load and never bound.
void EnsureShaders() {
    if (s_terrainShaderTried) {
        return;
    }

    s_terrainShaderTried = true;

    if (!g_theGxDevicePtr) {
        return;
    }

    g_theGxDevicePtr->ShaderCreate(s_uiVertexShader, GxSh_Vertex, "Shaders\\Vertex", "UI", 2);
    g_theGxDevicePtr->ShaderCreate(&s_uiPixelShader, GxSh_Pixel, "Shaders\\Pixel", "UI", 1);
}


// ------------------------------------------------------------------------------------------------
// WMO group visibility through portals (the reference's CPortalView walk, FUN_007ad1f0, seeded from
// the camera's group inside a building or from the exterior groups outside it).
// ------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
// Weather (the reference's MapWeather: FUN_0078ca50 draws three emitters -- rain drops
// textures\Weather\RainDrop01.blp, snow textures\Weather\SnowMist01.blp, mist
// textures\Weather\WeatherMistGrainy01.blp -- around the camera). This is a stand-in particle
// field: sprites fall inside a box that follows the camera, re-seeded when they leave it.
// ------------------------------------------------------------------------------------------------

} // namespace (weather state)


void TerrainLoad(const char* mapName, int32_t mapID) {
    TerrainUnload();
    SStrCopy(s_mapName, mapName, sizeof(s_mapName));
    s_mapID = mapID;

}

void TerrainUnload() {
    SkyRelease();

    CWorld::SetCameraUnderLiquid(false);
}

void TerrainUpdate(const C3Vector& cameraPos) {
    s_cameraPos = cameraPos;

    if (!s_mapName[0]) {
        return;
    }

    // Liquid under the camera. CWorldScene::UpdateCameraLiquid answers the outdoor half from
    // CMap::Render now and keeps the depth with it; this still asks LiquidAt as well, because
    // only LiquidAt knows about the WMO pools the scene's version cannot see yet, and because
    // the stand-in sky and overlay read the kind rather than the type.
    {
        float surfaceZ;
        s_cameraLiquidType = -1;
        // The camera liquid is CWorldScene::UpdateCameraLiquid's now, terrain and indoor halves
        // both, and it sets CWorld::SetCameraUnderLiquid itself.
        SkySetCameraState(cameraPos);

        // The weather's particle field is built around the camera too, and moved to Weather.cpp on
        // the same terms: the value it used to read directly, pushed from where it was read.
        WeatherSetCameraPos(cameraPos);
    }

    // Recompute the outdoor light for the current time of day; models and the sky read it directly,
    // and terrain is re-baked from its stored inputs when the light shifts enough (day/night cycle).
    CWorld::UpdateOutdoorLight();

    int32_t centerCol = static_cast<int32_t>(32.0f - cameraPos.y / TILE_SIZE);
    int32_t centerRow = static_cast<int32_t>(32.0f - cameraPos.x / TILE_SIZE);

    // The 5x5 tile pool used to be streamed here -- ADTs loaded and freed as the camera moved,
    // chunks parsed, and their vertex colours re-lit two tiles a frame on every light change.
    // Every consumer of that data has now gone: the terrain draw moved to CWorldScene::RenderTerrain,
    // the liquid queries to CMap and CMapObjGroup, the WMOs to CMapObjDef, and the blob decal was
    // deleted once it could no longer match the receiver's depth. CMapArea streams the real tiles.
    }

void TerrainUpdateView() {
    if (!s_mapName[0]) {
        return;
    }

    // Water is baked per vertex, so it has to follow the light rather than the load. Rebake only
    // Build exactly the transform the M2 scene uses: the eye-at-origin view translated by
    // -cameraPos, times the native projection, transposed for the shader.
    C44Matrix view;
    GxXformView(view);
    s_viewNoTranslate = view; // camera at the origin; per-chunk matrices add their own translation

    C3Vector invCameraPos = { -s_cameraPos.x, -s_cameraPos.y, -s_cameraPos.z };
    view.Translate(invCameraPos);

    C44Matrix proj;
    GxXformProjNative(proj);
    s_projNative = proj;

    C44Matrix viewProj = view * proj;
    s_viewProjT = viewProj.Transpose();

    // Data-driven fog: enable it whenever the fog begins within the view distance, so geometry
    // between the fog start and the far plane is hazed even when the fog end lies beyond the far
    // clip (linear fog handles the partial factor). Colours and distances come from Light.dbc.
    float fogEnd = CWorld::GetFogEnd();
    s_fogActive = fogEnd > 1.0f && CWorld::GetFogStart() < CWorld::GetFarClip();

    // The fog RENDER STATES are not set here any more. CMap::Render already calls
    // CWorld::SetupFogRenderStates, and this block ran afterwards and overwrote its work with a
    // worse conversion: it truncated each channel instead of CM2Lighting::FogColorByte's clamped
    // rounding, never clamped above 1.0 -- so an overbright fog colour overflowed its byte and
    // corrupted the packed value through the shift -- and left alpha at 0 where the reference sets
    // 0xFF. Only the flag survives, which is all Weather asks for.

    s_viewUpdated = true;
}

// All that is left of the stand-in's render: make sure this frame's view half has run.
//
// The passes this used to make are all the map's now -- CWorldScene::RenderTerrain draws the chunks,
// RenderMapObjs the buildings, Liquid::Draw both water buckets -- and the blob decal that outlived
// them was deleted once it could no longer reproduce a receiver's depth. What is still wanted from
// here is the frustum, the view-projection and the fog flag that DayNight, Weather and the entity
// cull read, plus the buildings' own prop cull.
void TerrainRender() {
    if (!s_mapName[0]) {
        return;
    }

    if (!s_viewUpdated) {
        TerrainUpdateView();
    }

    s_viewUpdated = false;
}


const C44Matrix& TerrainViewProjT() {
    return s_viewProjT;
}

bool TerrainFogActive() {
    return s_fogActive;
}

void TerrainUiShaders(CGxShader*& vs, CGxShader*& ps) {
    EnsureShaders();
    vs = s_uiVertexShader[0];
    ps = s_uiPixelShader;
}

// Is a point inside an interior room?
//
// Same walk as TerrainInteriorAmbientAt below, minus the ambient and minus its logging, because
// this one answers a game question rather than a lighting one and Lua can ask it at any time.
//
// DIVERGENCE worth knowing before trusting it. The reference does not search at all: the player
// object carries its current area context and the answer is a field lookup. This walks every
// loaded tile's buildings and tests containment geometrically, and the note on
// TerrainInteriorAmbientAt records where that is known to be wrong -- an interior group's bounding
// box can reach out over an open deck on a large single-WMO structure like Ebon Hold, so a point
// standing outside can test as inside. The geometry test after the box narrows it but does not
// close it.
bool TerrainPointIsIndoors(const C3Vector& pos) {
    // The reference does not test containment in a room volume at all: it drops a segment and asks
    // what surface is under you. QuerySegmentMapObjs already discards a slot whose group carries the
    // exterior flag, so a non-zero answer IS "the floor below this point belongs to a room" -- which
    // is the same question CWorldScene::UpdateCameraDef asks to decide the camera is indoors, with
    // the same 1760-unit drop and maxT of 1.0.
    //
    // This replaces a stand-in containment test (a per-group spatial grid plus an above/below
    // triangle count) whose imprecision the header used to warn about. Standing on an exterior
    // bridge above a room now reads as outdoors, because the bridge is the nearer surface, which is
    // the behaviour the reference has.
    C3Vector start = pos;
    C3Vector end = { pos.x, pos.y, pos.z - 1760.0f };

    CMapObjDef* defs[2] = { nullptr, nullptr };
    uint32_t groups[4] = { 0xffff, 0xffff, 0xffff, 0xffff };

    return QuerySegmentMapObjs(start, end, 1.0f, defs, groups) != 0;
}

