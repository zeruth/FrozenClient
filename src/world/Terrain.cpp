#include "world/Terrain.hpp"
#include "world/DayNight.hpp"
#include "world/Weather.hpp"
#include "world/map/CMap.hpp"
#include "world/CWorldScene.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/TerrainShadersD3d9.hpp"
#include "world/TerrainShadersArb.hpp"
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
CGxShader* s_terrainVS = nullptr;
CGxShader* s_terrainPS = nullptr;
CGxShader* s_blobDecalPS = nullptr; // paired with s_terrainVS for the coplanar shadow pass
CGxShader* s_detailPS = nullptr;    // paired with s_terrainVS for ground doodads
bool s_terrainShaderTried = false;
bool s_useTerrainShader = false;

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

// Extract the frustum planes from the world->clip matrix rows (r0..r3), D3D near-plane convention
void ExtractFrustum(const C44Matrix& viewProjT) {
    const float* m = reinterpret_cast<const float*>(&viewProjT);
    const float* r0 = m + 0;
    const float* r1 = m + 4;
    const float* r2 = m + 8;
    const float* r3 = m + 12;

    for (int32_t i = 0; i < 4; i++) {
        s_frustum[0][i] = r3[i] + r0[i]; // left
        s_frustum[1][i] = r3[i] - r0[i]; // right
        s_frustum[2][i] = r3[i] + r1[i]; // bottom
        s_frustum[3][i] = r3[i] - r1[i]; // top
        s_frustum[4][i] = r2[i];         // near
        s_frustum[5][i] = r3[i] - r2[i]; // far
    }

    // Normalise each plane so the signed distance it yields is in world units. SphereVisible
    // compares that distance against a radius; with the raw clip-space rows the side planes have
    // a normal length of ~1.5 and the top/bottom ~2.2 at the world FOV, which culled objects when
    // only half to two thirds of their radius had left the view.
    for (int32_t p = 0; p < 6; p++) {
        float* pl = s_frustum[p];
        float len = sqrtf(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);

        if (len > 1e-6f) {
            pl[0] /= len;
            pl[1] /= len;
            pl[2] /= len;
            pl[3] /= len;
        }
    }
}

// A doodad's cull radius: the model's own bounding radius when that is larger than the default,
// so big props (trees) are not culled while their canopy is still on screen.
float DoodadCullRadius(CM2Model* m, float scale) {
    // The reference culls a doodad by its own bounding sphere (model radius x placement scale), like
    // units; a small floor only guards degenerate/zero bounds. A blanket 40 yd floor here just kept
    // off-screen props in the draw list.
    float r = 2.0f;

    if (m && m->m_shared && m->m_shared->m_m2DataLoaded && m->m_shared->m_data) {
        float sr = m->m_shared->m_data->bounds.radius * scale;

        if (sr > r) {
            r = sr;
        }
    }

    // A prop with emitters reaches past its mesh: a brazier's sphere is its bowl, not its flames.
    return r + ParticleFxCullExtent(m, scale);
}

// The world-space centre of a doodad's bounding sphere. The sphere is centred on the mesh (often
// well above the feet the doodad is placed by), so offset the feet position by the model-space box
// centre rotated by the placement yaw and scaled -- otherwise a tall prop is culled the moment its
// base leaves the screen while its body is still in view.
C3Vector DoodadCullCenter(CM2Model* m) {
    if (!m) {
        return { 0.0f, 0.0f, 0.0f };
    }

    // Transform the model-space bounding-box centre by the doodad's full placement matrix (rotation,
    // scale and translation baked in). This is exact for tilted props, not just yaw-rotated ones, and
    // needs no separately stored feet/yaw. matrixB4 is row-major with the translation in d0..d2.
    if (m->m_shared && m->m_shared->m_m2DataLoaded && m->m_shared->m_data) {
        const CAaBox& e = m->m_shared->m_data->bounds.extent;
        float lx = (e.b.x + e.t.x) * 0.5f;
        float ly = (e.b.y + e.t.y) * 0.5f;
        float lz = (e.b.z + e.t.z) * 0.5f;
        const C44Matrix& M = m->matrixB4;
        return {
            lx * M.a0 + ly * M.b0 + lz * M.c0 + M.d0,
            lx * M.a1 + ly * M.b1 + lz * M.c1 + M.d1,
            lx * M.a2 + ly * M.b2 + lz * M.c2 + M.d2
        };
    }

    return { m->matrixB4.d0, m->matrixB4.d1, m->matrixB4.d2 };
}

// True if a world-space bounding sphere is at least partly inside the frustum
bool SphereVisible(const C3Vector& c, float r) {
    for (int32_t p = 0; p < 6; p++) {
        const float* pl = s_frustum[p];

        if (pl[0] * c.x + pl[1] * c.y + pl[2] * c.z + pl[3] < -r) {
            return false;
        }
    }

    return true;
}

// Scatter the chunk's detail doodads (the reference's DetailDoodad batch builder): per 8x8 cell the
// dominant layer's GroundEffectTexture names up to four doodads with weights and an amount; cells
// flagged in noEffectDoodad and holes get none. Positions are deterministic per chunk so a tile
// reloads identically.

// Load a WMO (root + group files), transform all group geometry into world space using the
// placement derived from the MODF entry, and build per-material textured batches. The placement
// convention (local X is up; local Y/Z are the horizontal plane rotated by the yaw) was verified
// against the MODF world-space bounding box.

CGxShader* MakeRawShader(int32_t target, const unsigned char* code, uint32_t len);

// ARB programs are text rather than compiled bytecode, so they arrive as a char array. The device
// does not care which it is handed -- it reads shader->code either way -- so this only spares the
// call sites a cast apiece.
CGxShader* MakeArbShader(int32_t target, const char* code, uint32_t len) {
    return MakeRawShader(target, reinterpret_cast<const unsigned char*>(code), len);
}

CGxShader* MakeRawShader(int32_t target, const unsigned char* code, uint32_t len) {
    if (!g_theGxDevicePtr || !code || !len) {
        return nullptr;
    }

    auto shader = new (std::nothrow) CGxShader();

    if (!shader) {
        return nullptr;
    }

    shader->target = target;
    shader->loaded = 0;
    shader->code.SetCount(len);
    memcpy(shader->code.Ptr(), code, len);

    g_theGxDevicePtr->IShaderCreate(shader);

    if (!shader->valid) {
        delete shader;
        return nullptr;
    }

    return shader;
}

void EnsureShaders() {
    if (s_terrainShaderTried) {
        return;
    }

    s_terrainShaderTried = true;

    if (!g_theGxDevicePtr) {
        return;
    }

    // Always create the UI shaders for the fallback path
    g_theGxDevicePtr->ShaderCreate(s_uiVertexShader, GxSh_Vertex, "Shaders\\Vertex", "UI", 2);
    g_theGxDevicePtr->ShaderCreate(&s_uiPixelShader, GxSh_Pixel, "Shaders\\Pixel", "UI", 1);

    EGxApi api = g_theGxDevicePtr->m_api;

    if (api == GxApi_D3d9 || api == GxApi_D3d9Ex) {
        s_terrainVS = MakeRawShader(GxSh_Vertex, g_terrainVsD3d9, g_terrainVsD3d9_len);
        s_terrainPS = MakeRawShader(GxSh_Pixel, g_terrainPsD3d9, g_terrainPsD3d9_len);
        s_blobDecalPS = MakeRawShader(GxSh_Pixel, g_blobDecalPsD3d9, g_blobDecalPsD3d9_len);
        s_detailPS = MakeRawShader(GxSh_Pixel, g_detailPsD3d9, g_detailPsD3d9_len);
        s_useTerrainShader = (s_terrainVS && s_terrainPS);
    } else if (api == GxApi_GLL || api == GxApi_OpenGl) {
        // The same four programs in ARB assembly. Without these the GL backends had no terrain
        // program at all and every chunk fell through to the fixed-function pass, which drew the mesh
        // untextured -- the flat white ground Android rendered under correctly textured models.
        s_terrainVS = MakeArbShader(GxSh_Vertex, g_terrainVsArb, sizeof(g_terrainVsArb) - 1);
        s_terrainPS = MakeArbShader(GxSh_Pixel, g_terrainPsArb, sizeof(g_terrainPsArb) - 1);
        s_blobDecalPS = MakeArbShader(GxSh_Pixel, g_blobDecalPsArb, sizeof(g_blobDecalPsArb) - 1);
        s_detailPS = MakeArbShader(GxSh_Pixel, g_detailPsArb, sizeof(g_detailPsArb) - 1);
        s_useTerrainShader = (s_terrainVS && s_terrainPS);
    }

    SysMsgPrintf(
        SYSMSG_INFO,
        "Terrain: api %d shaders vs %s ps %s blob %s detail %s -> %s",
        static_cast<int32_t>(api),
        s_terrainVS ? "ok" : "MISSING",
        s_terrainPS ? "ok" : "MISSING",
        s_blobDecalPS ? "ok" : "MISSING",
        s_detailPS ? "ok" : "MISSING",
        s_useTerrainShader ? "shaded" : "fallback"
    );
}




// ------------------------------------------------------------------------------------------------
// WMO group visibility through portals (the reference's CPortalView walk, FUN_007ad1f0, seeded from
// the camera's group inside a building or from the exterior groups outside it).
// ------------------------------------------------------------------------------------------------

uint32_t s_visFrame = 0;

// The frustum the current portal walk started from: narrowing keeps this one's near/far planes.
struct PortalFrustum;
const int32_t PORTAL_MAX_PLANES = 24; // 6 frustum planes + up to 18 portal edge planes
const int32_t PORTAL_MAX_DEPTH = 8;

struct PortalFrustum {
    float planes[PORTAL_MAX_PLANES][4];
    int32_t count;
};

PortalFrustum s_baseFrustum;

// MH2O per-chunk header (12 bytes) and layer info (24 bytes)
struct Mh2oHeader {
    uint32_t ofsInformation;
    uint32_t layerCount;
    uint32_t ofsRender;
};

struct Mh2oInfo {
    uint16_t liquidType;
    uint16_t vertexFormat; // 0 height+depth, 1 height+uv, 2 depth only (flat), 3 height+uv+depth
    float minHeight;
    float maxHeight;
    uint8_t xOffset;
    uint8_t yOffset;
    uint8_t width;
    uint8_t height;
    uint32_t ofsMask;
    uint32_t ofsHeightMap;
};

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

    ExtractFrustum(s_viewProjT);

    // Data-driven fog: enable it whenever the fog begins within the view distance, so geometry
    // between the fog start and the far plane is hazed even when the fog end lies beyond the far
    // clip (linear fog handles the partial factor). Colours and distances come from Light.dbc.
    float fogEnd = CWorld::GetFogEnd();
    s_fogActive = fogEnd > 1.0f && CWorld::GetFogStart() < CWorld::GetFarClip();

    if (s_fogActive) {
        const C3Vector& fc = CWorld::GetFogColor();
        uint32_t packed = (static_cast<uint32_t>(fc.x * 255.0f) << 16)
            | (static_cast<uint32_t>(fc.y * 255.0f) << 8)
            | static_cast<uint32_t>(fc.z * 255.0f);
        float fogStart = CWorld::GetFogStart();

        GxRsSet(GxRs_FogColor, static_cast<int32_t>(packed));
        GxRsSet(GxRs_FogStart, *reinterpret_cast<int32_t*>(&fogStart));
        GxRsSet(GxRs_FogEnd, *reinterpret_cast<int32_t*>(&fogEnd));
    }

    // Frustum-cull the buildings' props: only those in view animate and draw, which spares the
    // scene thousands of out-of-view models. They hang off the reference defs now, so this walks
    // those rather than the stand-in's instances.
    CMap::ForEachMapObjDoodad([](CM2Model* model, void*) {
        C3Vector c = DoodadCullCenter(model);

        // DoodadCullRadius scales a MODEL-SPACE radius, so it still needs the placement scale --
        // and that now lives in the model's matrix rather than in a parallel array. Recover it as
        // the length of the matrix's first row, which is exact because the matrix is built as
        // rotation times a uniform scale. Passing 1.0f here would under-cull every scaled prop.
        const C44Matrix& m = model->matrixB4;
        float scale = sqrtf(m.a0 * m.a0 + m.a1 * m.a1 + m.a2 * m.a2);

        if (scale <= 0.0f) {
            scale = 1.0f;
        }

        float r = DoodadCullRadius(model, scale);
        bool vis = SphereVisible(c, r);

        model->SetVisible(vis ? 1 : 0);
        model->SetAnimating(vis ? 1 : 0);
    }, nullptr);

    s_viewUpdated = true;
}

void TerrainRender() {
    if (!s_mapName[0]) {
        return;
    }

    EnsureShaders();

    bool haveShaded = s_useTerrainShader && s_terrainVS && s_terrainVS->Valid() && s_terrainPS && s_terrainPS->Valid();
    bool haveFallback = s_uiVertexShader[0] && s_uiVertexShader[0]->Valid() && s_uiPixelShader && s_uiPixelShader->Valid();

    if (!haveShaded && !haveFallback) {
        return;
    }

    GxRsPush();

    // The view/frustum/visibility half normally runs here, but the frame may have run it already so
    // that the shadow map could see this frame's visibility before anything drew. Running the
    // doodad sweep twice would be pure waste.
    if (!s_viewUpdated) {
        TerrainUpdateView();
    }

    s_viewUpdated = false;


    // The terrain chunks draw through the ported map (CMap::Render -> CWorldScene::RenderTerrain)
    // since 2026-09-25, and the two stand-in passes that used to draw them are gone. What still
    // reads this copy of the chunk geometry is the blob shadow receiver walk, which re-draws a
    // chunk's own triangles under a decal; retiring that is what finally frees the tile data.
    (void)haveShaded;

    // The stand-in's WMO visibility sweep used to run here, walking every instance and recursing
    // through its portals to set WmoGroup::visFrame and visDepth. Every reader of those two flags
    // has now gone -- RenderWmos, BlobShadowDrawWmo and LiquidRender -- so the sweep, the portal
    // recursion and the frustum narrowing it did were pure per-frame waste. The reference's own
    // portal walk (CMapObj::WalkPortals) is what decides visibility now.

    // The liquids belong to CMap::Render, which draws both buckets through the reference material.
    // This used to call LiquidRender(0) as well, and the comment here already said the liquids were
    // CMap::Render's -- so every opaque surface was drawn twice from the moment the material draw
    // started working.

    GxRsPop();
}


// ------------------------------------------------------------------------------------------------
// Liquids (MH2O). The reference builds Liquid::CInstance objects per chunk layer and draws them in
// two sorted buckets (FUN_008a2240(cam, 0) opaque inside CMap::Render, (cam, 1) transparent in the
// world frame's transparent block) with the animated LiquidType surface textures.
// ------------------------------------------------------------------------------------------------

// One entry per liquid vertex. A terrain MH2O layer is at most 9x9 = 81, but a WMO MLIQ grid runs
// to (xtiles+1)*(ytiles+1) and is much larger, so this grows to the biggest surface drawn; a fixed
// 81 read off the end of the array for any WMO liquid.
std::vector<CImVector> s_liquidColor;
uint8_t s_liquidAlpha = 0;



// ------------------------------------------------------------------------------------------------
// Blob shadows (the reference's CWorldScene FUN_00793980: per scene entity with a model, project
// Textures\ShadowBlob.blp onto the ground through FUN_007e4480). The chunk mesh under the entity
// is redrawn with the blob texture and planar texture coordinates centred on the entity, so the
// decal follows the terrain exactly; depth is tested less-equal against the identical geometry.
// ------------------------------------------------------------------------------------------------

HTEXTURE s_shadowBlob = nullptr;
bool s_shadowBlobTried = false;
bool s_blobActive = false;

// Blob shadows on WMO floors. Same technique as the terrain receivers: re-draw the receiver's own
// triangles through the same vertex program and per-instance matrix the base pass used, with a
// depth-EQUAL test, and derive the blob coordinate from the vertex XY (instance-local here).
// Scratch index list for the WMO shadow receiver gather, reused every call.
std::vector<uint16_t> s_shadowIndices;

// How dark a blob is. A constant at the reference's own call site, not a light ratio.
// DAT_009f98d8
static const float BLOB_SHADOW_STRENGTH = 0.4f;

// The scene's frustum, not the stand-in's copy of one.


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

