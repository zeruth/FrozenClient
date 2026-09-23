#include "world/Terrain.hpp"
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

// One 16x16 map chunk: 145 vertices (9x9 outer grid interleaved with 8x8 inner). Terrain is
// blended per-pixel in a single pass: the base layer plus up to three overlay layers, each
// sampled at the tiled diffuse UV, weighted by a combined alpha texture (R/G/B = layers 1/2/3).
struct TerrainChunk {
    // World-space positions, used by everything on the CPU side (bounds, height and colour
    // queries, liquid and detail placement).
    C3Vector position[145];

    // The same mesh relative to `origin`, which is what actually gets drawn. World coordinates run
    // to +-17066, and transforming them by a matrix that also carries -cameraPos makes the shader
    // subtract two large nearly-equal numbers per vertex; the surviving depth is only good to a
    // few millimetres and wobbles as the camera moves, which is what makes coplanar geometry
    // fight. The reference keeps chunk-local vertices and folds the origin into a per-chunk
    // matrix instead (FUN_007d0050 -> FUN_00790440), so the cancellation happens once on the CPU.
    C3Vector localPos[145];
    C3Vector origin = { 0.0f, 0.0f, 0.0f };
    C2Vector texcoord[145];
    CImVector color[145]; // rgb = lit colour (rebuilt when the outdoor light changes), a = 255
    uint8_t ndotl[145];   // static per-vertex sun term (with baked shadow) for re-lighting
    CImVector mccv[145];  // static per-vertex baked colour (127 = neutral) for re-lighting

    int32_t layerTex[MAX_LAYERS]; // MTEX index per layer
    int32_t layerEffect[MAX_LAYERS]; // MCLY effectId per layer (GroundEffectTexture id, 0 = none)
    int32_t nLayers = 0;

    // Detail (ground effect) doodads: per 8x8 cell the dominant layer (MCNK predTex, 2 bits per
    // cell) picks the GroundEffectTexture; noEffectDoodad marks cells that get none. The
    // placements are generated once per chunk from that data (see BuildDetailDoodads).
    uint8_t predTex[16] = { 0 };
    uint8_t noEffectDoodad[8] = { 0 };
    struct DetailPlacement* details = nullptr;
    uint32_t detailCount = 0;
    struct DetailBatch* detailBatch = nullptr; // built lazily when the chunk comes within range
    bool detailDirty = true;                   // placements or lighting changed since the batch was built

    CImVector alphaCombined[64 * 64]; // r/g/b = overlay-layer coverage, feeds alphaTexture
    HTEXTURE alphaTexture = nullptr;

    uint32_t holes = 0; // MCNK holes bitmask: cells the reference does not render (cave/mine mouths)
    uint32_t areaID = 0; // MCNK header +0x34: the AreaTable.dbc row this chunk belongs to

    C3Vector boundsMin = { 0.0f, 0.0f, 0.0f };
    C3Vector boundsMax = { 0.0f, 0.0f, 0.0f };

    // MH2O liquid layers on this chunk (world-space surface meshes), see ParseLiquid
    struct ChunkLiquid* liquids = nullptr;
    uint32_t liquidCount = 0;

    bool valid = false;
};

// One scattered detail doodad: GroundEffectDoodad id, ground position, yaw, scale
struct DetailPlacement {
    int32_t doodadID;
    C3Vector position;
    float yaw;
    float scale;
};

// A chunk's detail doodads merged into one vertex set with one draw range per model type
struct DetailBatch {
    C3Vector* positions = nullptr;
    C2Vector* uvs = nullptr;
    CImVector* colors = nullptr;
    uint16_t* indices = nullptr;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    struct Range {
        HTEXTURE texture;
        uint32_t blend;
        uint32_t indexStart;
        uint32_t indexCount;
    };

    Range* ranges = nullptr;
    uint32_t rangeCount = 0;
    bool complete = false; // false while some model was still loading: rebuilt next time
};

// One MH2O layer of a chunk: a height-field mesh over the cells the layer covers
struct ChunkLiquid {
    C3Vector* verts = nullptr;
    C2Vector* uvs = nullptr;
    // Per-vertex colour, present only when the layer carries MH2O depth data. Alpha comes from the
    // water depth so shallows fade out at the shoreline the way the reference does; without it the
    // whole surface is one flat alpha and the water meets the bank on a hard line.
    CImVector* colors = nullptr;
    // 0..255 per vertex: how deep this vertex is, kept so the colours above can be rebuilt when the
    // light changes without re-reading the ADT.
    uint8_t* depthRamp = nullptr;
    uint16_t* indices = nullptr;
    uint32_t vertCount = 0;
    uint32_t indexCount = 0;
    int32_t liquidType = 0; // LiquidType.dbc id
    int32_t kind = 0;       // LiquidType.m_type: 0 water, 1 ocean, 2 magma, 3 slime
    C3Vector boundsMin = { 0.0f, 0.0f, 0.0f };
    C3Vector boundsMax = { 0.0f, 0.0f, 0.0f };

    int32_t group = -1; // owning WMO group index for MLIQ surfaces, -1 for terrain (MH2O) layers

    // Cell rectangle within the chunk's 8x8 grid and the covered-cell mask, for point queries
    uint8_t xOffset = 0;
    uint8_t yOffset = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t cellMask[8] = { 0 };
};

struct TerrainTile {
    int32_t x = -1;
    int32_t y = -1;
    bool loaded = false;
    HTEXTURE textures[256] = { nullptr };
    uint32_t textureCount = 0;
    TerrainChunk chunks[256];

    // M2 doodads placed on this tile (MDDF); the world scene draws them, we own the references
    CM2Model** doodads = nullptr;
    float* doodadScale = nullptr;  // placement scale, so the cull sphere matches the world size
    uint32_t doodadCount = 0;

    // Placement uniqueIds this tile loaded (a large object listed in several tiles is owned by the
    // first tile to load it; the rest skip it). Removed from the global registry on unload.
    uint32_t* ownedUnique = nullptr;
    uint32_t ownedUniqueCount = 0;

    // WMO building instances placed on this tile (MODF)
    struct WmoInstance* wmos = nullptr;
    uint32_t wmoCount = 0;

    bool needsRebake = false; // outdoor light changed; relight this tile's vertices (spread over frames)
};

// One drawable range of a WMO group sharing a single material/texture
struct WmoBatch {
    HTEXTURE texture = nullptr;
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    uint32_t blend = 0;       // WMO material blend mode (0 opaque, 1 alpha-key, >=2 alpha)
    bool twoSided = false;    // MOMT F_UNCULLED (0x4): render both faces, no backface culling
    bool unlit = false;       // MOMT F_UNLIT (0x1): ignore lighting, render full-bright
    bool unfogged = false;    // MOMT F_UNFOGGED (0x2): exclude this batch from distance fog
};

// One WMO group's geometry (world space). Each group is its own mesh so the format's 16-bit
// indices never overflow, no matter how large the whole WMO is.
struct WmoGroup {
    C3Vector* positions = nullptr;
    CImVector* colors = nullptr;
    C2Vector* texcoords = nullptr;
    uint32_t vertexCount = 0;

    uint16_t* indices = nullptr;
    uint32_t indexCount = 0;

    WmoBatch* batches = nullptr;
    uint32_t batchCount = 0;

    uint8_t* ndotl = nullptr; // static per-vertex sun term for exterior groups (re-lighting)
    uint8_t* ao = nullptr;    // (unused legacy occlusion slot)
    CImVector* mocvAdd = nullptr; // exterior MOCV: additive local light (dark except near glows)
    bool interior = false;    // interior groups keep their baked MOCV (torch-lit, do not cycle)
    C3Vector groupAmbient = { 0.35f, 0.35f, 0.35f }; // this room's avg interior colour, for units in it

    C3Vector boundsMin = { 0.0f, 0.0f, 0.0f };
    C3Vector boundsMax = { 0.0f, 0.0f, 0.0f };

    // Uniform XY grid over this group's up-facing triangles, built once on first blob-shadow use.
    //
    // Without it, casting a blob onto a WMO scanned EVERY triangle of every overlapping group, for
    // every caster, every frame. Ebon Hold is a single enormous WMO, so with a few NPCs present
    // that ran to millions of triangle tests per frame and the client appeared to hang -- sampled
    // with tools/samplehang.py, which put the main thread in BlobShadowDrawWmo on 6 of 9 in-module
    // samples.
    // A second XY grid over ALL triangles, for the indoor test. The shadow grid cannot be reused:
    // it deliberately keeps only up-facing triangles, and deciding whether a point is inside a room
    // needs the ceiling above it, which faces down.
    std::vector<std::vector<uint32_t>> containGrid;
    float containCellSize = 0.0f;
    float containMinX = 0.0f;
    float containMinY = 0.0f;
    int32_t containCellsX = 0;
    int32_t containCellsY = 0;
    bool containGridBuilt = false;

    std::vector<std::vector<uint32_t>> shadowGrid; // cell -> triangle start offsets into `indices`
    float shadowCellSize = 0.0f;
    float shadowGridMinX = 0.0f;
    float shadowGridMinY = 0.0f;
    int32_t shadowCellsX = 0;
    int32_t shadowCellsY = 0;
    bool shadowGridBuilt = false;

    // MOGP portal reference range (into WmoInstance::portalRefs) and the frame stamp of the last
    // visibility pass that reached this group (see WmoUpdateVisibility)
    uint16_t portalStart = 0;
    uint16_t portalCount = 0;
    uint32_t visFrame = 0;
    int32_t visDepth = 0x7FFFFFFF; // shallowest portal depth this group was reached at this frame

    // The group as the reference's CMapObjGroup queries see it: MOPY, the MOBN/MOBR tree, the raw
    // MOCV, and pointers at `positions` / `indices` above. Queries run in the same instance-local
    // space as `positions` (world minus WmoInstance::origin, rotation applied), where the
    // reference keeps WMO-local vertices and transforms the query through the placement instead.
    CImVector* mocv = nullptr;
    // The group's vertices in the model's own space, before the placement yaw. Only the BSP
    // queries use these; everything that draws uses `positions`. See the note where it is filled.
    C3Vector* queryVerts = nullptr;
    CMapObjGroup objGroup;
};

// A WMO portal polygon (MOPT) in world space: a vertex range into WmoInstance::portalVerts plus
// the polygon's plane, oriented as the file stores it so MOPR's side values keep their meaning.
struct WmoPortal {
    uint16_t startVertex = 0;
    uint16_t vertexCount = 0;
    C3Vector normal = { 0.0f, 0.0f, 0.0f };
    float dist = 0.0f;
};

// MOPR: a group's link through a portal to a neighbouring group. side is +1/-1: which side of the
// portal plane the referencing group lies on. The camera passes through the portal only when it
// stands on that same side.
struct WmoPortalRef {
    uint16_t portal = 0;
    uint16_t group = 0;
    int16_t side = 0;
};

// A placed WMO: its groups plus the shared material textures
struct WmoInstance {
    WmoGroup* groups = nullptr;
    uint32_t groupCount = 0;

    // Portal graph (MOPV/MOPT/MOPR), world space. Empty for WMOs without portals, which then fall
    // back to a plain per-group frustum test.
    C3Vector* portalVerts = nullptr;
    uint32_t portalVertCount = 0;
    WmoPortal* portals = nullptr;
    uint32_t portalCount = 0;
    WmoPortalRef* portalRefs = nullptr;
    uint32_t portalRefCount = 0;
    uint32_t groupsExpected = 0; // MOHD's group count, to spot groups that failed to load


    // MLIQ surfaces (one per group that carries liquid), same mesh type as the terrain layers
    struct ChunkLiquid* liquids = nullptr;
    uint32_t liquidCount = 0;

    // Vertices are stored relative to this, the instance's placement position, and the origin is
    // folded back in by a per-instance matrix at draw time. Same reason as TerrainChunk::localPos:
    // absolute world coordinates transformed by a matrix carrying -cameraPos lose most of the
    // depth precision to cancellation. Bounds below stay in world space.
    C3Vector origin = { 0.0f, 0.0f, 0.0f };

    // The placement yaw, kept so a world-space point can be brought back into the model's own
    // space for the BSP queries (WmoGroup::queryVerts live there).
    float yawCos = 1.0f;
    float yawSin = 0.0f;

    // World-space bounding box over all groups, for whole-instance frustum culling (the reference
    // culls a WMO hierarchically before descending into its groups).
    C3Vector bboxMin = { 0.0f, 0.0f, 0.0f };
    C3Vector bboxMax = { 0.0f, 0.0f, 0.0f };
    bool hasBounds = false;

    HTEXTURE* textures = nullptr; // one per MOMT material
    uint32_t textureCount = 0;

    // M2 doodads placed inside the WMO (MODD); the world scene draws them, we own the references
    CM2Model** doodads = nullptr;
    float* doodadScale = nullptr;  // placement scale, so the cull sphere matches the world size
    C3Vector* doodadAmbient = nullptr; // per-doodad baked lighting colour (MODD colour field)
    uint32_t doodadCount = 0;

    // Average interior (MOCV) brightness; interior doodads are lit by this constant value so they
    // match the torch-lit walls and do not cycle with the outdoor day/night like exterior props.
    C3Vector interiorAmbient = { 0.35f, 0.35f, 0.35f };

    // The root as the group queries reach it: the MOMT copy, the MOHD flags and ambient colour
    CMapObj mapObj;
};

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
TerrainTile s_tiles[MAX_TILES];
char s_mapName[128] = { 0 };

// Registry of every placement uniqueId currently loaded, so an object that appears in more than one
// overlapping ADT tile (large WMOs, cross-boundary doodads) is placed exactly once, like the
// reference. The sentinel 0xFFFFFFFF is never registered and always placed.
std::set<uint32_t> s_loadedUnique;
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

uint16_t s_indices[768];
bool s_indicesBuilt = false;

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

// local->clip for one chunk, transposed for the vertex constants: translate by (origin - camera),
// then the camera-at-origin view and the projection. Every pass that draws chunk geometry must use
// this same matrix with the same local vertices, or their depths will not agree.
C44Matrix ChunkMatrixT(const C3Vector& origin) {
    C3Vector t = { origin.x - s_cameraPos.x, origin.y - s_cameraPos.y, origin.z - s_cameraPos.z };

    C44Matrix m = s_viewNoTranslate;
    m.Translate(t);

    C44Matrix mvp = m * s_projNative;

    return mvp.Transpose();
}

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

// True if the world-space AABB is at least partly inside the frustum
bool BoxVisible(const C3Vector& mn, const C3Vector& mx) {
    for (int32_t p = 0; p < 6; p++) {
        const float* pl = s_frustum[p];
        float px = pl[0] >= 0.0f ? mx.x : mn.x;
        float py = pl[1] >= 0.0f ? mx.y : mn.y;
        float pz = pl[2] >= 0.0f ? mx.z : mn.z;

        if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.0f) {
            return false;
        }
    }

    return true;
}

void BuildIndices() {
    int32_t n = 0;

    for (int32_t j = 0; j < 8; j++) {
        for (int32_t i = 0; i < 8; i++) {
            uint16_t tl = j * 17 + i;
            uint16_t tr = j * 17 + i + 1;
            uint16_t bl = (j + 1) * 17 + i;
            uint16_t br = (j + 1) * 17 + i + 1;
            uint16_t c = j * 17 + 9 + i;

            s_indices[n++] = c; s_indices[n++] = tl; s_indices[n++] = tr;
            s_indices[n++] = c; s_indices[n++] = tr; s_indices[n++] = br;
            s_indices[n++] = c; s_indices[n++] = br; s_indices[n++] = bl;
            s_indices[n++] = c; s_indices[n++] = bl; s_indices[n++] = tl;
        }
    }

    s_indicesBuilt = true;
}

// Build a chunk's triangle indices while skipping the cells marked as holes. The 16-bit holes
// field maps to a 4x4 grid of 2x2 cell blocks via the standard masks the client uses.
uint32_t BuildHoleIndices(uint32_t holes, uint16_t* out) {
    static const uint16_t hMask[4] = { 0x1111, 0x2222, 0x4444, 0x8888 };
    static const uint16_t vMask[4] = { 0x000F, 0x00F0, 0x0F00, 0xF000 };

    uint32_t n = 0;

    for (int32_t j = 0; j < 8; j++) {
        for (int32_t i = 0; i < 8; i++) {
            if (holes & hMask[i >> 1] & vMask[j >> 1]) {
                continue;
            }

            uint16_t tl = j * 17 + i;
            uint16_t tr = j * 17 + i + 1;
            uint16_t bl = (j + 1) * 17 + i;
            uint16_t br = (j + 1) * 17 + i + 1;
            uint16_t c = j * 17 + 9 + i;

            out[n++] = c; out[n++] = tl; out[n++] = tr;
            out[n++] = c; out[n++] = tr; out[n++] = br;
            out[n++] = c; out[n++] = br; out[n++] = bl;
            out[n++] = c; out[n++] = bl; out[n++] = tl;
        }
    }

    return n;
}

uint32_t ReadChunkTag(const uint8_t* data, uint32_t offset, uint32_t& size) {
    uint32_t tag = *reinterpret_cast<const uint32_t*>(data + offset);
    size = *reinterpret_cast<const uint32_t*>(data + offset + 4);
    return tag;
}

// ADT chunk tags are stored byte-reversed on disk
constexpr uint32_t FourCC(const char* s) {
    return (static_cast<uint32_t>(s[0]) << 24) | (static_cast<uint32_t>(s[1]) << 16) | (static_cast<uint32_t>(s[2]) << 8) | static_cast<uint32_t>(s[3]);
}

void DecompressAlpha(const uint8_t* in, const uint8_t* inEnd, uint8_t* out) {
    int32_t o = 0;
    const uint8_t* p = in;

    while (o < 4096 && p < inEnd) {
        uint8_t cmd = *p++;
        int32_t count = cmd & 0x7F;
        bool fill = (cmd & 0x80) != 0;

        if (fill) {
            if (p >= inEnd) {
                break;
            }

            uint8_t value = *p++;

            while (count-- > 0 && o < 4096) {
                out[o++] = value;
            }
        } else {
            while (count-- > 0 && o < 4096 && p < inEnd) {
                out[o++] = *p++;
            }
        }
    }

    while (o < 4096) {
        out[o++] = 0;
    }
}

// Decode one layer's alpha map (4-bit 2048B, 8-bit 4096B, or RLE compressed) into 64x64 8-bit.
// Pre-Cataclysm ADTs only store 63x63 valid alpha; reconstruct the last row/column from their
// neighbours unless the chunk's do_not_fix_alpha_map flag is set, or every chunk shows an edge seam.
void DecodeAlphaMap(const uint8_t* src, uint32_t srcSize, bool compressed, bool fixLastRowCol, uint8_t* out /* 4096 */) {
    if (compressed) {
        DecompressAlpha(src, src + srcSize, out);
    } else if (srcSize >= 4096) {
        memcpy(out, src, 4096);
    } else {
        for (int32_t i = 0; i < 4096; i++) {
            uint8_t byte = src[i >> 1];
            uint8_t nibble = (i & 1) ? ((byte >> 4) & 0xF) : (byte & 0xF);
            out[i] = static_cast<uint8_t>(nibble * 17);
        }
    }

    if (fixLastRowCol) {
        for (int32_t x = 0; x < 64; x++) {
            out[63 * 64 + x] = out[62 * 64 + x];
        }

        for (int32_t y = 0; y < 64; y++) {
            out[y * 64 + 63] = out[y * 64 + 62];
        }
    }
}

void AlphaTexCallback(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mip, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Latch) {
        stride = 4 * w;
        texels = userArg;
    }
}

// --- Sky dome ---------------------------------------------------------------------------------
// A camera-centred dome coloured by the LightIntBand sky gradient (horizon .. zenith). Drawn first
// with depth off so terrain and objects paint over it, matching the reference's sky backdrop.
// Sky dome rings, as zenith angles in turns of pi (0 = straight up, 0.5 = horizon, 1 = nadir).
//
// The paragraph that used to stand here described the dome BEFORE the table was read out of the
// binary, flagged the zenith-angle reading as uncertain, and argued for carrying the gradient down
// to the horizon and blending into fog. It was left in place when the reference's own table landed
// and then contradicted the paragraph below it and the code under both. Removed 2026-09-23; the
// uncertainty it flagged is settled.
//
// One thing in it was a measurement and is kept, because it predicts what a run will show. Colouring
// the reference's table literally puts a hard edge at 45 degrees wherever the fog band differs from
// the horizon band, and in some zones it differs badly -- the Death Knight start reads sky
// 62,154,197 against fog 0,62,85 at noon, which will look like a blue ball with a dark skirt. That
// is what the reference does, so it is the expected appearance rather than a defect to tune away.
// If it looks wrong on screen, check the band mapping before changing the geometry.
//
// The reference's dome, read out of the binary rather than tuned: FUN_007f2470 builds 24 segments
// and 7 rings whose zenith angles are the table at 0x00a41a90 times pi, and the azimuth step at
// 0x00a41cec is exactly 1/24. Both were checked against WoW.exe directly (2026-09-23), as was the
// vertex count the colour writer implies: 1 + 5*24 + 1 = 122.
//
// Note how little of the sphere the gradient occupies. Every band sits between the zenith and 45
// degrees elevation; from there down it is one flat sheet of the fog band, which is why the dome
// meets the fogged terrain horizon with no seam and no blending -- they are the same colour.
// frozen previously spread 12 rings evenly and lerped a z gradient across them, which put the
// gradient far too low and made the horizon band far too thin.
const int32_t SKY_RINGS = 6;
const int32_t SKY_SEGS = 24;
const float SKY_RING_ZENITH[SKY_RINGS + 1] = {
    0.0f, 0.17f, 0.20f, 0.23f, 0.24f, 0.25f, 1.0f
};
const int32_t SKY_VERTS = (SKY_RINGS + 1) * (SKY_SEGS + 1);
const float SKY_RADIUS = 150.0f; // inside the minimum far clip (183) so the dome is never clipped

C3Vector s_skyPos[SKY_VERTS];
C2Vector s_skyUv[SKY_VERTS];
CImVector s_skyCol[SKY_VERTS];
uint16_t s_skyIdx[SKY_RINGS * SKY_SEGS * 6];
int32_t s_skyIdxCount = 0;
bool s_skyBuilt = false;
HTEXTURE s_skyWhite = nullptr;

// GxTexCreate asserts width >= 8, so the dome cannot have the 2x2 white sheet it wants --
// the sheet is uniform, so widening it to the smallest legal size costs 256 bytes and
// changes nothing on screen. On Windows the assertion is compiled out and a 2x2 texture
// went through unnoticed; on Android assertions are live and it killed the client a few
// seconds into the world, the moment the sky first drew.
const int32_t SKY_WHITE_DIM = 8;
CImVector s_skyWhitePixels[SKY_WHITE_DIM * SKY_WHITE_DIM];

void SkyWhiteCallback(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mip, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Latch) {
        stride = 4 * w;
        texels = s_skyWhitePixels;
    }
}

// Depth range of the sky viewport in the reference (DAT_00adeef0 / DAT_00adeef4)
// The sky's depth range. Both ends are the far value on purpose.
//
// The reference constant for the minimum is 0.9990234375, and squeezing the dome into
// [0.9990234375, 1.0] is meant to put every sky pixel behind everything else. With this client's
// near and far planes it does not: depth is z_buf = (f/(f-n)) * (1 - n/z), so with n = 0.2 and
// f = 727 the buffer already reads 0.9990234 at **159 yards**. Every terrain pixel beyond that has a
// LARGER depth than the dome's nearest ring, so the dome passed the less-equal test and, being
// GxBlend_Add, was added on top of ground that was already fogged -- a bright ring at the onset
// distance, which is what the user reported seeing.
//
// Pinning both ends to 1.0 makes every sky pixel land at exactly the cleared depth, so less-equal
// admits it only where nothing else has drawn. That is the property the squeezed range was reaching
// for. Recorded as a deliberate divergence from the reference constant rather than a port.
const float SKY_VIEWPORT_MIN_Z = 1.0f;
const float SKY_VIEWPORT_MAX_Z = 1.0f;

// The sky's own scenes, separate from the world M2 scene. File scope so SkyRelease can free them
// on unload; as function-local statics they survived every map change and leaked.
CM2Scene* s_starsScene = nullptr;
CM2Model* s_starsModel = nullptr;
bool s_starsTried = false;
CM2Scene* s_skyboxScene = nullptr;
CM2Model* s_skyboxModel = nullptr;
char s_skyboxLoaded[260] = { 0 };

void BuildSkyDome() {
    const float PI = 3.14159265f;
    int32_t v = 0;

    for (int32_t ring = 0; ring <= SKY_RINGS; ring++) {
        float theta = SKY_RING_ZENITH[ring] * PI; // measured from the zenith
        float ct = cosf(theta);
        float st = sinf(theta);

        for (int32_t seg = 0; seg <= SKY_SEGS; seg++) {
            float phi = static_cast<float>(seg) / SKY_SEGS * 2.0f * PI;
            s_skyPos[v].x = st * cosf(phi) * SKY_RADIUS;
            s_skyPos[v].y = st * sinf(phi) * SKY_RADIUS;
            s_skyPos[v].z = ct * SKY_RADIUS;
            s_skyUv[v].x = 0.0f;
            s_skyUv[v].y = 0.0f;
            v++;
        }
    }

    int32_t n = 0;

    for (int32_t ring = 0; ring < SKY_RINGS; ring++) {
        for (int32_t seg = 0; seg < SKY_SEGS; seg++) {
            uint16_t i0 = static_cast<uint16_t>(ring * (SKY_SEGS + 1) + seg);
            uint16_t i1 = static_cast<uint16_t>(i0 + 1);
            uint16_t i2 = static_cast<uint16_t>(i0 + (SKY_SEGS + 1));
            uint16_t i3 = static_cast<uint16_t>(i2 + 1);

            s_skyIdx[n++] = i0; s_skyIdx[n++] = i2; s_skyIdx[n++] = i1;
            s_skyIdx[n++] = i1; s_skyIdx[n++] = i2; s_skyIdx[n++] = i3;
        }
    }

    s_skyIdxCount = n;
    s_skyBuilt = true;
}

// Rebuild a chunk's vertex colours from its static lighting inputs and the current outdoor light.
void RebakeChunkColors(TerrainChunk& chunk) {
    const C3Vector& amb = CWorld::GetOutdoorAmbient();
    const C3Vector& dif = CWorld::GetOutdoorDiffuse();
    chunk.detailDirty = true;

    for (int32_t k = 0; k < 145; k++) {
        float nd = chunk.ndotl[k] / 255.0f;
        float fr = (amb.x + dif.x * nd) * (chunk.mccv[k].r / 127.0f) * 255.0f;
        float fg = (amb.y + dif.y * nd) * (chunk.mccv[k].g / 127.0f) * 255.0f;
        float fb = (amb.z + dif.z * nd) * (chunk.mccv[k].b / 127.0f) * 255.0f;
        chunk.color[k].r = static_cast<uint8_t>(fr > 255.0f ? 255.0f : fr);
        chunk.color[k].g = static_cast<uint8_t>(fg > 255.0f ? 255.0f : fg);
        chunk.color[k].b = static_cast<uint8_t>(fb > 255.0f ? 255.0f : fb);
        // Alpha carries the ambient ratio: what fraction of this vertex's lit colour survives with
        // the sun removed. The terrain shader lerps to it wherever the baked shadow map is set.
        float lit = (amb.x + dif.x * nd) + (amb.y + dif.y * nd) + (amb.z + dif.z * nd);
        float ambient = amb.x + amb.y + amb.z;
        float ratio = lit > 0.0001f ? ambient / lit : 1.0f;
        chunk.color[k].a = static_cast<uint8_t>((ratio > 1.0f ? 1.0f : ratio) * 255.0f);
    }
}

void ParseLiquid(TerrainChunk& chunk, int32_t chunkIndex, const uint8_t* mh2o, uint32_t mh2oSize);
void ParseLegacyLiquid(TerrainChunk& chunk, const uint8_t* mcnk, uint32_t mcnkSize);
void BuildDetailDoodads(TerrainChunk& chunk, int32_t tileX, int32_t tileY, int32_t chunkIndex);
void FreeDetailBatch(TerrainChunk& chunk);

int32_t LiquidAt(const C3Vector& pos, float& surfaceZ);

// Height of the chunk surface at a point inside it (bilinear over the 9x9 outer grid)
float ChunkHeightAt(const TerrainChunk& chunk, float x, float y) {
    float rowF = (chunk.position[0].x - x) / UNIT_SIZE;
    float colF = (chunk.position[0].y - y) / UNIT_SIZE;

    if (rowF < 0.0f) rowF = 0.0f;
    if (rowF > 7.999f) rowF = 7.999f;
    if (colF < 0.0f) colF = 0.0f;
    if (colF > 7.999f) colF = 7.999f;

    int32_t r = static_cast<int32_t>(rowF);
    int32_t c = static_cast<int32_t>(colF);
    float fr = rowF - r;
    float fc = colF - c;

    // Outer vertex (r, c) is at index r*17 + c (rows of 9 outer + 8 inner vertices)
    float z00 = chunk.position[r * 17 + c].z;
    float z01 = chunk.position[r * 17 + c + 1].z;
    float z10 = chunk.position[(r + 1) * 17 + c].z;
    float z11 = chunk.position[(r + 1) * 17 + c + 1].z;

    return (z00 * (1.0f - fc) + z01 * fc) * (1.0f - fr) + (z10 * (1.0f - fc) + z11 * fc) * fr;
}

// Scatter the chunk's detail doodads (the reference's DetailDoodad batch builder): per 8x8 cell the
// dominant layer's GroundEffectTexture names up to four doodads with weights and an amount; cells
// flagged in noEffectDoodad and holes get none. Positions are deterministic per chunk so a tile
// reloads identically.
void BuildDetailDoodads(TerrainChunk& chunk, int32_t tileX, int32_t tileY, int32_t chunkIndex) {
    if (!chunk.valid || chunk.nLayers <= 0) {
        return;
    }

    uint32_t seed = static_cast<uint32_t>(tileX * 73856093) ^ static_cast<uint32_t>(tileY * 19349663) ^ static_cast<uint32_t>(chunkIndex * 83492791) ^ 0x9E3779B9u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };

    // First pass counts, second fills
    DetailPlacement* out = nullptr;
    uint32_t count = 0;

    for (int32_t pass = 0; pass < 2; pass++) {
        uint32_t n = 0;

        for (int32_t cell = 0; cell < 64; cell++) {
            int32_t row = cell / 8;
            int32_t col = cell % 8;

            if (chunk.noEffectDoodad[row] & (1 << col)) {
                continue;
            }

            if (chunk.holes & (1 << ((row / 2) * 4 + (col / 2)))) {
                continue;
            }

            int32_t layer = (chunk.predTex[cell / 4] >> ((cell % 4) * 2)) & 0x3;

            if (layer >= chunk.nLayers) {
                layer = 0;
            }

            int32_t effect = chunk.layerEffect[layer];

            if (effect <= 0) {
                continue;
            }

            auto rec = g_groundEffectTextureDB.GetRecord(effect);

            if (!rec) {
                continue;
            }

            int32_t totalWeight = 0;

            for (int32_t d = 0; d < 4; d++) {
                if (rec->m_doodadID[d] > 0) {
                    totalWeight += rec->m_doodadWeight[d] > 0 ? rec->m_doodadWeight[d] : 0;
                }
            }

            if (!totalWeight) {
                continue;
            }

            int32_t amount = rec->m_amount;

            if (amount <= 0) amount = 1;
            if (amount > 12) amount = 12;

            for (int32_t k = 0; k < amount; k++) {
                float u = rnd();
                float v = rnd();
                float pick = rnd() * totalWeight;
                float yaw = rnd() * 6.2831853f;
                float scale = 0.85f + rnd() * 0.3f;

                if (pass == 0) {
                    n++;
                    continue;
                }

                int32_t doodadID = 0;
                float acc = 0.0f;

                for (int32_t d = 0; d < 4; d++) {
                    if (rec->m_doodadID[d] <= 0 || rec->m_doodadWeight[d] <= 0) {
                        continue;
                    }

                    acc += static_cast<float>(rec->m_doodadWeight[d]);

                    if (pick <= acc || d == 3) {
                        doodadID = rec->m_doodadID[d];
                        break;
                    }
                }

                if (!doodadID) {
                    for (int32_t d = 0; d < 4; d++) {
                        if (rec->m_doodadID[d] > 0) { doodadID = rec->m_doodadID[d]; break; }
                    }
                }

                DetailPlacement& p = out[n++];
                p.doodadID = doodadID;
                p.position.x = chunk.position[0].x - (row + u) * UNIT_SIZE;
                p.position.y = chunk.position[0].y - (col + v) * UNIT_SIZE;
                p.position.z = ChunkHeightAt(chunk, p.position.x, p.position.y);
                p.yaw = yaw;
                p.scale = scale;
            }
        }

        if (pass == 0) {
            count = n;

            if (!count) {
                return;
            }

            out = static_cast<DetailPlacement*>(SMemAlloc(count * sizeof(DetailPlacement), __FILE__, __LINE__, 0));
            seed = static_cast<uint32_t>(tileX * 73856093) ^ static_cast<uint32_t>(tileY * 19349663) ^ static_cast<uint32_t>(chunkIndex * 83492791) ^ 0x9E3779B9u;
        } else {
            chunk.details = out;
            chunk.detailCount = n;
        }
    }
}

void ParseChunk(TerrainChunk& chunk, const uint8_t* mcnk, uint32_t mcnkSize) {
    const uint8_t* hdr = mcnk;
    uint32_t mcnkFlags = *reinterpret_cast<const uint32_t*>(hdr + 0x00);
    bool doNotFixAlpha = (mcnkFlags & 0x8000) != 0;
    uint32_t ofsHeight = *reinterpret_cast<const uint32_t*>(hdr + 0x14);
    uint32_t ofsNormal = *reinterpret_cast<const uint32_t*>(hdr + 0x18);
    uint32_t ofsLayer = *reinterpret_cast<const uint32_t*>(hdr + 0x1C);
    uint32_t ofsAlpha = *reinterpret_cast<const uint32_t*>(hdr + 0x24);
    uint32_t ofsShadow = *reinterpret_cast<const uint32_t*>(hdr + 0x2C);
    uint32_t ofsVertexColor = *reinterpret_cast<const uint32_t*>(hdr + 0x74);
    chunk.areaID = *reinterpret_cast<const uint32_t*>(hdr + 0x34);
    chunk.holes = *reinterpret_cast<const uint32_t*>(hdr + 0x3C);
    float posX = *reinterpret_cast<const float*>(hdr + 0x68);
    float posY = *reinterpret_cast<const float*>(hdr + 0x6C);
    float posZ = *reinterpret_cast<const float*>(hdr + 0x70);

    const float* heights = ofsHeight ? reinterpret_cast<const float*>(mcnk + ofsHeight) : nullptr;
    const int8_t* normals = ofsNormal ? reinterpret_cast<const int8_t*>(mcnk + ofsNormal) : nullptr;
    // MCCV: baked per-vertex colours (BGRA, 127 = neutral) the reference modulates terrain by
    const CImVector* vertexColors = ((mcnkFlags & 0x40) && ofsVertexColor) ? reinterpret_cast<const CImVector*>(mcnk + ofsVertexColor) : nullptr;
    // MCSH: 64x64 1-bit baked shadow map; a set bit removes the sun's diffuse at that texel
    const uint8_t* shadowMap = ((mcnkFlags & 0x1) && ofsShadow) ? (mcnk + ofsShadow) : nullptr;

    if (!heights) {
        return;
    }

    int32_t k = 0;

    for (int32_t rowIndex = 0; rowIndex < 17; rowIndex++) {
        bool inner = (rowIndex & 1) != 0;
        int32_t count = inner ? 8 : 9;
        int32_t r = rowIndex / 2;

        for (int32_t c = 0; c < count; c++) {
            float fx = inner ? (r + 0.5f) : static_cast<float>(r);
            float fy = inner ? (c + 0.5f) : static_cast<float>(c);

            chunk.position[k].x = posX - fx * UNIT_SIZE;
            chunk.position[k].y = posY - fy * UNIT_SIZE;
            chunk.position[k].z = posZ + heights[k];

            chunk.texcoord[k].x = fy / 8.0f;
            chunk.texcoord[k].y = fx / 8.0f;

            // Light the vertex with the same coloured outdoor sun the models use
            // (CWorld::LightingCallback): colour = ambient + diffuse * saturate(N . lightDir).
            float ndotl = 0.7f;

            if (normals) {
                const C3Vector& sun = CWorld::GetOutdoorDirection();
                float nx = normals[k * 3 + 0] / 127.0f;
                float ny = normals[k * 3 + 1] / 127.0f;
                float nz = normals[k * 3 + 2] / 127.0f;
                float d = nx * sun.x + ny * sun.y + nz * sun.z;
                ndotl = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
            }

            // Store the static lighting inputs; the final colour is (re)built by RebakeChunkColors
            // whenever the outdoor light changes, so terrain tracks the day/night cycle.
            chunk.ndotl[k] = static_cast<uint8_t>(ndotl * 255.0f);
            chunk.mccv[k].r = vertexColors ? vertexColors[k].r : 127;
            chunk.mccv[k].g = vertexColors ? vertexColors[k].g : 127;
            chunk.mccv[k].b = vertexColors ? vertexColors[k].b : 127;
            chunk.mccv[k].a = 0xFF;

            k++;
        }
    }

    // Build the initial vertex colours from the current outdoor light
    RebakeChunkColors(chunk);

    // Chunk-local copy of the mesh for drawing (see TerrainChunk::localPos)
    //
    // The origin is snapped to a 5-yard grid in XY. terrain_vs tiles the layer textures from
    // position.xy * 0.2, and that position is chunk-LOCAL; a chunk is 33.33 yards, 6.67 repeats,
    // so an origin at the chunk corner restarted the tiling two thirds of a repeat off from the
    // neighbouring chunk -- every chunk read as its own square with a seam at each edge. With the
    // origin on the 5-yard grid, local * 0.2 differs from world * 0.2 by a whole number of repeats
    // and the phase is continuous across the map. Z is unaffected (it does not feed the UV).
    chunk.origin = chunk.position[0];
    chunk.origin.x = floorf(chunk.origin.x / 5.0f) * 5.0f;
    chunk.origin.y = floorf(chunk.origin.y / 5.0f) * 5.0f;

    for (int32_t k = 0; k < 145; k++) {
        chunk.localPos[k].x = chunk.position[k].x - chunk.origin.x;
        chunk.localPos[k].y = chunk.position[k].y - chunk.origin.y;
        chunk.localPos[k].z = chunk.position[k].z - chunk.origin.z;
    }

    // World-space bounding box of the chunk, for view-frustum culling
    chunk.boundsMin = chunk.position[0];
    chunk.boundsMax = chunk.position[0];

    for (int32_t v = 1; v < 145; v++) {
        const C3Vector& p = chunk.position[v];
        chunk.boundsMin.x = p.x < chunk.boundsMin.x ? p.x : chunk.boundsMin.x;
        chunk.boundsMin.y = p.y < chunk.boundsMin.y ? p.y : chunk.boundsMin.y;
        chunk.boundsMin.z = p.z < chunk.boundsMin.z ? p.z : chunk.boundsMin.z;
        chunk.boundsMax.x = p.x > chunk.boundsMax.x ? p.x : chunk.boundsMax.x;
        chunk.boundsMax.y = p.y > chunk.boundsMax.y ? p.y : chunk.boundsMax.y;
        chunk.boundsMax.z = p.z > chunk.boundsMax.z ? p.z : chunk.boundsMax.z;
    }

    // The baked shadow map goes into the blend texture's alpha channel, which the layer blend does
    // not use, so the terrain shader can sample it per pixel. Folding it into the per-vertex term
    // instead quantised it to the 9x9 height grid and made mountain shadows blocky.
    for (int32_t sr = 0; sr < 64; sr++) {
        for (int32_t sc = 0; sc < 64; sc++) {
            bool shadowed = shadowMap && (shadowMap[sr * 8 + (sc >> 3)] & (1 << (sc & 7)));
            chunk.alphaCombined[sr * 64 + sc].a = shadowed ? 0x00 : 0xFF;
        }
    }

    // Layers
    chunk.nLayers = 0;
    chunk.layerTex[0] = chunk.layerTex[1] = chunk.layerTex[2] = chunk.layerTex[3] = 0;
    chunk.layerEffect[0] = chunk.layerEffect[1] = chunk.layerEffect[2] = chunk.layerEffect[3] = 0;
    memcpy(chunk.predTex, hdr + 0x40, sizeof(chunk.predTex));
    memcpy(chunk.noEffectDoodad, hdr + 0x50, sizeof(chunk.noEffectDoodad));
    for (int32_t i = 0; i < 64 * 64; i++) {
        chunk.alphaCombined[i].r = 0;
        chunk.alphaCombined[i].g = 0;
        chunk.alphaCombined[i].b = 0;
    }

    if (ofsLayer) {
        uint32_t mclySize = *reinterpret_cast<const uint32_t*>(mcnk + ofsLayer - 4);
        uint32_t layerCount = mclySize / 16;

        if (layerCount > MAX_LAYERS) {
            layerCount = MAX_LAYERS;
        }

        const uint8_t* mcly = mcnk + ofsLayer;
        const uint8_t* alphaBase = ofsAlpha ? (mcnk + ofsAlpha) : nullptr;
        uint32_t alphaSize = ofsAlpha ? *reinterpret_cast<const uint32_t*>(mcnk + ofsAlpha - 4) : 0;

        for (uint32_t l = 0; l < layerCount; l++) {
            const uint8_t* entry = mcly + l * 16;
            uint32_t textureId = *reinterpret_cast<const uint32_t*>(entry + 0x00);
            uint32_t flags = *reinterpret_cast<const uint32_t*>(entry + 0x04);
            uint32_t ofsInAlpha = *reinterpret_cast<const uint32_t*>(entry + 0x08);
            uint32_t effectId = *reinterpret_cast<const uint32_t*>(entry + 0x0C);

            chunk.layerTex[l] = static_cast<int32_t>(textureId);
            chunk.layerEffect[l] = static_cast<int32_t>(effectId);

            if (l == 0 || !(flags & 0x100) || !alphaBase) {
                continue;
            }

            uint32_t nextOfs = alphaSize;

            for (uint32_t m = l + 1; m < layerCount; m++) {
                uint32_t mFlags = *reinterpret_cast<const uint32_t*>(mcly + m * 16 + 0x04);

                if (mFlags & 0x100) {
                    nextOfs = *reinterpret_cast<const uint32_t*>(mcly + m * 16 + 0x08);
                    break;
                }
            }

            uint32_t thisSize = (nextOfs > ofsInAlpha) ? (nextOfs - ofsInAlpha) : 0;
            bool compressed = (flags & 0x200) != 0;

            uint8_t decoded[4096];
            DecodeAlphaMap(alphaBase + ofsInAlpha, thisSize, compressed, !doNotFixAlpha, decoded);

            // Pack into the combined texture's channel for this overlay layer
            for (int32_t t = 0; t < 4096; t++) {
                uint8_t a = decoded[t];

                if (l == 1) {
                    chunk.alphaCombined[t].r = a;
                } else if (l == 2) {
                    chunk.alphaCombined[t].g = a;
                } else if (l == 3) {
                    chunk.alphaCombined[t].b = a;
                }
            }
        }

        chunk.nLayers = static_cast<int32_t>(layerCount);

        // NOTE: do not touch the alpha channel here. It carries the chunk's baked MCSH shadow map
        // for the terrain shader; unused overlay channels are already 0.
    }

    // Upload the per-chunk combined alpha map (clamped, no wrap)
    chunk.alphaTexture = TextureCreate(
        64, 64,
        GxTex_Argb8888, GxTex_Argb8888,
        CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1),
        chunk.alphaCombined,
        AlphaTexCallback,
        __FILE__, __LINE__
    );

    chunk.valid = true;
}

// WMO doodads sit inside buildings, so the reference lights them by the interior/ambient term
// rather than the direct outdoor sun; using the zone ambient keeps them from being blown out.
// The skybox model is shown at full brightness (its own texture/colours carry the sky's look); it
// never takes distance fog.
void SkyboxLightingCallback(CM2Model* model, CM2Lighting* lighting, void* arg) {
    C3Vector white = { 1.0f, 1.0f, 1.0f };
    lighting->AddAmbient(white);
}

void WmoDoodadLightingCallback(CM2Model* model, CM2Lighting* lighting, void* arg) {
    const C3Vector* interiorAmbient = static_cast<const C3Vector*>(arg);
    lighting->AddAmbient(interiorAmbient ? *interiorAmbient : CWorld::GetOutdoorAmbient());

    // Match the distance fog the surrounding WMO and terrain use (M2 fog is shader-side, per model).
    float fogEnd = CWorld::GetFogEnd();
    float fogStart = CWorld::GetFogStart();

    if (fogEnd > 1.0f && fogEnd > fogStart && fogStart < CWorld::GetFarClip()) {
        lighting->SetFog(CWorld::GetFogColor(), fogStart, fogEnd);
    }
}

// Load a WMO (root + group files), transform all group geometry into world space using the
// placement derived from the MODF entry, and build per-material textured batches. The placement
// convention (local X is up; local Y/Z are the horizontal plane rotated by the yaw) was verified
// against the MODF world-space bounding box.

// WMO group liquid (MLIQ): a height grid in group-local space with per-tile flags. The surface is
// baked to world space with the group's placement, exactly like its geometry, and rendered through
// the same liquid buckets as the terrain water (reference: FUN_00793d20 builds Liquid::CInstance
// per group and logs "WMO: Liquid type [%d] not found, defaulting to water!" for unknown ids).
void LoadWmoLiquid(WmoInstance& out, uint32_t groupIndex, uint32_t nGroups, const uint8_t* mliq, uint32_t mliqSize,
                   uint32_t groupLiquid, uint16_t mohdFlags, const C3Vector& worldPos, float cs, float sn) {
    uint32_t xverts = *reinterpret_cast<const uint32_t*>(mliq + 0);
    uint32_t yverts = *reinterpret_cast<const uint32_t*>(mliq + 4);
    uint32_t xtiles = *reinterpret_cast<const uint32_t*>(mliq + 8);
    uint32_t ytiles = *reinterpret_cast<const uint32_t*>(mliq + 12);
    const float* base = reinterpret_cast<const float*>(mliq + 16);

    if (!xverts || !yverts || xverts > 256 || yverts > 256 || xtiles + 1 != xverts || ytiles + 1 != yverts) {
        return;
    }

    const uint8_t* verts = mliq + 30;
    const uint8_t* tiles = verts + xverts * yverts * 8;

    if (30 + xverts * yverts * 8 + xtiles * ytiles > mliqSize) {
        return;
    }

    // Liquid type: with MOHD flag 0x4 the group's value is a LiquidType id; otherwise the legacy
    // basic type (0 water, 1 ocean, 2 magma, 3 slime) maps onto LiquidType ids 1..4, taken from
    // the group when it names one and from the tile flags when it does not (15 = "per tile").
    int32_t liquidType;

    if (mohdFlags & 0x4) {
        liquidType = static_cast<int32_t>(groupLiquid);
    } else if (groupLiquid < 15) {
        liquidType = static_cast<int32_t>(groupLiquid) + 1;
    } else {
        liquidType = -1; // resolved from the first covered tile below
    }

    uint32_t covered = 0;

    for (uint32_t t = 0; t < xtiles * ytiles; t++) {
        uint8_t f = tiles[t];

        if ((f & 0x0F) == 0x0F) {
            continue;
        }

        if (liquidType < 0) {
            liquidType = (f & 0x3) + 1;
        }

        covered++;
    }

    if (!covered || liquidType < 0) {
        return;
    }

    if (!out.liquids) {
        out.liquids = static_cast<ChunkLiquid*>(SMemAlloc(nGroups * sizeof(ChunkLiquid), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    }

    ChunkLiquid& liq = out.liquids[out.liquidCount];
    liq.group = static_cast<int32_t>(groupIndex);
    liq.liquidType = liquidType;

    auto rec = g_liquidTypeDB.GetRecord(liquidType);
    liq.kind = rec ? rec->m_type : 0;

    liq.vertCount = xverts * yverts;
    liq.verts = static_cast<C3Vector*>(SMemAlloc(liq.vertCount * sizeof(C3Vector), __FILE__, __LINE__, 0));
    liq.uvs = static_cast<C2Vector*>(SMemAlloc(liq.vertCount * sizeof(C2Vector), __FILE__, __LINE__, 0));
    liq.indices = static_cast<uint16_t*>(SMemAlloc(covered * 6 * sizeof(uint16_t), __FILE__, __LINE__, 0));

    for (uint32_t j = 0; j < yverts; j++) {
        for (uint32_t i = 0; i < xverts; i++) {
            uint32_t k = j * xverts + i;
            // Each vertex is 8 bytes: flow/depth data then the height (the second float)
            float h = *reinterpret_cast<const float*>(verts + k * 8 + 4);
            // WMO model space is Z-up, so the grid runs along the two horizontal components from
            // the MLIQ base corner and the per-vertex height is the vertical one. (An earlier pass
            // here put the height into component 0 to match a vertex transform that was itself
            // wrong; both are corrected now and they agree.)
            float lx = base[0] + i * UNIT_SIZE;
            float ly = base[1] + j * UNIT_SIZE;
            float lz = h;

            // Same placement transform as the group's MOVT vertices
            liq.verts[k].x = worldPos.x + (lx * cs - ly * sn);
            liq.verts[k].y = worldPos.y + (lx * sn + ly * cs);
            liq.verts[k].z = worldPos.z + lz;
            liq.uvs[k].x = i * 0.25f;
            liq.uvs[k].y = j * 0.25f;

            if (k == 0) {
                liq.boundsMin = liq.boundsMax = liq.verts[k];
            } else {
                const C3Vector& v = liq.verts[k];
                liq.boundsMin.x = v.x < liq.boundsMin.x ? v.x : liq.boundsMin.x;
                liq.boundsMin.y = v.y < liq.boundsMin.y ? v.y : liq.boundsMin.y;
                liq.boundsMin.z = v.z < liq.boundsMin.z ? v.z : liq.boundsMin.z;
                liq.boundsMax.x = v.x > liq.boundsMax.x ? v.x : liq.boundsMax.x;
                liq.boundsMax.y = v.y > liq.boundsMax.y ? v.y : liq.boundsMax.y;
                liq.boundsMax.z = v.z > liq.boundsMax.z ? v.z : liq.boundsMax.z;
            }
        }
    }

    uint32_t n = 0;

    for (uint32_t j = 0; j < ytiles; j++) {
        for (uint32_t i = 0; i < xtiles; i++) {
            if ((tiles[j * xtiles + i] & 0x0F) == 0x0F) {
                continue;
            }

            uint16_t a = static_cast<uint16_t>(j * xverts + i);
            uint16_t b = static_cast<uint16_t>(a + 1);
            uint16_t c = static_cast<uint16_t>(a + xverts);
            uint16_t d = static_cast<uint16_t>(c + 1);
            liq.indices[n++] = a; liq.indices[n++] = c; liq.indices[n++] = b;
            liq.indices[n++] = b; liq.indices[n++] = c; liq.indices[n++] = d;
        }
    }

    liq.indexCount = n;
    out.liquidCount++;
}

void LoadWmoInstance(const char* rootPath, const C3Vector& worldPos, float ry, uint32_t doodadSet, WmoInstance& out) {
    void* rootData = nullptr;
    size_t rootSize = 0;

    if (!SFile::Load(nullptr, rootPath, &rootData, &rootSize, 0, 0, nullptr) || !rootData) {
        return;
    }

    auto root = static_cast<const uint8_t*>(rootData);

    uint32_t nGroups = 0;
    uint32_t nMaterials = 0;
    const char* motx = nullptr;
    const uint8_t* momt = nullptr;
    const char* modn = nullptr;      // doodad filename block
    const uint8_t* modd = nullptr;   // doodad placements
    uint32_t moddCount = 0;
    uint32_t set0First = 0;   // doodad set 0 (the global set, always displayed)
    uint32_t set0Count = 0;
    uint32_t setNFirst = 0;   // the selected set (displayed in addition to set 0)
    uint32_t setNCount = 0;
    bool modsPresent = false;
    uint16_t mohdFlags = 0;          // 0x4: MOGP.groupLiquid is a LiquidType id directly
    const float* mopv = nullptr;     // portal vertices (3 floats each, WMO-local)
    uint32_t mopvCount = 0;
    const uint8_t* mopt = nullptr;   // portal infos (20 bytes: startVertex, count, plane)
    uint32_t moptCount = 0;
    const uint8_t* mopr = nullptr;   // portal references (8 bytes: portal, group, side, pad)
    uint32_t moprCount = 0;

    uint32_t off = 0;

    while (off + 8 <= rootSize) {
        uint32_t sz;
        uint32_t tag = ReadChunkTag(root, off, sz);
        const uint8_t* body = root + off + 8;

        if (tag == FourCC("MOHD")) {
            nMaterials = *reinterpret_cast<const uint32_t*>(body + 0);
            nGroups = *reinterpret_cast<const uint32_t*>(body + 4);
            mohdFlags = (sz >= 62) ? *reinterpret_cast<const uint16_t*>(body + 60) : 0;

            // MOHD ambient colour (CImVector BGRA at +0x1C): the WMO's declared interior ambient
            // light. The reference lights interior contents from this floor, so use it as the
            // doodad interior ambient. Keep the neutral fallback only when the WMO declares none.
            uint8_t ab = body[28], ag = body[29], ar = body[30];

            if (ar || ag || ab) {
                out.interiorAmbient.x = ar / 255.0f;
                out.interiorAmbient.y = ag / 255.0f;
                out.interiorAmbient.z = ab / 255.0f;
            }

            out.mapObj.m_mohdFlags = mohdFlags;
            out.mapObj.m_ambientColor.b = ab;
            out.mapObj.m_ambientColor.g = ag;
            out.mapObj.m_ambientColor.r = ar;
            out.mapObj.m_ambientColor.a = body[31];
        } else if (tag == FourCC("MOTX")) {
            motx = reinterpret_cast<const char*>(body);
        } else if (tag == FourCC("MOMT")) {
            momt = body;

            // The group queries read the materials through CMapObj, so keep a copy that outlives
            // the root file buffer
            if (sz >= sizeof(SMOMaterial)) {
                out.mapObj.m_materialCount = sz / sizeof(SMOMaterial);
                out.mapObj.m_materials = static_cast<SMOMaterial*>(SMemAlloc(out.mapObj.m_materialCount * sizeof(SMOMaterial), __FILE__, __LINE__, 0));
                memcpy(out.mapObj.m_materials, body, out.mapObj.m_materialCount * sizeof(SMOMaterial));
            }
        } else if (tag == FourCC("MODN")) {
            modn = reinterpret_cast<const char*>(body);
        } else if (tag == FourCC("MODD")) {
            modd = body;
            moddCount = sz / 40;
        } else if (tag == FourCC("MOPV")) {
            mopv = reinterpret_cast<const float*>(body);
            mopvCount = sz / 12;
        } else if (tag == FourCC("MOPT")) {
            mopt = body;
            moptCount = sz / 20;
        } else if (tag == FourCC("MOPR")) {
            mopr = body;
            moprCount = sz / 8;
        } else if (tag == FourCC("MODS") && sz >= 32) {
            // Doodad set 0 is the global set the reference always displays; the placement's
            // selected set (when > 0 and valid) is shown in addition to it, not instead of it.
            uint32_t setCount = sz / 32;
            set0First = *reinterpret_cast<const uint32_t*>(body + 0 * 32 + 20);
            set0Count = *reinterpret_cast<const uint32_t*>(body + 0 * 32 + 24);

            if (doodadSet > 0 && doodadSet < setCount) {
                setNFirst = *reinterpret_cast<const uint32_t*>(body + doodadSet * 32 + 20);
                setNCount = *reinterpret_cast<const uint32_t*>(body + doodadSet * 32 + 24);
            }

            modsPresent = true;
        }

        off += 8 + sz;
    }

    // The doodad ranges to spawn: set 0 always, plus the selected set when distinct. Without a MODS
    // chunk the WMO has no sets and every MODD entry is placed.
    struct { uint32_t start; uint32_t end; } ranges[2];
    uint32_t rangeCount = 0;

    if (modsPresent) {
        ranges[rangeCount].start = set0First;
        ranges[rangeCount].end = set0First + set0Count;
        rangeCount++;

        if (setNCount && setNFirst != set0First) {
            ranges[rangeCount].start = setNFirst;
            ranges[rangeCount].end = setNFirst + setNCount;
            rangeCount++;
        }
    } else {
        ranges[rangeCount].start = 0;
        ranges[rangeCount].end = moddCount;
        rangeCount++;
    }

    // Materials -> textures (MOMT material's texture1 is an offset into the MOTX name block)
    if (nMaterials && momt && motx) {
        out.textures = static_cast<HTEXTURE*>(SMemAlloc(nMaterials * sizeof(HTEXTURE), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
        out.textureCount = nMaterials;

        for (uint32_t i = 0; i < nMaterials; i++) {
            uint32_t texOfs = *reinterpret_cast<const uint32_t*>(momt + i * 64 + 0x0C);
            const char* texName = motx + texOfs;
            CStatus status;

            // WRAP on both axes, always. This used to derive clamping from MOMT flags 0x40 and
            // 0x80, with a comment asserting the reference clamps decals, windows and bordered
            // textures. **It does not**, checked 2026-09-23. Every WMO material texture in the
            // reference is loaded through one helper, FUN_007d9990, which takes a filename and
            // nothing else and builds its flags as CGxTexFlags(GxTex_LinearMipLinear, GxTex_Wrap,
            // GxTex_Wrap, 0, 0, 0, 1). Its caller FUN_007d7710 is unmistakably the MOMT loop: it
            // indexes 64-byte materials and reads texture1 at +0x0C and texture2 at +0x18, which
            // is the layout read just above. All five call sites reach the same hardcoded flags,
            // so there is no path on which the reference clamps one of these.
            out.textures[i] = TextureCreate(texName, CGxTexFlags(GxTex_LinearMipLinear, GxTex_Wrap, GxTex_Wrap, 0, 0, 0, 1), &status, 0);
        }
    }

    // Group file path: replace the root's ".wmo" extension with "_NNN.wmo"
    char base[260];
    SStrCopy(base, rootPath, sizeof(base));
    char* dot = SStrChrR(base, '.');

    if (dot) {
        *dot = '\0';
    }

    // Build each group as its own world-space mesh (per-group 16-bit indices never overflow)
    out.groups = static_cast<WmoGroup*>(SMemAlloc((nGroups ? nGroups : 1) * sizeof(WmoGroup), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    out.groupCount = 0;

    float cs = cosf(ry);
    float sn = sinf(ry);

    out.yawCos = cs;
    out.yawSin = sn;

    out.origin = worldPos;
    out.groupsExpected = nGroups;


    // Portal graph, transformed with the same proper rotation the geometry gets below. The MOPT
    // plane normal is rotated as a direction and its distance recomputed from a transformed portal
    // vertex, so the plane still contains the polygon and MOPR's side signs stay valid.
    if (mopv && mopt && mopr && mopvCount && moptCount && moprCount) {
        out.portalVertCount = mopvCount;
        out.portalVerts = static_cast<C3Vector*>(SMemAlloc(mopvCount * sizeof(C3Vector), __FILE__, __LINE__, 0));

        for (uint32_t i = 0; i < mopvCount; i++) {
            float lx = mopv[i * 3 + 0];
            float ly = mopv[i * 3 + 1];
            float lz = mopv[i * 3 + 2];
            out.portalVerts[i].x = worldPos.x + (lx * cs - ly * sn);
            out.portalVerts[i].y = worldPos.y + (lx * sn + ly * cs);
            out.portalVerts[i].z = worldPos.z + lz;
        }

        out.portalCount = moptCount;
        out.portals = static_cast<WmoPortal*>(SMemAlloc(moptCount * sizeof(WmoPortal), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));

        for (uint32_t i = 0; i < moptCount; i++) {
            const uint8_t* e = mopt + i * 20;
            WmoPortal& pt = out.portals[i];
            pt.startVertex = *reinterpret_cast<const uint16_t*>(e + 0);
            pt.vertexCount = *reinterpret_cast<const uint16_t*>(e + 2);

            float nx = *reinterpret_cast<const float*>(e + 4);
            float ny = *reinterpret_cast<const float*>(e + 8);
            float nz = *reinterpret_cast<const float*>(e + 12);
            pt.normal.x = nx * cs - ny * sn;
            pt.normal.y = nx * sn + ny * cs;
            pt.normal.z = nz;

            if (pt.startVertex < mopvCount && pt.vertexCount && static_cast<uint32_t>(pt.startVertex) + pt.vertexCount <= mopvCount) {
                const C3Vector& v0 = out.portalVerts[pt.startVertex];
                pt.dist = -(pt.normal.x * v0.x + pt.normal.y * v0.y + pt.normal.z * v0.z);
            } else {
                pt.vertexCount = 0; // malformed; never traversed
            }
        }

        out.portalRefCount = moprCount;
        out.portalRefs = static_cast<WmoPortalRef*>(SMemAlloc(moprCount * sizeof(WmoPortalRef), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));

        for (uint32_t i = 0; i < moprCount; i++) {
            const uint8_t* e = mopr + i * 8;
            out.portalRefs[i].portal = *reinterpret_cast<const uint16_t*>(e + 0);
            out.portalRefs[i].group = *reinterpret_cast<const uint16_t*>(e + 2);
            out.portalRefs[i].side = *reinterpret_cast<const int16_t*>(e + 4);
        }
    }

    // Accumulate interior MOCV to derive a constant interior ambient for the doodads
    double mocvSumR = 0.0, mocvSumG = 0.0, mocvSumB = 0.0;
    uint32_t mocvSamples = 0;

    for (uint32_t g = 0; g < nGroups; g++) {
        char groupPath[260];
        SStrPrintf(groupPath, sizeof(groupPath), "%s_%03u.wmo", base, g);

        void* gdata = nullptr;
        size_t gsize = 0;

        if (!SFile::Load(nullptr, groupPath, &gdata, &gsize, 0, 0, nullptr) || !gdata) {
            continue;
        }

        auto gb = static_cast<const uint8_t*>(gdata);
        const uint8_t* mogp = nullptr;
        uint32_t mogpSize = 0;
        uint32_t go = 0;

        while (go + 8 <= gsize) {
            uint32_t sz;
            uint32_t tag = ReadChunkTag(gb, go, sz);

            if (tag == FourCC("MOGP")) {
                mogp = gb + go + 8;
                mogpSize = sz;
                break;
            }

            go += 8 + sz;
        }

        if (mogp) {
            // MOGP flags: bit 0x8 marks an exterior (outdoor) group, which is lit by the sun and
            // cycles with the day; interior groups keep their baked MOCV torch lighting.
            uint32_t mogpFlags = *reinterpret_cast<const uint32_t*>(mogp + 8);
            bool exterior = (mogpFlags & 0x8) != 0;
            uint16_t mogpPortalStart = *reinterpret_cast<const uint16_t*>(mogp + 36);
            uint16_t mogpPortalCount = *reinterpret_cast<const uint16_t*>(mogp + 38);
            uint32_t mogpGroupLiquid = *reinterpret_cast<const uint32_t*>(mogp + 52);

            const uint8_t* sub = mogp + 68;
            uint32_t subSize = mogpSize - 68;
            uint32_t so = 0;

            const float* movt = nullptr;
            uint32_t movtCount = 0;
            const float* monr = nullptr; // WMO MONR normals are 3 floats per vertex (already unit)
            const float* motv = nullptr;
            const uint16_t* movi = nullptr;
            uint32_t moviCount = 0;
            const uint8_t* moba = nullptr;
            uint32_t mobaCount = 0;
            const CImVector* mocv = nullptr; // baked per-vertex colours for interior groups
            uint32_t mocvCount = 0;
            const uint8_t* mliq = nullptr;   // group liquid (header + vertex grid + tile flags)
            uint32_t mliqSize = 0;
            const SMOPoly* mopy = nullptr;   // per-face flags + material
            uint32_t mopyCount = 0;
            const CAaBspNode* mobn = nullptr; // the face BSP
            uint32_t mobnCount = 0;
            const uint16_t* mobr = nullptr;  // the BSP leaves' face lists
            uint32_t mobrCount = 0;

            while (so + 8 <= subSize) {
                uint32_t sz;
                uint32_t tag = ReadChunkTag(sub, so, sz);
                const uint8_t* body = sub + so + 8;

                if (tag == FourCC("MOVT")) { movt = reinterpret_cast<const float*>(body); movtCount = sz / 12; }
                else if (tag == FourCC("MONR")) { monr = reinterpret_cast<const float*>(body); }
                else if (tag == FourCC("MOTV")) { motv = reinterpret_cast<const float*>(body); }
                else if (tag == FourCC("MOVI")) { movi = reinterpret_cast<const uint16_t*>(body); moviCount = sz / 2; }
                else if (tag == FourCC("MOBA")) { moba = body; mobaCount = sz / 24; }
                else if (tag == FourCC("MOCV")) { mocv = reinterpret_cast<const CImVector*>(body); mocvCount = sz / 4; }
                else if (tag == FourCC("MLIQ")) { mliq = body; mliqSize = sz; }
                else if (tag == FourCC("MOPY")) { mopy = reinterpret_cast<const SMOPoly*>(body); mopyCount = sz / 2; }
                else if (tag == FourCC("MOBN")) { mobn = reinterpret_cast<const CAaBspNode*>(body); mobnCount = sz / 16; }
                else if (tag == FourCC("MOBR")) { mobr = reinterpret_cast<const uint16_t*>(body); mobrCount = sz / 2; }

                so += 8 + sz;
            }

            if (movt && movi && movtCount && movtCount <= 65535) {
                WmoGroup& grp = out.groups[out.groupCount];
                grp.vertexCount = movtCount;
                grp.indexCount = moviCount;
                grp.positions = static_cast<C3Vector*>(SMemAlloc(movtCount * sizeof(C3Vector), __FILE__, __LINE__, 0));
                grp.colors = static_cast<CImVector*>(SMemAlloc(movtCount * sizeof(CImVector), __FILE__, __LINE__, 0));
                grp.texcoords = static_cast<C2Vector*>(SMemAlloc(movtCount * sizeof(C2Vector), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
                grp.indices = static_cast<uint16_t*>(SMemAlloc(moviCount * sizeof(uint16_t), __FILE__, __LINE__, 0));
                grp.ndotl = static_cast<uint8_t*>(SMemAlloc(movtCount, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
                grp.ao = static_cast<uint8_t*>(SMemAlloc(movtCount, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
                grp.mocvAdd = static_cast<CImVector*>(SMemAlloc(movtCount * sizeof(CImVector), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
                grp.interior = !exterior;
                grp.portalStart = mogpPortalStart;
                grp.portalCount = mogpPortalCount;

                double grpMocvR = 0.0, grpMocvG = 0.0, grpMocvB = 0.0;
                uint32_t grpMocvN = 0;

                for (uint32_t i = 0; i < movtCount; i++) {
                    float lx = movt[i * 3 + 0]; // up
                    float ly = movt[i * 3 + 1];
                    float lz = movt[i * 3 + 2];

                    // Proper rotation (determinant +1); the reference never reflects placements
            // Model space -> world, matching the convention the PLACEMENT already uses. A MODF
            // position converts as world = (CORNER - p.z, CORNER - p.x, p.y), so in model space
            // component 2 feeds world X, component 0 feeds world Y, and component 1 is UP. The
            // yaw therefore rotates the (x, z) pair, and component 1 goes straight to world Z.
            //
            // This used to treat component 0 as up and rotate (y, z), i.e. the axes were rolled by
            // one. Buildings came out 2-6x too tall and correspondingly too narrow while their
            // centres stayed about right -- exactly what an axis permutation about the centre
            // looks like, and what the placement self-check reported.
                    // WMO model space is Z-UP and already aligned with the world axes: the only
                    // thing the placement adds is a yaw about the vertical and the instance's
                    // position. The ADT->world axis shuffle (CORNER - z, CORNER - x, y) applies to
                    // the placement POSITION, which is stored in ADT space; it does not apply again
                    // to the model's own vertices.
                    //
                    // Two earlier attempts here rolled the axes (treating component 0, then
                    // component 1, as up) and both produced buildings 2-6x too tall and too narrow
                    // with roughly correct centres -- the placement self-check reported exactly
                    // that, and it is what an axis permutation about the centre looks like.
                    grp.positions[i].x = worldPos.x + (lx * cs - ly * sn);
                    grp.positions[i].y = worldPos.y + (lx * sn + ly * cs);
                    grp.positions[i].z = worldPos.z + lz;

                    if (motv) {
                        grp.texcoords[i].x = motv[i * 2 + 0];
                        grp.texcoords[i].y = motv[i * 2 + 1];
                    }

                    if (!exterior && mocv) {
                        // Interior groups carry baked local lighting (MOCV), but ~37% of Acherus's
                        // interior verts are pitch black -- the reference lifts them with the WMO's
                        // MOHD ambient, blended by the MOCV alpha: color = MOCV + ambient*(1 - a/255).
                        // out.interiorAmbient still holds the MOHD colour here (the average-MOCV
                        // override runs after this loop), so use it as the ambient floor.
                        float aw = 1.0f - mocv[i].a / 255.0f;
                        float fr = mocv[i].r + out.interiorAmbient.x * 255.0f * aw;
                        float fg = mocv[i].g + out.interiorAmbient.y * 255.0f * aw;
                        float fb = mocv[i].b + out.interiorAmbient.z * 255.0f * aw;
                        grp.colors[i].r = static_cast<uint8_t>(fr > 255.0f ? 255.0f : fr);
                        grp.colors[i].g = static_cast<uint8_t>(fg > 255.0f ? 255.0f : fg);
                        grp.colors[i].b = static_cast<uint8_t>(fb > 255.0f ? 255.0f : fb);
                        grp.colors[i].a = 0xFF;

                        mocvSumR += mocv[i].r;
                        mocvSumG += mocv[i].g;
                        mocvSumB += mocv[i].b;
                        mocvSamples++;

                        grpMocvR += mocv[i].r;
                        grpMocvG += mocv[i].g;
                        grpMocvB += mocv[i].b;
                        grpMocvN++;
                    } else if (!exterior) {
                        // Interior group with no baked MOCV: light it by the WMO's flat interior
                        // ambient (MOHD), never the outdoor sun. grp.interior keeps it out of the
                        // day/night rebake, so it stays constant like the torch-lit walls.
                        float ir = out.interiorAmbient.x * 255.0f;
                        float ig = out.interiorAmbient.y * 255.0f;
                        float ib = out.interiorAmbient.z * 255.0f;
                        grp.colors[i].r = static_cast<uint8_t>(ir > 255.0f ? 255.0f : ir);
                        grp.colors[i].g = static_cast<uint8_t>(ig > 255.0f ? 255.0f : ig);
                        grp.colors[i].b = static_cast<uint8_t>(ib > 255.0f ? 255.0f : ib);
                        grp.colors[i].a = 0xFF;
                    } else {
                        float ndotl = 0.7f;

                        if (monr) {
                            float nx = monr[i * 3 + 0];
                            float ny = monr[i * 3 + 1];
                            float nz = monr[i * 3 + 2];
                            // Rotate the local normal into world space (same rotation as the vertex)
                            float wnx = nx * cs - ny * sn;
                            float wny = nx * sn + ny * cs;
                            float wnz = nz;
                            const C3Vector& sun = CWorld::GetOutdoorDirection();
                            float d = wnx * sun.x + wny * sun.y + wnz * sun.z;
                            ndotl = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
                        }

                        grp.ndotl[i] = static_cast<uint8_t>(ndotl * 255.0f);

                        // Exterior groups: MOCV is an ADDITIVE local-light term (near-black over the
                        // whole surface, bright only near torches/glows), NOT an occlusion multiplier.
                        // Verified against the data: the Acherus deck's MOCV averages ~9/255, so
                        // multiplying by it would render the sun-lit deck almost black. Store it and
                        // add it to the cycling sun light.
                        if (mocv) {
                            grp.mocvAdd[i] = mocv[i];
                        }

                        const C3Vector& amb = CWorld::GetOutdoorAmbient();
                        const C3Vector& dif = CWorld::GetOutdoorDiffuse();
                        float fr = (amb.x + dif.x * ndotl) * 255.0f + grp.mocvAdd[i].r;
                        float fg = (amb.y + dif.y * ndotl) * 255.0f + grp.mocvAdd[i].g;
                        float fb = (amb.z + dif.z * ndotl) * 255.0f + grp.mocvAdd[i].b;
                        grp.colors[i].r = static_cast<uint8_t>(fr > 255.0f ? 255.0f : fr);
                        grp.colors[i].g = static_cast<uint8_t>(fg > 255.0f ? 255.0f : fg);
                        grp.colors[i].b = static_cast<uint8_t>(fb > 255.0f ? 255.0f : fb);
                        grp.colors[i].a = 0xFF;
                    }
                }

                // This room's average interior colour (plus the MOHD ambient floor, matching the
                // geometry above), so a unit standing in it is lit like the walls around it. The
                // interiorAmbient field still holds the MOHD colour at this point in the load.
                if (grpMocvN > 0) {
                    float gr = static_cast<float>(grpMocvR / grpMocvN) / 255.0f + out.interiorAmbient.x;
                    float gg = static_cast<float>(grpMocvG / grpMocvN) / 255.0f + out.interiorAmbient.y;
                    float gb = static_cast<float>(grpMocvB / grpMocvN) / 255.0f + out.interiorAmbient.z;
                    grp.groupAmbient.x = gr > 1.0f ? 1.0f : gr;
                    grp.groupAmbient.y = gg > 1.0f ? 1.0f : gg;
                    grp.groupAmbient.z = gb > 1.0f ? 1.0f : gb;
                }

                // World-space bounding box of the group, for view-frustum culling. Computed from
                // the world positions BEFORE they are rebased below, so bounds, culling, sorting
                // and the liquid queries all keep working in world space.
                if (movtCount) {
                    grp.boundsMin = grp.positions[0];
                    grp.boundsMax = grp.positions[0];

                    for (uint32_t v = 1; v < movtCount; v++) {
                        const C3Vector& p = grp.positions[v];
                        grp.boundsMin.x = p.x < grp.boundsMin.x ? p.x : grp.boundsMin.x;
                        grp.boundsMin.y = p.y < grp.boundsMin.y ? p.y : grp.boundsMin.y;
                        grp.boundsMin.z = p.z < grp.boundsMin.z ? p.z : grp.boundsMin.z;
                        grp.boundsMax.x = p.x > grp.boundsMax.x ? p.x : grp.boundsMax.x;
                        grp.boundsMax.y = p.y > grp.boundsMax.y ? p.y : grp.boundsMax.y;
                        grp.boundsMax.z = p.z > grp.boundsMax.z ? p.z : grp.boundsMax.z;
                    }
                }

                // Rebase the geometry onto the instance origin now that the bounds are taken.
                for (uint32_t v = 0; v < movtCount; v++) {
                    grp.positions[v].x -= out.origin.x;
                    grp.positions[v].y -= out.origin.y;
                    grp.positions[v].z -= out.origin.z;
                }

                for (uint32_t j = 0; j < moviCount; j++) {
                    grp.indices[j] = movi[j];
                }

                // The reference-form group data the CMapObjGroup queries walk. Faces are MOVI
                // triples; MOPY has one record per face, MOBR indexes faces, MOBN indexes MOBR.
                {
                    CMapObjGroup& og = grp.objGroup;
                    uint32_t faceCount = moviCount / 3;

                    og.m_flags = mogpFlags;
                    og.m_indices = grp.indices;
                    og.m_vertexCount = movtCount;
                    og.m_faceCount = faceCount;
                    og.m_mapObj = &out.mapObj;

                    // The queries need the vertices in the model's OWN space, untouched by the
                    // placement, because MOBN's split planes are axis-aligned in that space and
                    // came straight out of the file. grp.positions is no good for this: it has
                    // already been yawed into the instance's orientation, so walking the tree with
                    // it navigates in the wrong frame and descends into the wrong subtrees. The
                    // triangle tests would still be self-consistent, which is what makes the bug
                    // quiet -- the query simply finds the wrong faces, or none.
                    //
                    // So keep a second copy in file space, and note the queries are handed a probe
                    // transformed the same way (see TerrainWmoFloorLightAt).
                    grp.queryVerts = static_cast<C3Vector*>(SMemAlloc(movtCount * sizeof(C3Vector), __FILE__, __LINE__, 0));

                    for (uint32_t v = 0; v < movtCount; v++) {
                        grp.queryVerts[v] = { movt[v * 3 + 0], movt[v * 3 + 1], movt[v * 3 + 2] };
                    }

                    og.m_vertices = grp.queryVerts;

                    // ...and the bounds in that same file space.
                    og.m_bounds.b = grp.queryVerts[0];
                    og.m_bounds.t = grp.queryVerts[0];

                    for (uint32_t v = 1; v < movtCount; v++) {
                        const C3Vector& q = grp.queryVerts[v];
                        og.m_bounds.b.x = q.x < og.m_bounds.b.x ? q.x : og.m_bounds.b.x;
                        og.m_bounds.b.y = q.y < og.m_bounds.b.y ? q.y : og.m_bounds.b.y;
                        og.m_bounds.b.z = q.z < og.m_bounds.b.z ? q.z : og.m_bounds.b.z;
                        og.m_bounds.t.x = q.x > og.m_bounds.t.x ? q.x : og.m_bounds.t.x;
                        og.m_bounds.t.y = q.y > og.m_bounds.t.y ? q.y : og.m_bounds.t.y;
                        og.m_bounds.t.z = q.z > og.m_bounds.t.z ? q.z : og.m_bounds.t.z;
                    }

                    if (mopy && mopyCount >= faceCount && faceCount) {
                        og.m_polys = static_cast<SMOPoly*>(SMemAlloc(faceCount * sizeof(SMOPoly), __FILE__, __LINE__, 0));
                        memcpy(og.m_polys, mopy, faceCount * sizeof(SMOPoly));
                    }

                    if (og.m_polys && mobn && mobnCount && mobr && mobrCount) {
                        og.m_bspNodes = static_cast<CAaBspNode*>(SMemAlloc(mobnCount * sizeof(CAaBspNode), __FILE__, __LINE__, 0));
                        memcpy(og.m_bspNodes, mobn, mobnCount * sizeof(CAaBspNode));
                        og.m_bspNodeCount = mobnCount;
                        og.m_bspFaceRefs = static_cast<uint16_t*>(SMemAlloc(mobrCount * sizeof(uint16_t), __FILE__, __LINE__, 0));
                        memcpy(og.m_bspFaceRefs, mobr, mobrCount * sizeof(uint16_t));
                        og.m_bspFaceRefCount = mobrCount;
                    }

                    if (mocv && mocvCount >= movtCount) {
                        grp.mocv = static_cast<CImVector*>(SMemAlloc(movtCount * sizeof(CImVector), __FILE__, __LINE__, 0));
                        memcpy(grp.mocv, mocv, movtCount * sizeof(CImVector));
                        og.m_colors = grp.mocv;
                    }
                }

                if (mobaCount) {
                    grp.batches = static_cast<WmoBatch*>(SMemAlloc(mobaCount * sizeof(WmoBatch), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));

                    for (uint32_t bch = 0; bch < mobaCount; bch++) {
                        const uint8_t* e = moba + bch * 24;
                        uint32_t startIndex = *reinterpret_cast<const uint32_t*>(e + 12);
                        uint16_t count = *reinterpret_cast<const uint16_t*>(e + 16);
                        uint8_t materialId = e[23];

                        WmoBatch& b = grp.batches[grp.batchCount++];
                        b.indexStart = startIndex;
                        b.indexCount = count;
                        b.texture = (materialId < out.textureCount) ? out.textures[materialId] : nullptr;
                        b.blend = (momt && materialId < nMaterials) ? *reinterpret_cast<const uint32_t*>(momt + materialId * 64 + 0x08) : 0;
                        uint32_t matFlags = (momt && materialId < nMaterials) ? *reinterpret_cast<const uint32_t*>(momt + materialId * 64 + 0x00) : 0;
                        b.twoSided = (matFlags & 0x4) != 0;
                        b.unlit = (matFlags & 0x1) != 0;
                        b.unfogged = (matFlags & 0x2) != 0;
                    }
                }

                if (mliq && mliqSize >= 30) {
                    LoadWmoLiquid(out, out.groupCount, nGroups, mliq, mliqSize, mogpGroupLiquid, mohdFlags, worldPos, cs, sn);
                }

                out.groupCount++;
            }
        }

        SMemFree(gdata, __FILE__, __LINE__, 0);
    }

    // Whole-instance bounding box = union of the group boxes, for hierarchical frustum culling.
    for (uint32_t g = 0; g < out.groupCount; g++) {
        const WmoGroup& grp = out.groups[g];

        if (!grp.vertexCount) {
            continue;
        }

        if (!out.hasBounds) {
            out.bboxMin = grp.boundsMin;
            out.bboxMax = grp.boundsMax;
            out.hasBounds = true;
        } else {
            out.bboxMin.x = grp.boundsMin.x < out.bboxMin.x ? grp.boundsMin.x : out.bboxMin.x;
            out.bboxMin.y = grp.boundsMin.y < out.bboxMin.y ? grp.boundsMin.y : out.bboxMin.y;
            out.bboxMin.z = grp.boundsMin.z < out.bboxMin.z ? grp.boundsMin.z : out.bboxMin.z;
            out.bboxMax.x = grp.boundsMax.x > out.bboxMax.x ? grp.boundsMax.x : out.bboxMax.x;
            out.bboxMax.y = grp.boundsMax.y > out.bboxMax.y ? grp.boundsMax.y : out.bboxMax.y;
            out.bboxMax.z = grp.boundsMax.z > out.bboxMax.z ? grp.boundsMax.z : out.bboxMax.z;
        }
    }

    // Constant interior ambient from the average interior MOCV (0.35 default if the WMO has none)
    if (mocvSamples > 0) {
        out.interiorAmbient.x = static_cast<float>(mocvSumR / mocvSamples) / 255.0f;
        out.interiorAmbient.y = static_cast<float>(mocvSumG / mocvSamples) / 255.0f;
        out.interiorAmbient.z = static_cast<float>(mocvSumB / mocvSamples) / 255.0f;
    }

    // Interior doodads (MODD): M2 props placed in WMO-local space. Transform each position through
    // the same proper rotation the geometry uses, and spawn the model into the world scene.
    CM2Scene* scene = CWorld::GetM2Scene();

    if (scene && modn && modd && moddCount) {
        out.doodads = static_cast<CM2Model**>(SMemAlloc(moddCount * sizeof(CM2Model*), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
        out.doodadScale = static_cast<float*>(SMemAlloc(moddCount * sizeof(float), __FILE__, __LINE__, 0));
        out.doodadAmbient = static_cast<C3Vector*>(SMemAlloc(moddCount * sizeof(C3Vector), __FILE__, __LINE__, 0));
        out.doodadCount = 0;

        for (uint32_t rr = 0; rr < rangeCount; rr++) {
        uint32_t rStart = ranges[rr].start;
        uint32_t rEnd = ranges[rr].end > moddCount ? moddCount : ranges[rr].end;

        for (uint32_t i = rStart; i < rEnd; i++) {
            const uint8_t* e = modd + i * 40;
            uint32_t nameOfs = *reinterpret_cast<const uint32_t*>(e + 0) & 0xFFFFFF;
            float dx = *reinterpret_cast<const float*>(e + 4);
            float dy = *reinterpret_cast<const float*>(e + 8);
            float dz = *reinterpret_cast<const float*>(e + 12);
            float qx = *reinterpret_cast<const float*>(e + 16);
            float qy = *reinterpret_cast<const float*>(e + 20);
            float qz = *reinterpret_cast<const float*>(e + 24);
            float qw = *reinterpret_cast<const float*>(e + 28);
            float dscale = *reinterpret_cast<const float*>(e + 32);

            // MODD colour (BGRA at +36): the doodad's baked lighting colour. The reference lights each
            // interior prop by its own colour, so a doodad in a blue-lit hall reads blue and one by a
            // purple crystal reads purple, instead of every prop sharing one average tint.
            C3Vector doodadColor = { e[38] / 255.0f, e[37] / 255.0f, e[36] / 255.0f };

            const char* modelPath = modn + nameOfs;

            C3Vector wp = {
                worldPos.x + (dx * cs - dy * sn),
                worldPos.y + (dx * sn + dy * cs),
                worldPos.z + dz
            };

            CM2Model* model = scene->CreateModel(modelPath, 0);

            if (!model) {
                continue;
            }

            // Every WMO doodad is lit by the building's own baked lighting -- its MODD colour -- not
            // the cycling outdoor sun, whether it sits in an interior hall or on the open deck. This
            // is the reference's behaviour (WMO props take the WMO light, so they can read differently
            // from the day/night world around them). An unset MODD colour (all-zero) falls back to the
            // WMO's average interior ambient.
            bool hasColor = (e[36] | e[37] | e[38]) != 0;
            out.doodadAmbient[out.doodadCount] = hasColor ? doodadColor : out.interiorAmbient;
            model->SetLightingCallback(&WmoDoodadLightingCallback, &out.doodadAmbient[out.doodadCount]);

            // Apply the doodad's full orientation, not just its yaw: the MODD rotation is a proper
            // Z-up world-space quaternion (its yaw extracts cleanly as 2*atan2(qz,qw), which is the
            // textbook Z-up yaw, so the whole quaternion is a consistent rotation). Build the world
            // matrix as WMO-yaw x doodad-quaternion x scale so leaning/tilted props (weapons on racks,
            // debris) sit the way the reference places them instead of standing upright.
            // These ops pre-multiply (M = Op x M), so applying the quaternion first then the WMO yaw
            // builds Rz(ry) x Rq -- yaw outermost, the doodad's own rotation inside -- then scale, the
            // same shape SetWorldTransform makes for the yaw-only case.
            float ds = dscale > 0.0f ? dscale : 1.0f;
            model->matrixB4.Identity();
            model->matrixB4.Rotate(C4Quaternion(qx, qy, qz, qw));
            model->matrixB4.RotateAroundZ(ry);
            model->matrixB4.Scale(ds);
            model->matrixB4.d0 = wp.x;
            model->matrixB4.d1 = wp.y;
            model->matrixB4.d2 = wp.z;
            model->m_flag8000 = 1;
            model->SetBoneSequence(-1, 0, -1, 0, 1.0f, 0, 1);
            model->SetAnimating(1);
            model->SetVisible(1);
            model->m_flag10000 = 1;

            out.doodadScale[out.doodadCount] = dscale > 0.0f ? dscale : 1.0f;
            out.doodads[out.doodadCount] = model;
            out.doodadCount++;
        }
        }
    }

    SMemFree(rootData, __FILE__, __LINE__, 0);
}

// Claim a placement uniqueId for this tile: returns true (and registers it) if the object should be
// placed here, false if another still-loaded tile already owns it. The 0xFFFFFFFF sentinel (no id)
// is always placed and never registered.
bool ClaimUnique(TerrainTile& tile, uint32_t uniqueId) {
    if (uniqueId == 0xFFFFFFFF) {
        return true;
    }

    if (s_loadedUnique.find(uniqueId) != s_loadedUnique.end()) {
        return false;
    }

    s_loadedUnique.insert(uniqueId);

    if (tile.ownedUnique) {
        tile.ownedUnique[tile.ownedUniqueCount++] = uniqueId;
    }

    return true;
}

void LoadTile(TerrainTile& tile, int32_t tileX, int32_t tileY) {
    tile.x = tileX;
    tile.y = tileY;
    tile.loaded = true;
    tile.textureCount = 0;
    tile.doodads = nullptr;
    tile.doodadCount = 0;
    tile.wmos = nullptr;
    tile.wmoCount = 0;

    for (auto& chunk : tile.chunks) {
        chunk.valid = false;
        chunk.nLayers = 0;
        chunk.alphaTexture = nullptr;
    }

    for (auto& t : tile.textures) {
        t = nullptr;
    }

    char path[256];
    SStrPrintf(path, sizeof(path), "World\\Maps\\%s\\%s_%d_%d.adt", s_mapName, s_mapName, tileX, tileY);

    void* data = nullptr;
    size_t size = 0;

    if (!SFile::Load(nullptr, path, &data, &size, 0, 0, nullptr) || !data) {
        return;
    }

    auto bytes = static_cast<const uint8_t*>(data);

    static const char* textureNames[256];
    uint32_t textureCount = 0;
    const uint8_t* mcin = nullptr;
    const char* mmdx = nullptr;
    const uint32_t* mmid = nullptr;
    uint32_t mmidCount = 0;
    const uint8_t* mddf = nullptr;
    uint32_t mddfCount = 0;
    const char* mwmo = nullptr;
    const uint32_t* mwid = nullptr;
    uint32_t mwidCount = 0;
    const uint8_t* modf = nullptr;
    uint32_t modfCount = 0;

    const uint8_t* mh2o = nullptr; // 3.3.5 liquid chunk (per-chunk headers + layer infos)
    uint32_t mh2oSize = 0;

    uint32_t offset = 0;

    while (offset + 8 <= size) {
        uint32_t chunkSize;
        uint32_t tag = ReadChunkTag(bytes, offset, chunkSize);
        const uint8_t* body = bytes + offset + 8;

        if (tag == FourCC("MTEX")) {
            const char* p = reinterpret_cast<const char*>(body);
            const char* end = p + chunkSize;

            while (p < end && textureCount < 256) {
                textureNames[textureCount++] = p;
                p += SStrLen(p) + 1;
            }
        } else if (tag == FourCC("MCIN")) {
            mcin = body;
        } else if (tag == FourCC("MMDX")) {
            mmdx = reinterpret_cast<const char*>(body);
        } else if (tag == FourCC("MMID")) {
            mmid = reinterpret_cast<const uint32_t*>(body);
            mmidCount = chunkSize / 4;
        } else if (tag == FourCC("MDDF")) {
            mddf = body;
            mddfCount = chunkSize / 36;
        } else if (tag == FourCC("MWMO")) {
            mwmo = reinterpret_cast<const char*>(body);
        } else if (tag == FourCC("MWID")) {
            mwid = reinterpret_cast<const uint32_t*>(body);
            mwidCount = chunkSize / 4;
        } else if (tag == FourCC("MODF")) {
            modf = body;
            modfCount = chunkSize / 64;
        } else if (tag == FourCC("MH2O")) {
            mh2o = body;
            mh2oSize = chunkSize;
        }

        offset += 8 + chunkSize;
    }

    tile.textureCount = textureCount;

    for (uint32_t i = 0; i < textureCount; i++) {
        CStatus status;
        // Trilinear + mipmaps, like the reference's ground textures, to avoid distance shimmer
        tile.textures[i] = TextureCreate(textureNames[i], CGxTexFlags(GxTex_LinearMipLinear, 1, 1, 0, 0, 0, 1), &status, 0);
    }

    if (mcin) {
        for (int32_t i = 0; i < 256; i++) {
            uint32_t mcnkOffset = *reinterpret_cast<const uint32_t*>(mcin + i * 16);

            if (mcnkOffset && mcnkOffset + 8 <= size) {
                uint32_t mcnkSize = *reinterpret_cast<const uint32_t*>(bytes + mcnkOffset + 4);
                ParseChunk(tile.chunks[i], bytes + mcnkOffset + 8, mcnkSize);

                if (mh2o) {
                    ParseLiquid(tile.chunks[i], i, mh2o, mh2oSize);
                }

                // Tiles without MH2O (or chunks it does not cover) still carry the pre-WotLK MCLQ
                if (!tile.chunks[i].liquidCount) {
                    ParseLegacyLiquid(tile.chunks[i], bytes + mcnkOffset + 8, mcnkSize);
                }

                BuildDetailDoodads(tile.chunks[i], tileX, tileY, i);
            }
        }
    }

    // Place the tile's M2 doodads (MDDF) into the world scene, exactly as the reference client
    // does: resolve the model path through MMID -> MMDX, convert the corner-relative position to
    // world coordinates, and apply the stored yaw and scale.
    CM2Scene* scene = CWorld::GetM2Scene();

    // One slot per placement in this tile; a claimed id is recorded here and released on unload.
    uint32_t ownedCap = mddfCount + modfCount;

    if (ownedCap) {
        tile.ownedUnique = static_cast<uint32_t*>(SMemAlloc(ownedCap * sizeof(uint32_t), __FILE__, __LINE__, 0));
        tile.ownedUniqueCount = 0;
    }

    if (scene && mmdx && mmid && mddf && mddfCount) {
        tile.doodads = static_cast<CM2Model**>(SMemAlloc(mddfCount * sizeof(CM2Model*), __FILE__, __LINE__, 0));
        tile.doodadScale = static_cast<float*>(SMemAlloc(mddfCount * sizeof(float), __FILE__, __LINE__, 0));
        tile.doodadCount = 0;

        for (uint32_t i = 0; i < mddfCount; i++) {
            const uint8_t* e = mddf + i * 36;
            uint32_t nameId = *reinterpret_cast<const uint32_t*>(e + 0);

            if (nameId >= mmidCount) {
                continue;
            }

            // Skip a doodad already placed by an overlapping neighbour tile (deduped by uniqueId)
            if (!ClaimUnique(tile, *reinterpret_cast<const uint32_t*>(e + 4))) {
                continue;
            }

            const char* modelPath = mmdx + mmid[nameId];

            float px = *reinterpret_cast<const float*>(e + 8);
            float py = *reinterpret_cast<const float*>(e + 12);
            float pz = *reinterpret_cast<const float*>(e + 16);
            float ry = *reinterpret_cast<const float*>(e + 24); // rotation about the vertical axis
            uint16_t scaleRaw = *reinterpret_cast<const uint16_t*>(e + 32);

            C3Vector worldPos = { MAP_CORNER - pz, MAP_CORNER - px, py };
            // NOTE: the M2 doodad path below uses the same negated-axis position conversion as
            // the WMO path, so it probably needs the same 180 degree yaw correction. Left alone
            // deliberately: the correction above was measured against WMO bounding boxes, and there
            // is no equivalent measurement for doodads yet. Fix it when it can be checked, not
            // because it looks similar.
            float yaw = ry * DEG2RAD;
            float scale = scaleRaw / 1024.0f;

            CM2Model* model = scene->CreateModel(modelPath, 0);

            if (!model) {
                continue;
            }

            model->SetLightingCallback(&CWorld::LightingCallback, nullptr);
            model->SetWorldTransform(worldPos, yaw, scale);
            model->SetBoneSequence(-1, 0, -1, 0, 1.0f, 0, 1);
            model->SetAnimating(1);
            model->SetVisible(1);
            model->m_flag10000 = 1;

            tile.doodadScale[tile.doodadCount] = scale;
            tile.doodads[tile.doodadCount] = model;
            tile.doodadCount++;
        }
    }

    // Place the tile's WMO buildings (MODF), resolving the path through MWID -> MWMO and using the
    // world placement transform derived from the MODF bounding box.
    if (mwmo && mwid && modf && modfCount) {
        tile.wmos = static_cast<WmoInstance*>(SMemAlloc(modfCount * sizeof(WmoInstance), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
        tile.wmoCount = 0;

        for (uint32_t i = 0; i < modfCount; i++) {
            const uint8_t* e = modf + i * 64;
            uint32_t nameId = *reinterpret_cast<const uint32_t*>(e + 0);

            if (nameId >= mwidCount) {
                continue;
            }

            // Skip a WMO already placed by an overlapping neighbour tile (deduped by uniqueId)
            if (!ClaimUnique(tile, *reinterpret_cast<const uint32_t*>(e + 4))) {
                continue;
            }

            const char* rootPath = mwmo + mwid[nameId];

            float px = *reinterpret_cast<const float*>(e + 8);
            float py = *reinterpret_cast<const float*>(e + 12);
            float pz = *reinterpret_cast<const float*>(e + 16);
            float rx = *reinterpret_cast<const float*>(e + 20);
            float ry = *reinterpret_cast<const float*>(e + 24);
            float rz = *reinterpret_cast<const float*>(e + 28);
            uint16_t doodadSet = *reinterpret_cast<const uint16_t*>(e + 58);

            C3Vector worldPos = { MAP_CORNER - pz, MAP_CORNER - px, py };

            // MODF also stores the placed instance's world-space AABB (offsets 32..55) in the same
            // axis convention as the position. It is ground truth for where this WMO belongs, so
            // compare it against the box we build from the transformed vertices: a mismatch means
            // the vertex transform's axes are wrong, not the placement.
            C3Vector eMin, eMax;
            {
                float ax = *reinterpret_cast<const float*>(e + 32);
                float ay = *reinterpret_cast<const float*>(e + 36);
                float az = *reinterpret_cast<const float*>(e + 40);
                float bx = *reinterpret_cast<const float*>(e + 44);
                float by = *reinterpret_cast<const float*>(e + 48);
                float bz = *reinterpret_cast<const float*>(e + 52);

                C3Vector c0 = { MAP_CORNER - az, MAP_CORNER - ax, ay };
                C3Vector c1 = { MAP_CORNER - bz, MAP_CORNER - bx, by };

                eMin = { c0.x < c1.x ? c0.x : c1.x, c0.y < c1.y ? c0.y : c1.y, c0.z < c1.z ? c0.z : c1.z };
                eMax = { c0.x > c1.x ? c0.x : c1.x, c0.y > c1.y ? c0.y : c1.y, c0.z > c1.z ? c0.z : c1.z };
            }

            WmoInstance& inst = tile.wmos[tile.wmoCount];
            new (&inst) WmoInstance();
            // The placement POSITION is converted with two negated horizontal axes
            // (MAP_CORNER - pz, MAP_CORNER - px), which is itself a 180 degree rotation about the
            // vertical. The model's own yaw has to be turned by the same 180 degrees or the
            // geometry ends up rotated half a turn about its placement point.
            //
            // Measured, not guessed: for two unrelated buildings the offset between frozen's built
            // centre and the placement record's centre implies a rotation error of 173.5 and 180.0
            // degrees respectively, derived from each model's own centroid in its MOHD header.
            // Buildings whose geometry is centred on their placement point showed almost no offset,
            // which is why only 43 of 106 looked wrong.
            LoadWmoInstance(rootPath, worldPos, (ry + 180.0f) * DEG2RAD, doodadSet, inst);

            if (inst.hasBounds) {
                // Compare CENTRES and SIZES, not min corners. A corner delta conflates two very
                // different faults: geometry in the wrong place, and geometry that is merely
                // incomplete (a missing group shrinks the box and moves its corner). The centre
                // offset says "misplaced"; the size ratio says "incomplete".
                C3Vector bc = { (inst.bboxMin.x + inst.bboxMax.x) * 0.5f, (inst.bboxMin.y + inst.bboxMax.y) * 0.5f, (inst.bboxMin.z + inst.bboxMax.z) * 0.5f };
                C3Vector ec = { (eMin.x + eMax.x) * 0.5f, (eMin.y + eMax.y) * 0.5f, (eMin.z + eMax.z) * 0.5f };

                float cdx = bc.x < ec.x ? ec.x - bc.x : bc.x - ec.x;
                float cdy = bc.y < ec.y ? ec.y - bc.y : bc.y - ec.y;
                float cdz = bc.z < ec.z ? ec.z - bc.z : bc.z - ec.z;

                float esx = eMax.x - eMin.x;
                float esy = eMax.y - eMin.y;
                float esz = eMax.z - eMin.z;
                float rx2 = esx > 0.01f ? (inst.bboxMax.x - inst.bboxMin.x) / esx : 1.0f;
                float ry2 = esy > 0.01f ? (inst.bboxMax.y - inst.bboxMin.y) / esy : 1.0f;
                float rz2 = esz > 0.01f ? (inst.bboxMax.z - inst.bboxMin.z) / esz : 1.0f;

                // MODF's extents are a PADDED bound, not the geometry's own box: for a large
                // building they can be 50 yards wider on a side and are not always centred on the
                // geometry. Judging placement by how far the two centres differ therefore flags
                // correct buildings, which is what produced 43 false failures before the yaw fix
                // and the last 6 after it.
                //
                // The honest test is containment: if every transformed vertex lies inside the box
                // the placement record declares, the instance is where the record says it is. A
                // small slack absorbs the enlargement a yaw rotation adds to an axis-aligned box.
                const float SLACK = 1.0f;
                bool inside =
                    inst.bboxMin.x >= eMin.x - SLACK && inst.bboxMax.x <= eMax.x + SLACK &&
                    inst.bboxMin.y >= eMin.y - SLACK && inst.bboxMax.y <= eMax.y + SLACK &&
                    inst.bboxMin.z >= eMin.z - SLACK && inst.bboxMax.z <= eMax.z + SLACK;

                float dx = inside ? 0.0f : cdx;
                float dy = inside ? 0.0f : cdy;
                float dz = inside ? 0.0f : cdz;

                fprintf(stderr, "WMO %s yaw %7.1f centre(%6.1f %6.1f %6.1f) sizeRatio(%.2f %.2f %.2f) %s\n",
                    dx > 5.0f || dy > 5.0f || dz > 5.0f ? "BAD " : "ok  ", ry,
                    cdx, cdy, cdz, rx2, ry2, rz2, rootPath);

                if (dx > 5.0f || dy > 5.0f || dz > 5.0f) {
                    fprintf(stderr,
                        "WMO placement mismatch %s groups %u/%u built min(%.1f %.1f %.1f) max(%.1f %.1f %.1f) "
                        "MODF min(%.1f %.1f %.1f) max(%.1f %.1f %.1f)\n",
                        rootPath, inst.groupCount, inst.groupsExpected,
                        inst.bboxMin.x, inst.bboxMin.y, inst.bboxMin.z,
                        inst.bboxMax.x, inst.bboxMax.y, inst.bboxMax.z,
                        eMin.x, eMin.y, eMin.z, eMax.x, eMax.y, eMax.z);
                }
            }

            if (inst.groupCount) {
                tile.wmoCount++;
            }
        }
    }

    SMemFree(data, __FILE__, __LINE__, 0);
}

void FreeTile(TerrainTile& tile) {
    // Release this tile's claimed placement ids so a neighbour can own them when it next loads
    if (tile.ownedUnique) {
        for (uint32_t i = 0; i < tile.ownedUniqueCount; i++) {
            s_loadedUnique.erase(tile.ownedUnique[i]);
        }

        SMemFree(tile.ownedUnique, __FILE__, __LINE__, 0);
        tile.ownedUnique = nullptr;
        tile.ownedUniqueCount = 0;
    }

    if (tile.doodads) {
        for (uint32_t i = 0; i < tile.doodadCount; i++) {
            if (tile.doodads[i]) {
                ParticleFxForgetModel(tile.doodads[i]);
                tile.doodads[i]->DetachFromScene();
                tile.doodads[i]->Release();
            }
        }

        SMemFree(tile.doodads, __FILE__, __LINE__, 0);
        tile.doodads = nullptr;
    }


    if (tile.doodadScale) {
        SMemFree(tile.doodadScale, __FILE__, __LINE__, 0);
        tile.doodadScale = nullptr;
    }

    tile.doodadCount = 0;

    for (auto& chunk : tile.chunks) {
        if (chunk.details) {
            SMemFree(chunk.details, __FILE__, __LINE__, 0);
            chunk.details = nullptr;
        }

        chunk.detailCount = 0;
        FreeDetailBatch(chunk);

        for (uint32_t l = 0; l < chunk.liquidCount; l++) {
            ChunkLiquid& liq = chunk.liquids[l];
            if (liq.verts) SMemFree(liq.verts, __FILE__, __LINE__, 0);
            if (liq.uvs) SMemFree(liq.uvs, __FILE__, __LINE__, 0);
            if (liq.colors) SMemFree(liq.colors, __FILE__, __LINE__, 0);
            if (liq.depthRamp) SMemFree(liq.depthRamp, __FILE__, __LINE__, 0);
            if (liq.indices) SMemFree(liq.indices, __FILE__, __LINE__, 0);
        }

        if (chunk.liquids) {
            SMemFree(chunk.liquids, __FILE__, __LINE__, 0);
            chunk.liquids = nullptr;
        }

        chunk.liquidCount = 0;
    }

    if (tile.wmos) {
        for (uint32_t i = 0; i < tile.wmoCount; i++) {
            WmoInstance& w = tile.wmos[i];

            for (uint32_t gi = 0; gi < w.groupCount; gi++) {
                WmoGroup& grp = w.groups[gi];

                if (grp.positions) SMemFree(grp.positions, __FILE__, __LINE__, 0);
                if (grp.colors) SMemFree(grp.colors, __FILE__, __LINE__, 0);
                if (grp.texcoords) SMemFree(grp.texcoords, __FILE__, __LINE__, 0);
                if (grp.indices) SMemFree(grp.indices, __FILE__, __LINE__, 0);
                if (grp.batches) SMemFree(grp.batches, __FILE__, __LINE__, 0);
                if (grp.ndotl) SMemFree(grp.ndotl, __FILE__, __LINE__, 0);
                if (grp.ao) SMemFree(grp.ao, __FILE__, __LINE__, 0);
                if (grp.mocvAdd) SMemFree(grp.mocvAdd, __FILE__, __LINE__, 0);
                if (grp.mocv) SMemFree(grp.mocv, __FILE__, __LINE__, 0);
                if (grp.queryVerts) SMemFree(grp.queryVerts, __FILE__, __LINE__, 0);
                grp.objGroup.FreeQueryData();
            }

            if (w.mapObj.m_materials) {
                SMemFree(w.mapObj.m_materials, __FILE__, __LINE__, 0);
                w.mapObj.m_materials = nullptr;
                w.mapObj.m_materialCount = 0;
            }

            for (uint32_t t = 0; t < w.textureCount; t++) {
                if (w.textures[t]) {
                    HandleClose(w.textures[t]);
                }
            }

            for (uint32_t di = 0; di < w.doodadCount; di++) {
                if (w.doodads[di]) {
                    ParticleFxForgetModel(w.doodads[di]);
                    w.doodads[di]->DetachFromScene();
                    w.doodads[di]->Release();
                }
            }

            if (w.doodads) SMemFree(w.doodads, __FILE__, __LINE__, 0);
            if (w.doodadScale) SMemFree(w.doodadScale, __FILE__, __LINE__, 0);
            if (w.doodadAmbient) SMemFree(w.doodadAmbient, __FILE__, __LINE__, 0);
            if (w.portalVerts) SMemFree(w.portalVerts, __FILE__, __LINE__, 0);
            if (w.portals) SMemFree(w.portals, __FILE__, __LINE__, 0);
            if (w.portalRefs) SMemFree(w.portalRefs, __FILE__, __LINE__, 0);

            for (uint32_t li = 0; li < w.liquidCount; li++) {
                ChunkLiquid& liq = w.liquids[li];
                if (liq.verts) SMemFree(liq.verts, __FILE__, __LINE__, 0);
                if (liq.uvs) SMemFree(liq.uvs, __FILE__, __LINE__, 0);
                if (liq.colors) SMemFree(liq.colors, __FILE__, __LINE__, 0);
            if (liq.depthRamp) SMemFree(liq.depthRamp, __FILE__, __LINE__, 0);
                if (liq.indices) SMemFree(liq.indices, __FILE__, __LINE__, 0);
            }

            if (w.liquids) SMemFree(w.liquids, __FILE__, __LINE__, 0);
            if (w.groups) SMemFree(w.groups, __FILE__, __LINE__, 0);
            if (w.textures) SMemFree(w.textures, __FILE__, __LINE__, 0);
        }

        SMemFree(tile.wmos, __FILE__, __LINE__, 0);
        tile.wmos = nullptr;
    }

    tile.wmoCount = 0;

    for (auto& chunk : tile.chunks) {
        if (chunk.alphaTexture) {
            HandleClose(chunk.alphaTexture);
            chunk.alphaTexture = nullptr;
        }

        chunk.valid = false;
        chunk.nLayers = 0;
    }

    for (uint32_t i = 0; i < tile.textureCount; i++) {
        if (tile.textures[i]) {
            HandleClose(tile.textures[i]);
            tile.textures[i] = nullptr;
        }
    }

    tile.textureCount = 0;
    tile.loaded = false;
    tile.x = -1;
    tile.y = -1;
}

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
        // program at all and every chunk fell through to RenderFallback, which draws the mesh
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

// Single-pass per-pixel blend (base + up to 3 overlay layers weighted by the combined alpha map)
void RenderShaded(const C44Matrix& viewProjT) {
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthWrite, 1);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, s_fogActive ? 1 : 0);

    GxRsSet(GxRs_VertexShader, s_terrainVS);
    GxRsSet(GxRs_PixelShader, s_terrainPS);

    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (auto& chunk : tile.chunks) {
            if (!chunk.valid || chunk.nLayers <= 0 || !chunk.alphaTexture) {
                continue;
            }

            if (!BoxVisible(chunk.boundsMin, chunk.boundsMax)) {
                continue;
            }

            // Bind the four diffuse layers; unused stages reuse the base (their alpha weight is 0)
            for (int32_t l = 0; l < MAX_LAYERS; l++) {
                int32_t texId = (l < chunk.nLayers) ? chunk.layerTex[l] : chunk.layerTex[0];
                HTEXTURE tex = (texId >= 0 && static_cast<uint32_t>(texId) < tile.textureCount) ? tile.textures[texId] : nullptr;

                if (!tex) {
                    tex = tile.textures[chunk.layerTex[0]];
                }

                GxRsSet(static_cast<EGxRenderState>(GxRs_Texture0 + l), tex ? TextureGetGxTex(tex, 0, nullptr) : nullptr);
            }

            GxRsSet(GxRs_Texture4, TextureGetGxTex(chunk.alphaTexture, 0, nullptr));

            C44Matrix chunkT = ChunkMatrixT(chunk.origin);
            GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&chunkT), 4);

            GxPrimLockVertexPtrs(
                145,
                chunk.localPos, sizeof(C3Vector),
                nullptr, 0,
                chunk.color, sizeof(CImVector),
                nullptr, 0,
                chunk.texcoord, sizeof(C2Vector),
                nullptr, 0
            );

            if (chunk.holes) {
                uint32_t hcount = BuildHoleIndices(chunk.holes, s_holeIndices);
                GxDrawLockedElements(GxPrim_Triangles, hcount, s_holeIndices);
            } else {
                GxDrawLockedElements(GxPrim_Triangles, 768, s_indices);
            }

            GxPrimUnlockVertexPtrs();
        }
    }
}

// Fallback for non-D3D backends: base opaque, then each overlay layer alpha-blended by the
// combined alpha map sampled per vertex, through the UI shaders.
uint8_t SampleCombined(const CImVector* map, float u, float v, int32_t layer) {
    int32_t ax = static_cast<int32_t>(u * 63.0f + 0.5f);
    int32_t ay = static_cast<int32_t>(v * 63.0f + 0.5f);
    ax = ax < 0 ? 0 : (ax > 63 ? 63 : ax);
    ay = ay < 0 ? 0 : (ay > 63 ? 63 : ay);
    const CImVector& c = map[ay * 64 + ax];

    // layer 0 reads the alpha channel, which carries the chunk's baked MCSH shadow map
    if (layer == 0) {
        return c.a;
    }

    return layer == 1 ? c.r : (layer == 2 ? c.g : c.b);
}

void RenderFallback(const C44Matrix& viewProjT) {
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, s_fogActive ? 1 : 0);

    GxRsSet(GxRs_VertexShader, s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, s_uiPixelShader);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&viewProjT), 4);

    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (auto& chunk : tile.chunks) {
            if (!chunk.valid || chunk.nLayers <= 0) {
                continue;
            }

            if (!BoxVisible(chunk.boundsMin, chunk.boundsMax)) {
                continue;
            }

            for (int32_t l = 0; l < chunk.nLayers; l++) {
                int32_t texId = chunk.layerTex[l];
                HTEXTURE tex = (texId >= 0 && static_cast<uint32_t>(texId) < tile.textureCount) ? tile.textures[texId] : nullptr;

                if (!tex) {
                    continue;
                }

                if (l == 0) {
                    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
                    GxRsSet(GxRs_DepthWrite, 1);
                } else {
                    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
                    GxRsSet(GxRs_DepthWrite, 0);
                }

                for (int32_t v = 0; v < 145; v++) {
                    // The vertex colour is lit WITHOUT the baked shadow (the shaded path applies it
                    // per pixel from the blend map's alpha). This path has no shader to do that, so
                    // apply it per vertex here: the colour's own alpha is the ambient ratio, i.e.
                    // what survives with the sun removed.
                    uint8_t lit = chunk.color[v].r;
                    uint8_t shadow = SampleCombined(chunk.alphaCombined, chunk.texcoord[v].x, chunk.texcoord[v].y, 0);
                    float ratio = chunk.color[v].a / 255.0f;
                    float f = ratio + (1.0f - ratio) * (shadow / 255.0f);
                    uint8_t s = static_cast<uint8_t>(lit * f);

                    uint8_t a = (l == 0) ? 0xFF : SampleCombined(chunk.alphaCombined, chunk.texcoord[v].x, chunk.texcoord[v].y, l);
                    s_colorScratch[v] = { s, s, s, a };
                }

                GxRsSet(GxRs_Texture0, TextureGetGxTex(tex, 0, nullptr));

                GxPrimLockVertexPtrs(
                    145,
                    chunk.position, sizeof(C3Vector),
                    nullptr, 0,
                    s_colorScratch, sizeof(CImVector),
                    nullptr, 0,
                    chunk.texcoord, sizeof(C2Vector),
                    nullptr, 0
                );

                if (chunk.holes) {
                    uint32_t hcount = BuildHoleIndices(chunk.holes, s_holeIndices);
                    GxDrawLockedElements(GxPrim_Triangles, hcount, s_holeIndices);
                } else {
                    GxDrawLockedElements(GxPrim_Triangles, 768, s_indices);
                }

                GxPrimUnlockVertexPtrs();
            }
        }
    }
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

bool BoxInPlanes(const C3Vector& mn, const C3Vector& mx, const PortalFrustum& f) {
    for (int32_t p = 0; p < f.count; p++) {
        const float* pl = f.planes[p];
        float px = pl[0] >= 0.0f ? mx.x : mn.x;
        float py = pl[1] >= 0.0f ? mx.y : mn.y;
        float pz = pl[2] >= 0.0f ? mx.z : mn.z;

        if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.0f) {
            return false;
        }
    }

    return true;
}

// A polygon is inside unless every vertex lies outside one plane
bool PolyInPlanes(const C3Vector* v, uint32_t n, const PortalFrustum& f) {
    for (int32_t p = 0; p < f.count; p++) {
        const float* pl = f.planes[p];
        bool allOut = true;

        for (uint32_t i = 0; i < n; i++) {
            if (pl[0] * v[i].x + pl[1] * v[i].y + pl[2] * v[i].z + pl[3] >= 0.0f) {
                allOut = false;
                break;
            }
        }

        if (allOut) {
            return false;
        }
    }

    return true;
}

// Narrow a frustum to what is seen through a portal polygon: keep the near and far planes and add
// one plane per polygon edge through the eye, oriented so the polygon's centroid is inside.
void NarrowFrustum(const PortalFrustum& in, const C3Vector* v, uint32_t n, const C3Vector& eye, PortalFrustum& out) {
    out.count = 0;

    // Slots 4 and 5 are the near and far planes only in the BASE frustum; once narrowed, those
    // slots hold portal edge planes, so carrying them forward by index dropped the near/far pair
    // at every depth past the first. Take them from the frustum this walk started with.
    for (int32_t p = 4; p < 6 && p < s_baseFrustum.count; p++) {
        for (int32_t k = 0; k < 4; k++) {
            out.planes[out.count][k] = s_baseFrustum.planes[p][k];
        }
        out.count++;
    }

    C3Vector c = { 0.0f, 0.0f, 0.0f };

    for (uint32_t i = 0; i < n; i++) {
        c.x += v[i].x; c.y += v[i].y; c.z += v[i].z;
    }

    c.x /= n; c.y /= n; c.z /= n;

    for (uint32_t i = 0; i < n && out.count < PORTAL_MAX_PLANES; i++) {
        const C3Vector& a = v[i];
        const C3Vector& b = v[(i + 1) % n];
        C3Vector ea = { a.x - eye.x, a.y - eye.y, a.z - eye.z };
        C3Vector eb = { b.x - eye.x, b.y - eye.y, b.z - eye.z };
        float nx = ea.y * eb.z - ea.z * eb.y;
        float ny = ea.z * eb.x - ea.x * eb.z;
        float nz = ea.x * eb.y - ea.y * eb.x;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);

        if (len < 1e-6f) {
            continue;
        }

        nx /= len; ny /= len; nz /= len;
        float d = -(nx * eye.x + ny * eye.y + nz * eye.z);

        if (nx * c.x + ny * c.y + nz * c.z + d < 0.0f) {
            nx = -nx; ny = -ny; nz = -nz; d = -d;
        }

        float* pl = out.planes[out.count++];
        pl[0] = nx; pl[1] = ny; pl[2] = nz; pl[3] = d;
    }
}

void WalkPortals(WmoInstance& w, uint32_t g, const PortalFrustum& f, const C3Vector& eye, int32_t depth) {
    WmoGroup& grp = w.groups[g];
    grp.visFrame = s_visFrame;

    // Re-entering a group is only worth it from a shallower path: without this a WMO with many
    // interconnected rooms is re-walked exponentially, since every portal re-opens its neighbours.
    if (grp.visDepth <= depth) {
        return;
    }

    grp.visDepth = depth;

    if (depth >= PORTAL_MAX_DEPTH) {
        return;
    }

    for (uint32_t r = 0; r < grp.portalCount; r++) {
        uint32_t ri = grp.portalStart + r;

        if (ri >= w.portalRefCount) {
            break;
        }

        const WmoPortalRef& ref = w.portalRefs[ri];

        if (ref.portal >= w.portalCount || ref.group >= w.groupCount || ref.group == g) {
            continue;
        }

        const WmoPortal& pt = w.portals[ref.portal];

        if (!pt.vertexCount) {
            continue;
        }

        // Only look through a portal from the referencing group's own side of it
        float side = pt.normal.x * eye.x + pt.normal.y * eye.y + pt.normal.z * eye.z + pt.dist;

        if (side * static_cast<float>(ref.side) <= 0.0f) {
            continue;
        }

        const C3Vector* pv = w.portalVerts + pt.startVertex;

        if (!PolyInPlanes(pv, pt.vertexCount, f)) {
            continue;
        }

        PortalFrustum narrowed;
        NarrowFrustum(f, pv, pt.vertexCount, eye, narrowed);

        WmoGroup& target = w.groups[ref.group];

        if (!BoxInPlanes(target.boundsMin, target.boundsMax, narrowed)) {
            continue;
        }

        // A group already reached this frame is still walked again: a different portal can open
        // a different slice of its neighbours. The depth cap bounds the recursion.
        WalkPortals(w, ref.group, narrowed, eye, depth + 1);
    }
}

bool BoxContains(const C3Vector& mn, const C3Vector& mx, const C3Vector& p, float margin) {
    return p.x >= mn.x - margin && p.x <= mx.x + margin
        && p.y >= mn.y - margin && p.y <= mx.y + margin
        && p.z >= mn.z - margin && p.z <= mx.z + margin;
}

// Stamp the groups visible this frame. Inside a building the walk starts at the camera's group and
// only rooms reachable through in-view portals are drawn; outside, the exterior groups pass the
// frustum test and interiors are reached through their exterior portals. WMOs without portal data
// and interior groups with no portal links keep the plain frustum test so nothing vanishes.
void WmoUpdateVisibility(const C3Vector& eye) {
    s_visFrame++;

    PortalFrustum base;
    base.count = 6;

    for (int32_t p = 0; p < 6; p++) {
        for (int32_t k = 0; k < 4; k++) {
            base.planes[p][k] = s_frustum[p][k];
        }
    }

    s_baseFrustum = base;

    for (auto& tile : s_tiles) {
        if (!tile.loaded || !tile.wmos) {
            continue;
        }

        for (uint32_t wi = 0; wi < tile.wmoCount; wi++) {
            WmoInstance& w = tile.wmos[wi];

            if (w.hasBounds && !BoxVisible(w.bboxMin, w.bboxMax)) {
                continue;
            }

            for (uint32_t g = 0; g < w.groupCount; g++) {
                w.groups[g].visDepth = 0x7FFFFFFF;
            }

            if (!w.portalCount || !w.portalRefCount) {
                for (uint32_t g = 0; g < w.groupCount; g++) {
                    WmoGroup& grp = w.groups[g];

                    if (grp.vertexCount && BoxVisible(grp.boundsMin, grp.boundsMax)) {
                        grp.visFrame = s_visFrame;
                    }
                }

                continue;
            }

            // The camera's group: the smallest interior group box that contains the eye. (The
            // reference resolves this exactly through the group BSP; the box is a close stand-in.)
            int32_t camGroup = -1;
            float bestVolume = 0.0f;

            for (uint32_t g = 0; g < w.groupCount; g++) {
                const WmoGroup& grp = w.groups[g];

                if (!grp.vertexCount || !grp.interior || !grp.portalCount) {
                    continue;
                }

                if (!BoxContains(grp.boundsMin, grp.boundsMax, eye, 0.5f)) {
                    continue;
                }

                float vol = (grp.boundsMax.x - grp.boundsMin.x) * (grp.boundsMax.y - grp.boundsMin.y) * (grp.boundsMax.z - grp.boundsMin.z);

                if (camGroup < 0 || vol < bestVolume) {
                    camGroup = static_cast<int32_t>(g);
                    bestVolume = vol;
                }
            }

            if (camGroup >= 0) {
                WalkPortals(w, static_cast<uint32_t>(camGroup), base, eye, 0);
            }

            for (uint32_t g = 0; g < w.groupCount; g++) {
                WmoGroup& grp = w.groups[g];

                if (!grp.vertexCount) {
                    continue;
                }

                bool open = !grp.interior || !grp.portalCount; // exterior, or an unlinked room

                if (!open || !BoxVisible(grp.boundsMin, grp.boundsMax)) {
                    continue;
                }

                if (camGroup < 0) {
                    // Outside: every in-view exterior group draws and opens its portals inward
                    WalkPortals(w, g, base, eye, 0);
                } else {
                    // Inside: exterior shells stay visible through windows and open walls
                    grp.visFrame = s_visFrame;
                }
            }
        }
    }
}

// WMO buildings render with the UI shader (one texture modulated by the per-vertex sun light),
// which is exactly what a WMO material batch needs.
void RenderWmos(const C44Matrix& viewProjT) {
    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, s_fogActive ? 1 : 0);

    // Draw through the terrain vertex program where it exists: its inputs are exactly a WMO
    // group's streams (position, colour, texcoord), and sharing one program is what lets the blob
    // shadow pass re-draw this geometry with a bit-identical transform and a depth-EQUAL test.
    bool wmoShaded = s_useTerrainShader && s_terrainVS && s_detailPS && s_detailPS->Valid();

    GxRsSet(GxRs_VertexShader, wmoShaded ? s_terrainVS : s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, wmoShaded ? s_detailPS : s_uiPixelShader);

    // A full-bright white colour stream for F_UNLIT batches (a group's vertex colours hold baked
    // lighting; unlit materials must ignore it and show texture x white, like the reference).
    static CImVector* s_wmoWhite = nullptr;

    if (!s_wmoWhite) {
        s_wmoWhite = static_cast<CImVector*>(SMemAlloc(65536 * sizeof(CImVector), __FILE__, __LINE__, 0));

        for (uint32_t i = 0; i < 65536; i++) {
            s_wmoWhite[i].b = 0xFF;
            s_wmoWhite[i].g = 0xFF;
            s_wmoWhite[i].r = 0xFF;
            s_wmoWhite[i].a = 0xFF;
        }
    }

    // Opaque and alpha-tested batches first, with depth writes on. Alpha-blended batches draw after,
    // depth-sorted (see below), so they layer over the solid geometry like the reference.
    {
        const int32_t pass = 0;
        GxRsSet(GxRs_DepthWrite, 1);

        for (auto& tile : s_tiles) {
            if (!tile.loaded || !tile.wmos) {
                continue;
            }

            for (uint32_t i = 0; i < tile.wmoCount; i++) {
                WmoInstance& w = tile.wmos[i];

                // Skip the whole building when its bounding box is off-screen (hierarchical cull).
                if (w.hasBounds && !BoxVisible(w.bboxMin, w.bboxMax)) {
                    continue;
                }

                // Vertices are instance-local, so the matrix carries the origin (see
                // WmoInstance::origin).
                C44Matrix instanceT = ChunkMatrixT(w.origin);
                GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&instanceT), 4);

                for (uint32_t gi = 0; gi < w.groupCount; gi++) {
                    WmoGroup& grp = w.groups[gi];

                    if (!grp.vertexCount || !grp.batchCount) {
                        continue;
                    }

                    if (grp.visFrame != s_visFrame) {
                        continue;
                    }

                    const CImVector* lockedColors = nullptr;

                    for (uint32_t b = 0; b < grp.batchCount; b++) {
                        const WmoBatch& batch = grp.batches[b];

                        if (!batch.indexCount) {
                            continue;
                        }

                        bool blended = batch.blend >= 2;

                        if (blended != (pass == 1)) {
                            continue;
                        }

                        // Unlit batches source a white colour stream so lighting drops out; lit
                        // batches use the group's baked per-vertex colours. Re-lock when it changes.
                        const CImVector* wantColors = batch.unlit ? s_wmoWhite : grp.colors;

                        if (lockedColors != wantColors) {
                            if (lockedColors) {
                                GxPrimUnlockVertexPtrs();
                            }

                            GxPrimLockVertexPtrs(
                                grp.vertexCount,
                                grp.positions, sizeof(C3Vector),
                                nullptr, 0,
                                wantColors, sizeof(CImVector),
                                nullptr, 0,
                                grp.texcoords, sizeof(C2Vector),
                                nullptr, 0
                            );
                            lockedColors = wantColors;
                        }

                        // Opaque, alpha-tested (blend 1), or alpha-blended (blend >= 2)
                        EGxBlend mode = batch.blend == 0 ? GxBlend_Opaque : (batch.blend == 1 ? GxBlend_AlphaKey : GxBlend_Alpha);
                        GxRsSet(GxRs_BlendingMode, mode);
                        // Alpha-key materials discard nearly-transparent texels via the alpha test
                        // (ref 224/255, matching the M2 path's 0.878); other modes disable it. Without
                        // this the cutout texels render opaque instead of being punched through.
                        GxRsSet(GxRs_AlphaRef, batch.blend == 1 ? 224 : 0);
                        // Backface culling (engine default mode 1) unless the material is two-sided,
                        // matching how the reference renders M2 and WMO materials
                        GxRsSet(GxRs_Culling, batch.twoSided ? 0 : 1);
                        // F_UNFOGGED materials are excluded from distance fog, like the reference
                        GxRsSet(GxRs_Fog, (s_fogActive && !batch.unfogged) ? 1 : 0);
                        GxRsSet(GxRs_Texture0, batch.texture ? TextureGetGxTex(batch.texture, 0, nullptr) : nullptr);
                        GxDrawLockedElements(GxPrim_Triangles, batch.indexCount, grp.indices + batch.indexStart);
                    }

                    if (lockedColors) {
                        GxPrimUnlockVertexPtrs();
                    }
                }
            }
        }
    }

    // Alpha-blended batches, drawn back-to-front so overlapping transparent surfaces layer in the
    // right order (the reference depth-sorts transparency). Depth writes stay off so they never
    // occlude one another. Each batch is keyed on its group's world-space centre distance.
    struct BlendedRef { WmoGroup* grp; uint32_t batch; float dist; C3Vector origin; };
    static std::vector<BlendedRef> s_blended;
    s_blended.clear();

    for (auto& tile : s_tiles) {
        if (!tile.loaded || !tile.wmos) {
            continue;
        }

        for (uint32_t i = 0; i < tile.wmoCount; i++) {
            WmoInstance& w = tile.wmos[i];

            if (w.hasBounds && !BoxVisible(w.bboxMin, w.bboxMax)) {
                continue;
            }

            for (uint32_t gi = 0; gi < w.groupCount; gi++) {
                WmoGroup& grp = w.groups[gi];

                if (!grp.vertexCount || !grp.batchCount || grp.visFrame != s_visFrame) {
                    continue;
                }

                float cx = (grp.boundsMin.x + grp.boundsMax.x) * 0.5f - s_cameraPos.x;
                float cy = (grp.boundsMin.y + grp.boundsMax.y) * 0.5f - s_cameraPos.y;
                float cz = (grp.boundsMin.z + grp.boundsMax.z) * 0.5f - s_cameraPos.z;
                float d = cx * cx + cy * cy + cz * cz;

                for (uint32_t b = 0; b < grp.batchCount; b++) {
                    if (grp.batches[b].indexCount && grp.batches[b].blend >= 2) {
                        s_blended.push_back({ &grp, b, d, w.origin });
                    }
                }
            }
        }
    }

    std::sort(s_blended.begin(), s_blended.end(), [](const BlendedRef& a, const BlendedRef& b) {
        return a.dist > b.dist; // farthest first
    });

    GxRsSet(GxRs_DepthWrite, 0);

    for (const BlendedRef& ref : s_blended) {
        WmoGroup& grp = *ref.grp;

        C44Matrix instanceT = ChunkMatrixT(ref.origin);
        GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&instanceT), 4);
        const WmoBatch& batch = grp.batches[ref.batch];
        const CImVector* colors = batch.unlit ? s_wmoWhite : grp.colors;

        GxPrimLockVertexPtrs(
            grp.vertexCount,
            grp.positions, sizeof(C3Vector),
            nullptr, 0,
            colors, sizeof(CImVector),
            nullptr, 0,
            grp.texcoords, sizeof(C2Vector),
            nullptr, 0
        );

        GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
        GxRsSet(GxRs_AlphaRef, 0); // alpha-blended, not alpha-tested
        GxRsSet(GxRs_Culling, batch.twoSided ? 0 : 1);
        GxRsSet(GxRs_Fog, (s_fogActive && !batch.unfogged) ? 1 : 0);
        GxRsSet(GxRs_Texture0, batch.texture ? TextureGetGxTex(batch.texture, 0, nullptr) : nullptr);
        GxDrawLockedElements(GxPrim_Triangles, batch.indexCount, grp.indices + batch.indexStart);
        GxPrimUnlockVertexPtrs();
    }

    GxRsSet(GxRs_AlphaRef, 0); // leave the alpha test disabled for later passes
}

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

void ParseLiquid(TerrainChunk& chunk, int32_t chunkIndex, const uint8_t* mh2o, uint32_t mh2oSize) {
    if (!chunk.valid || static_cast<uint32_t>(chunkIndex + 1) * sizeof(Mh2oHeader) > mh2oSize) {
        return;
    }

    const Mh2oHeader* hdr = reinterpret_cast<const Mh2oHeader*>(mh2o) + chunkIndex;

    if (!hdr->layerCount || !hdr->ofsInformation) {
        return;
    }

    uint32_t layers = hdr->layerCount > 4 ? 4 : hdr->layerCount;

    if (hdr->ofsInformation + layers * sizeof(Mh2oInfo) > mh2oSize) {
        return;
    }

    // The chunk's north-west corner (row 0, column 0 of the height grid)
    float posX = chunk.position[0].x;
    float posY = chunk.position[0].y;

    chunk.liquids = static_cast<ChunkLiquid*>(SMemAlloc(layers * sizeof(ChunkLiquid), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));

    for (uint32_t l = 0; l < layers; l++) {
        const Mh2oInfo* info = reinterpret_cast<const Mh2oInfo*>(mh2o + hdr->ofsInformation) + l;
        uint32_t w = info->width;
        uint32_t h = info->height;

        if (!w || !h || info->xOffset + w > 8 || info->yOffset + h > 8) {
            continue;
        }

        uint32_t vw = w + 1;
        uint32_t vh = h + 1;
        const float* heights = nullptr;

        if (info->vertexFormat != 2 && info->ofsHeightMap && info->ofsHeightMap + vw * vh * sizeof(float) <= mh2oSize) {
            heights = reinterpret_cast<const float*>(mh2o + info->ofsHeightMap);
        }

        // Vertex data layout by format: 0 = heights then depth bytes, 1 = heights then UVs,
        // 2 = depth bytes only, 3 = heights, UVs, then depth bytes. Only 0, 2 and 3 carry depth.
        const uint8_t* depth = nullptr;

        if (info->ofsHeightMap) {
            uint32_t n = vw * vh;
            uint32_t off = info->ofsHeightMap;

            if (info->vertexFormat == 2) {
                depth = mh2o + off;
            } else {
                off += n * sizeof(float); // heights

                if (info->vertexFormat == 3) {
                    off += n * 2 * sizeof(float); // uvs
                }

                if (info->vertexFormat == 0 || info->vertexFormat == 3) {
                    depth = mh2o + off;
                }
            }

            if (depth && off + n > mh2oSize) {
                depth = nullptr;
            }
        }

        const uint8_t* mask = nullptr;

        if (info->ofsMask && info->ofsMask + 8 <= mh2oSize) {
            mask = mh2o + info->ofsMask;
        }

        ChunkLiquid& liq = chunk.liquids[chunk.liquidCount];
        liq.liquidType = info->liquidType;
        liq.xOffset = info->xOffset;
        liq.yOffset = info->yOffset;
        liq.width = static_cast<uint8_t>(w);
        liq.height = static_cast<uint8_t>(h);

        // Re-pack the layer's sub-rect bitmap into the chunk's 8x8 grid, which is what LiquidAt
        // queries against.
        for (int32_t m = 0; m < 8; m++) {
            liq.cellMask[m] = 0;
        }

        for (uint32_t j = 0; j < h; j++) {
            for (uint32_t i = 0; i < w; i++) {
                uint32_t bit = j * w + i;
                bool covered = mask ? (mask[bit >> 3] & (1 << (bit & 7))) != 0 : true;

                if (covered) {
                    liq.cellMask[info->yOffset + j] |= static_cast<uint8_t>(1 << (info->xOffset + i));
                }
            }
        }

        auto rec = g_liquidTypeDB.GetRecord(info->liquidType);
        liq.kind = rec ? rec->m_type : 0;

        liq.vertCount = vw * vh;
        liq.verts = static_cast<C3Vector*>(SMemAlloc(liq.vertCount * sizeof(C3Vector), __FILE__, __LINE__, 0));
        liq.uvs = static_cast<C2Vector*>(SMemAlloc(liq.vertCount * sizeof(C2Vector), __FILE__, __LINE__, 0));

        if (depth) {
            liq.colors = static_cast<CImVector*>(SMemAlloc(liq.vertCount * sizeof(CImVector), __FILE__, __LINE__, 0));
            // One byte of depth ramp per vertex, so the colours can be rebuilt when the light
            // changes without re-reading the ADT. The reference rebuilds its gradient table for the
            // same reason; baking once at load leaves water lit by whatever zone it loaded under.
            liq.depthRamp = static_cast<uint8_t*>(SMemAlloc(liq.vertCount, __FILE__, __LINE__, 0));
        }
        liq.indices = static_cast<uint16_t*>(SMemAlloc(w * h * 6 * sizeof(uint16_t), __FILE__, __LINE__, 0));

        for (uint32_t j = 0; j < vh; j++) {
            for (uint32_t i = 0; i < vw; i++) {
                uint32_t k = j * vw + i;
                float row = static_cast<float>(info->yOffset + j);
                float col = static_cast<float>(info->xOffset + i);
                liq.verts[k].x = posX - row * UNIT_SIZE;
                liq.verts[k].y = posY - col * UNIT_SIZE;
                liq.verts[k].z = heights ? heights[k] : info->minHeight;
                // Surface texture repeats every four cells (~16.7 yd), continuous across chunks
                liq.uvs[k].x = col * 0.25f;
                liq.uvs[k].y = row * 0.25f;

                if (liq.colors) {
                    // MH2O depth is 0 at the shoreline and 255 at full depth. LiquidType's
                    // maxDarkenDepth says how deep the surface reaches full opacity.
                    float maxDepth = rec && rec->m_maxDarkenDepth > 0.0f ? rec->m_maxDarkenDepth : 8.0f;
                    float d = static_cast<float>(depth[k]) / 255.0f * 16.0f; // bytes span ~16 yards
                    float t = d / maxDepth;

                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;

                    if (liq.depthRamp) {
                        liq.depthRamp[k] = static_cast<uint8_t>(t * 255.0f);
                    }

                    // Colour interpolates from the shallow endpoint to the deep one across the
                    // same depth ramp, which is what the reference's 512-entry gradient table is.
                    // Magma and slime are not light-driven and stay white, tinted by their texture.
                    if (liq.kind == 2 || liq.kind == 3) {
                        liq.colors[k].b = 0xFF;
                        liq.colors[k].g = 0xFF;
                        liq.colors[k].r = 0xFF;
                    } else {
                        const C3Vector& shallowC = CWorld::GetLiquidShallow(liq.kind == 1);
                        const C3Vector& deepC = CWorld::GetLiquidDeep(liq.kind == 1);

                        liq.colors[k].r = static_cast<uint8_t>((shallowC.x + (deepC.x - shallowC.x) * t) * 255.0f);
                        liq.colors[k].g = static_cast<uint8_t>((shallowC.y + (deepC.y - shallowC.y) * t) * 255.0f);
                        liq.colors[k].b = static_cast<uint8_t>((shallowC.z + (deepC.z - shallowC.z) * t) * 255.0f);
                    }

                    // Interpolate the alpha from shallow to deep, the way the reference's gradient
                    // does, instead of ramping from zero. The old form multiplied the depth ramp by
                    // a fixed opacity, so a surface at zero depth came out FULLY TRANSPARENT; the
                    // reference starts it at the LightParams shallow alpha (0.5 for river water,
                    // 0.75 for ocean on these params) and only reaches full at maxDarkenDepth.
                    float alpha;

                    if (liq.kind == 2 || liq.kind == 3) {
                        alpha = 1.0f;   // magma and slime are opaque and not light-driven
                    } else {
                        float shallow = CWorld::GetLiquidAlpha(liq.kind == 1, 0);
                        float deep = CWorld::GetLiquidAlpha(liq.kind == 1, 1);
                        alpha = shallow + (deep - shallow) * t;
                    }

                    liq.colors[k].a = static_cast<uint8_t>(alpha * 255.0f);
                }

                if (k == 0) {
                    liq.boundsMin = liq.boundsMax = liq.verts[k];
                } else {
                    const C3Vector& v = liq.verts[k];
                    liq.boundsMin.x = v.x < liq.boundsMin.x ? v.x : liq.boundsMin.x;
                    liq.boundsMin.y = v.y < liq.boundsMin.y ? v.y : liq.boundsMin.y;
                    liq.boundsMin.z = v.z < liq.boundsMin.z ? v.z : liq.boundsMin.z;
                    liq.boundsMax.x = v.x > liq.boundsMax.x ? v.x : liq.boundsMax.x;
                    liq.boundsMax.y = v.y > liq.boundsMax.y ? v.y : liq.boundsMax.y;
                    liq.boundsMax.z = v.z > liq.boundsMax.z ? v.z : liq.boundsMax.z;
                }
            }
        }

        uint32_t n = 0;

        for (uint32_t j = 0; j < h; j++) {
            for (uint32_t i = 0; i < w; i++) {
                if (mask) {
                    // The exists bitmap covers this layer's w x h sub-rect, packed row by row --
                    // ceil(w*h/8) bytes -- not the chunk's full 8x8 grid. Indexing it as an 8x8
                    // grid read the wrong bits and produced water over the wrong cells.
                    uint32_t bit = j * w + i;

                    if (!(mask[bit >> 3] & (1 << (bit & 7)))) {
                        continue;
                    }
                }

                uint16_t a = static_cast<uint16_t>(j * vw + i);
                uint16_t b = static_cast<uint16_t>(a + 1);
                uint16_t c = static_cast<uint16_t>(a + vw);
                uint16_t d = static_cast<uint16_t>(c + 1);
                liq.indices[n++] = a; liq.indices[n++] = c; liq.indices[n++] = b;
                liq.indices[n++] = b; liq.indices[n++] = c; liq.indices[n++] = d;
            }
        }

        liq.indexCount = n;

        if (n) {
            chunk.liquidCount++;
        } else {
            SMemFree(liq.verts, __FILE__, __LINE__, 0);
            SMemFree(liq.uvs, __FILE__, __LINE__, 0);
            SMemFree(liq.indices, __FILE__, __LINE__, 0);
            liq.verts = nullptr; liq.uvs = nullptr; liq.indices = nullptr;
        }
    }
}


// The liquid surface above a point, if the point lies under one of the loaded MH2O layers: the
// covering cell's highest corner is the surface. Returns the layer's kind, or -1 when in air.
int32_t LiquidAt(const C3Vector& pos, float& surfaceZ) {
    int32_t found = -1;
    surfaceZ = 0.0f;

    // A surface is above the point when the point is inside its box and under its top. Terrain
    // layers additionally resolve the exact covering cell; WMO surfaces (canals, interior pools)
    // are tested by box alone, which is enough to decide submersion.
    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (uint32_t wi = 0; wi < tile.wmoCount && tile.wmos; wi++) {
            const WmoInstance& w = tile.wmos[wi];

            for (uint32_t l = 0; l < w.liquidCount; l++) {
                const ChunkLiquid& liq = w.liquids[l];

                if (!liq.indexCount || pos.x < liq.boundsMin.x || pos.x > liq.boundsMax.x
                    || pos.y < liq.boundsMin.y || pos.y > liq.boundsMax.y
                    || pos.z > liq.boundsMax.z || pos.z < liq.boundsMin.z) {
                    continue;
                }

                if (found < 0 || liq.boundsMax.z > surfaceZ) {
                    found = liq.kind;
                    s_cameraLiquidType = liq.liquidType;
                    surfaceZ = liq.boundsMax.z;
                }
            }
        }

        for (auto& chunk : tile.chunks) {
            if (!chunk.liquidCount || !chunk.valid) {
                continue;
            }

            for (uint32_t l = 0; l < chunk.liquidCount; l++) {
                const ChunkLiquid& liq = chunk.liquids[l];

                if (pos.x < liq.boundsMin.x || pos.x > liq.boundsMax.x || pos.y < liq.boundsMin.y || pos.y > liq.boundsMax.y || pos.z > liq.boundsMax.z) {
                    continue;
                }

                // Chunk-relative cell of the point (rows run along -x, columns along -y)
                float rowF = (chunk.position[0].x - pos.x) / UNIT_SIZE;
                float colF = (chunk.position[0].y - pos.y) / UNIT_SIZE;
                int32_t row = static_cast<int32_t>(rowF);
                int32_t col = static_cast<int32_t>(colF);

                if (row < 0 || row > 7 || col < 0 || col > 7) {
                    continue;
                }

                int32_t bit = row * 8 + col;

                if (!(liq.cellMask[bit >> 3] & (1 << (bit & 7)))) {
                    continue;
                }

                int32_t j = row - liq.yOffset;
                int32_t i = col - liq.xOffset;

                if (j < 0 || j >= liq.height || i < 0 || i >= liq.width) {
                    continue;
                }

                uint32_t vw = liq.width + 1;
                uint32_t k = static_cast<uint32_t>(j) * vw + static_cast<uint32_t>(i);
                float z = liq.verts[k].z;
                z = liq.verts[k + 1].z > z ? liq.verts[k + 1].z : z;
                z = liq.verts[k + vw].z > z ? liq.verts[k + vw].z : z;
                z = liq.verts[k + vw + 1].z > z ? liq.verts[k + vw + 1].z : z;

                if (pos.z < z && (found < 0 || z > surfaceZ)) {
                    found = liq.kind;
                    s_cameraLiquidType = liq.liquidType;
                    surfaceZ = z;
                }
            }
        }
    }

    return found;
}



// ------------------------------------------------------------------------------------------------
// Weather (the reference's MapWeather: FUN_0078ca50 draws three emitters -- rain drops
// textures\Weather\RainDrop01.blp, snow textures\Weather\SnowMist01.blp, mist
// textures\Weather\WeatherMistGrainy01.blp -- around the camera). This is a stand-in particle
// field: sprites fall inside a box that follows the camera, re-seeded when they leave it.
// ------------------------------------------------------------------------------------------------

const int32_t WEATHER_MAX_PARTICLES = 1024;
const float WEATHER_RADIUS = 28.0f; // half-size of the box around the camera, yards
const float WEATHER_TOP = 24.0f;
const float WEATHER_BOTTOM = -12.0f;

struct WeatherParticle {
    C3Vector pos;
    float speed;
    float phase;
};

int32_t s_weatherType = 0;          // 0 clear, 1 rain, 2 snow, 3 sand/mist
int32_t s_weatherTargetType = 0;
float s_weatherIntensity = 0.0f;    // current, eased toward the target
float s_weatherTarget = 0.0f;
C3Vector s_weatherColor = { 1.0f, 1.0f, 1.0f };
char s_weatherTexturePath[260] = { 0 };
HTEXTURE s_weatherTexture = nullptr;
char s_weatherTextureLoaded[260] = { 0 };
WeatherParticle s_weatherParticles[WEATHER_MAX_PARTICLES];
int32_t s_weatherAlive = 0;
uint32_t s_weatherRand = 0x12345678;
uint32_t s_weatherLastTime = 0;

C3Vector s_weatherPos[WEATHER_MAX_PARTICLES * 4];
C2Vector s_weatherUv[WEATHER_MAX_PARTICLES * 4];
CImVector s_weatherCol[WEATHER_MAX_PARTICLES * 4];
uint16_t s_weatherIdx[WEATHER_MAX_PARTICLES * 6];
bool s_weatherIdxBuilt = false;

float WeatherRand() {
    s_weatherRand = s_weatherRand * 1664525u + 1013904223u;
    return static_cast<float>((s_weatherRand >> 8) & 0xFFFF) / 65535.0f;
}

void WeatherSeed(WeatherParticle& p, const C3Vector& cam, bool atTop) {
    p.pos.x = cam.x + (WeatherRand() * 2.0f - 1.0f) * WEATHER_RADIUS;
    p.pos.y = cam.y + (WeatherRand() * 2.0f - 1.0f) * WEATHER_RADIUS;
    p.pos.z = cam.z + (atTop ? WEATHER_TOP : (WEATHER_BOTTOM + WeatherRand() * (WEATHER_TOP - WEATHER_BOTTOM)));
    p.phase = WeatherRand() * 6.2831853f;

    switch (s_weatherType) {
        case 1: p.speed = 22.0f + WeatherRand() * 8.0f; break;  // rain: fast, near-vertical streaks
        case 2: p.speed = 1.2f + WeatherRand() * 1.2f; break;   // snow: slow drifting flakes
        default: p.speed = 0.4f + WeatherRand() * 0.6f; break;  // mist: barely sinking sheets
    }
}

const char* WeatherDefaultTexture(int32_t type) {
    switch (type) {
        case 1: return "textures\\Weather\\RainDrop01.blp";
        case 2: return "textures\\Weather\\SnowMist01.blp";
        case 3: return "textures\\Weather\\WeatherMistGrainy01.blp";
        default: return nullptr;
    }
}

} // namespace (weather state)

void TerrainSetWeather(int32_t effectType, float intensity, const float* color, const char* texture, bool abrupt) {
    if (effectType < 1 || effectType > 3) {
        effectType = 0;
        intensity = 0.0f;
    }

    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    s_weatherTargetType = effectType;
    s_weatherTarget = intensity;

    if (color) {
        s_weatherColor = { color[0], color[1], color[2] };
    } else {
        s_weatherColor = { 1.0f, 1.0f, 1.0f };
    }

    const char* path = texture ? texture : WeatherDefaultTexture(effectType);
    SStrCopy(s_weatherTexturePath, path ? path : "", sizeof(s_weatherTexturePath));

    if (abrupt || s_weatherType == 0) {
        s_weatherType = effectType;
        s_weatherIntensity = intensity;
        s_weatherAlive = 0;
    }
}

namespace {

void WeatherUpdate(const C3Vector& cam, float dt) {
    // Ease toward the target; a type change waits for the old weather to fade out
    if (s_weatherType != s_weatherTargetType) {
        s_weatherIntensity -= dt * 0.5f;

        if (s_weatherIntensity <= 0.0f) {
            s_weatherIntensity = 0.0f;
            s_weatherType = s_weatherTargetType;
            s_weatherAlive = 0;
        }
    } else if (s_weatherIntensity < s_weatherTarget) {
        s_weatherIntensity = s_weatherIntensity + dt * 0.5f > s_weatherTarget ? s_weatherTarget : s_weatherIntensity + dt * 0.5f;
    } else if (s_weatherIntensity > s_weatherTarget) {
        s_weatherIntensity = s_weatherIntensity - dt * 0.5f < s_weatherTarget ? s_weatherTarget : s_weatherIntensity - dt * 0.5f;
    }

    if (!s_weatherType || s_weatherIntensity <= 0.0f) {
        s_weatherAlive = 0;
        return;
    }

    // Density: the weatherDensity cvar (0..3, default 2) scales the particle budget
    float density = 1.0f;
    CVar* dv = CVar::Lookup("weatherDensity");

    if (dv) {
        density = dv->GetFloat() * 0.5f;
        if (density < 0.0f) density = 0.0f;
        if (density > 1.5f) density = 1.5f;
    }

    int32_t budget = s_weatherType == 3 ? 160 : (s_weatherType == 2 ? 600 : 900);
    int32_t want = static_cast<int32_t>(budget * s_weatherIntensity * density);

    if (want > WEATHER_MAX_PARTICLES) want = WEATHER_MAX_PARTICLES;

    while (s_weatherAlive < want) {
        WeatherSeed(s_weatherParticles[s_weatherAlive++], cam, false);
    }

    if (s_weatherAlive > want) {
        s_weatherAlive = want;
    }

    for (int32_t i = 0; i < s_weatherAlive; i++) {
        WeatherParticle& p = s_weatherParticles[i];
        p.pos.z -= p.speed * dt;

        if (s_weatherType != 1) {
            p.phase += dt;
            p.pos.x += sinf(p.phase) * dt * 0.8f;
            p.pos.y += cosf(p.phase * 0.7f) * dt * 0.8f;
        }

        bool outside = p.pos.x < cam.x - WEATHER_RADIUS || p.pos.x > cam.x + WEATHER_RADIUS
            || p.pos.y < cam.y - WEATHER_RADIUS || p.pos.y > cam.y + WEATHER_RADIUS
            || p.pos.z > cam.z + WEATHER_TOP + 1.0f;

        if (p.pos.z < cam.z + WEATHER_BOTTOM) {
            WeatherSeed(p, cam, true);
        } else if (outside) {
            WeatherSeed(p, cam, false);
        }
    }
}

} // namespace (weather update)

void WeatherRender() {
    uint32_t now = CWorld::GetM2Scene() ? CWorld::GetM2Scene()->m_time : 0;
    float dt = s_weatherLastTime ? static_cast<float>(now - s_weatherLastTime) * 0.001f : 0.0f;
    s_weatherLastTime = now;

    if (dt > 0.1f) dt = 0.1f;

    WeatherUpdate(s_cameraPos, dt);

    if (!s_weatherAlive || s_cameraLiquidKind >= 0) {
        return;
    }

    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    if (SStrCmpI(s_weatherTexturePath, s_weatherTextureLoaded, 0x7FFFFFFF) != 0) {
        if (s_weatherTexture) {
            HandleClose(s_weatherTexture);
            s_weatherTexture = nullptr;
        }

        if (s_weatherTexturePath[0]) {
            CStatus status;
            s_weatherTexture = TextureCreate(s_weatherTexturePath, CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 0);
        }

        SStrCopy(s_weatherTextureLoaded, s_weatherTexturePath, sizeof(s_weatherTextureLoaded));
    }

    if (!s_weatherTexture) {
        return;
    }

    if (!s_weatherIdxBuilt) {
        for (int32_t i = 0; i < WEATHER_MAX_PARTICLES; i++) {
            uint16_t b = static_cast<uint16_t>(i * 4);
            s_weatherIdx[i * 6 + 0] = b; s_weatherIdx[i * 6 + 1] = b + 1; s_weatherIdx[i * 6 + 2] = b + 2;
            s_weatherIdx[i * 6 + 3] = b; s_weatherIdx[i * 6 + 4] = b + 2; s_weatherIdx[i * 6 + 5] = b + 3;
        }

        s_weatherIdxBuilt = true;
    }

    // Camera basis for the billboards: right is horizontal, up follows the world for rain streaks
    // (so they hang vertically) and the camera for flakes and mist
    const C3Vector& fwd = CWorld::GetCameraDir();
    C3Vector right = { fwd.y, -fwd.x, 0.0f };
    float rl = sqrtf(right.x * right.x + right.y * right.y);

    if (rl < 1e-4f) {
        right = { 1.0f, 0.0f, 0.0f };
    } else {
        right.x /= rl; right.y /= rl;
    }

    C3Vector up = { 0.0f, 0.0f, 1.0f };

    if (s_weatherType != 1) {
        up = { right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y - right.y * fwd.x };
    }

    float halfW, halfH, alphaScale;

    switch (s_weatherType) {
        case 1: halfW = 0.035f; halfH = 0.9f; alphaScale = 0.55f; break;
        case 2: halfW = 0.16f; halfH = 0.16f; alphaScale = 0.9f; break;
        default: halfW = 3.0f; halfH = 2.0f; alphaScale = 0.35f; break;
    }

    float a = s_weatherIntensity * alphaScale;
    CImVector col;
    col.b = static_cast<uint8_t>(s_weatherColor.z * 255.0f);
    col.g = static_cast<uint8_t>(s_weatherColor.y * 255.0f);
    col.r = static_cast<uint8_t>(s_weatherColor.x * 255.0f);
    col.a = static_cast<uint8_t>((a > 1.0f ? 1.0f : a) * 255.0f);

    for (int32_t i = 0; i < s_weatherAlive; i++) {
        const C3Vector& p = s_weatherParticles[i].pos;
        C3Vector rx = { right.x * halfW, right.y * halfW, right.z * halfW };
        C3Vector uy = { up.x * halfH, up.y * halfH, up.z * halfH };
        int32_t b = i * 4;
        s_weatherPos[b + 0] = { p.x - rx.x + uy.x, p.y - rx.y + uy.y, p.z - rx.z + uy.z };
        s_weatherPos[b + 1] = { p.x + rx.x + uy.x, p.y + rx.y + uy.y, p.z + rx.z + uy.z };
        s_weatherPos[b + 2] = { p.x + rx.x - uy.x, p.y + rx.y - uy.y, p.z + rx.z - uy.z };
        s_weatherPos[b + 3] = { p.x - rx.x - uy.x, p.y - rx.y - uy.y, p.z - rx.z - uy.z };
        s_weatherUv[b + 0] = { 0.0f, 0.0f };
        s_weatherUv[b + 1] = { 1.0f, 0.0f };
        s_weatherUv[b + 2] = { 1.0f, 1.0f };
        s_weatherUv[b + 3] = { 0.0f, 1.0f };
        s_weatherCol[b + 0] = col; s_weatherCol[b + 1] = col; s_weatherCol[b + 2] = col; s_weatherCol[b + 3] = col;
    }

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_AlphaRef, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, s_fogActive ? 1 : 0);
    GxRsSet(GxRs_VertexShader, s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, s_uiPixelShader);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&s_viewProjT), 4);
    GxRsSet(GxRs_Texture0, TextureGetGxTex(s_weatherTexture, 0, nullptr));

    GxPrimLockVertexPtrs(
        s_weatherAlive * 4,
        s_weatherPos, sizeof(C3Vector),
        nullptr, 0,
        s_weatherCol, sizeof(CImVector),
        nullptr, 0,
        s_weatherUv, sizeof(C2Vector),
        nullptr, 0
    );
    GxDrawLockedElements(GxPrim_Triangles, s_weatherAlive * 6, s_weatherIdx);
    GxPrimUnlockVertexPtrs();

    GxRsPop();
}

namespace {

// ------------------------------------------------------------------------------------------------
// Detail doodad batches. Each GroundEffectDoodad model is loaded once (World\NoDXT\Detail\...) and
// its skin geometry extracted; a chunk within groundEffectDist merges its placements into a single
// vertex set (one draw range per model texture), lit by the terrain colour under each doodad so
// the grass matches the ground. The reference builds equivalent batches in its DetailDoodad
// module and draws them with dedicated shaders; here the UI shader draws them alpha-keyed.
// ------------------------------------------------------------------------------------------------

struct DetailMesh {
    int32_t doodadID = 0;
    CM2Model* model = nullptr;
    bool ready = false;
    bool failed = false;
    std::vector<C3Vector> positions;
    std::vector<C2Vector> uvs;
    std::vector<uint16_t> indices;
    HTEXTURE texture = nullptr;
    uint32_t blend = 1;
};

CM2Scene* s_detailScene = nullptr;
std::vector<DetailMesh> s_detailMeshes;

DetailMesh* GetDetailMesh(int32_t doodadID) {
    for (DetailMesh& m : s_detailMeshes) {
        if (m.doodadID == doodadID) {
            return &m;
        }
    }

    s_detailMeshes.push_back(DetailMesh());
    DetailMesh& m = s_detailMeshes.back();
    m.doodadID = doodadID;

    auto rec = g_groundEffectDoodadDB.GetRecord(doodadID);

    if (!rec || !rec->m_doodadPath || !*rec->m_doodadPath) {
        m.failed = true;
        return &m;
    }

    if (!s_detailScene) {
        s_detailScene = M2CreateScene();
    }

    char path[260];
    SStrPrintf(path, sizeof(path), "World\\NoDXT\\Detail\\%s", rec->m_doodadPath);
    m.model = s_detailScene ? s_detailScene->CreateModel(path, 0) : nullptr;

    if (!m.model) {
        m.failed = true;
    }

    return &m;
}

// Pull the model's skin geometry once it has finished loading
bool DetailMeshResolve(DetailMesh& m) {
    if (m.ready || m.failed) {
        return m.ready;
    }

    CM2Shared* shared = m.model ? m.model->m_shared : nullptr;

    if (!shared || !shared->m_m2DataLoaded || !shared->m_skinProfileLoaded || !shared->m_data || !shared->skinProfile || !shared->m_skinSections || !shared->textures) {
        return false;
    }

    M2Data* data = shared->m_data;
    M2SkinProfile* skin = shared->skinProfile;

    if (!skin->batches.Count() || !skin->indices.Count() || !skin->vertices.Count()) {
        m.failed = true;
        return false;
    }

    // The first textured batch decides the texture and blend; detail models are single-material
    for (uint32_t b = 0; b < skin->batches.Count(); b++) {
        const M2Batch& batch = skin->batches[b];

        if (batch.skinSectionIndex >= skin->skinSections.Count()) {
            continue;
        }

        const M2SkinSection& section = skin->skinSections[batch.skinSectionIndex];

        if (!m.texture && batch.textureComboIndex < data->textureCombos.Count()) {
            uint16_t texIndex = data->textureCombos[batch.textureComboIndex];

            if (texIndex < data->textures.Count()) {
                m.texture = shared->textures[texIndex];
            }
        }

        if (batch.materialIndex < data->materials.Count()) {
            m.blend = data->materials[batch.materialIndex].blendMode;
        }

        uint32_t base = static_cast<uint32_t>(m.positions.size());

        for (uint32_t v = 0; v < section.vertexCount; v++) {
            uint32_t lookup = section.vertexStart + v;

            if (lookup >= skin->vertices.Count()) {
                break;
            }

            uint16_t vi = skin->vertices[lookup];

            if (vi >= data->vertices.Count()) {
                break;
            }

            const M2Vertex& mv = data->vertices[vi];
            m.positions.push_back(mv.position);
            m.uvs.push_back(mv.texcoord[0]);
        }

        for (uint32_t i = 0; i < section.indexCount; i++) {
            uint32_t ii = section.indexStart + i;

            if (ii >= skin->indices.Count()) {
                break;
            }

            uint16_t local = skin->indices[ii];

            if (local < section.vertexStart || local >= section.vertexStart + section.vertexCount) {
                continue;
            }

            m.indices.push_back(static_cast<uint16_t>(base + (local - section.vertexStart)));
        }

        break; // one section is enough for a detail doodad
    }

    if (m.positions.empty() || m.indices.empty()) {
        m.failed = true;
        return false;
    }

    // The texture must have finished loading too; leave the model alive so the shared data stays
    if (!m.texture) {
        m.failed = true;
        return false;
    }

    m.ready = true;
    return true;
}

} // namespace (detail meshes)

namespace {

void FreeDetailBatch(TerrainChunk& chunk) {
    DetailBatch* b = chunk.detailBatch;

    if (!b) {
        return;
    }

    if (b->positions) SMemFree(b->positions, __FILE__, __LINE__, 0);
    if (b->uvs) SMemFree(b->uvs, __FILE__, __LINE__, 0);
    if (b->colors) SMemFree(b->colors, __FILE__, __LINE__, 0);
    if (b->indices) SMemFree(b->indices, __FILE__, __LINE__, 0);
    if (b->ranges) SMemFree(b->ranges, __FILE__, __LINE__, 0);
    SMemFree(b, __FILE__, __LINE__, 0);
    chunk.detailBatch = nullptr;
    chunk.detailDirty = true;
}

// Lit colour of the ground at a point in the chunk (nearest outer vertex)
CImVector ChunkColorAt(const TerrainChunk& chunk, const C3Vector& p) {
    float rowF = (chunk.position[0].x - p.x) / UNIT_SIZE + 0.5f;
    float colF = (chunk.position[0].y - p.y) / UNIT_SIZE + 0.5f;
    int32_t r = static_cast<int32_t>(rowF);
    int32_t c = static_cast<int32_t>(colF);

    if (r < 0) r = 0; if (r > 8) r = 8;
    if (c < 0) c = 0; if (c > 8) c = 8;

    return chunk.color[r * 17 + c];
}

const uint32_t DETAIL_MAX_VERTICES = 48000;

void BuildDetailBatch(TerrainChunk& chunk) {
    FreeDetailBatch(chunk);
    chunk.detailDirty = false;

    if (!chunk.detailCount) {
        return;
    }

    // Placements grouped by model so each texture is one draw range
    std::vector<uint32_t> order(chunk.detailCount);

    for (uint32_t i = 0; i < chunk.detailCount; i++) {
        order[i] = i;
    }

    std::sort(order.begin(), order.end(), [&chunk](uint32_t a, uint32_t b) {
        return chunk.details[a].doodadID < chunk.details[b].doodadID;
    });

    // Size pass
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t rangeCount = 0;
    int32_t lastID = -1;
    bool complete = true;

    for (uint32_t oi = 0; oi < chunk.detailCount; oi++) {
        const DetailPlacement& p = chunk.details[order[oi]];
        DetailMesh* mesh = GetDetailMesh(p.doodadID);

        if (!mesh || mesh->failed) {
            continue;
        }

        if (!DetailMeshResolve(*mesh)) {
            complete = false;
            continue;
        }

        if (vertexCount + mesh->positions.size() > DETAIL_MAX_VERTICES) {
            break;
        }

        vertexCount += static_cast<uint32_t>(mesh->positions.size());
        indexCount += static_cast<uint32_t>(mesh->indices.size());

        if (p.doodadID != lastID) {
            rangeCount++;
            lastID = p.doodadID;
        }
    }

    if (!vertexCount || !indexCount) {
        if (!complete) {
            chunk.detailDirty = true; // try again once the models are in
        }

        return;
    }

    DetailBatch* b = static_cast<DetailBatch*>(SMemAlloc(sizeof(DetailBatch), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    b->positions = static_cast<C3Vector*>(SMemAlloc(vertexCount * sizeof(C3Vector), __FILE__, __LINE__, 0));
    b->uvs = static_cast<C2Vector*>(SMemAlloc(vertexCount * sizeof(C2Vector), __FILE__, __LINE__, 0));
    b->colors = static_cast<CImVector*>(SMemAlloc(vertexCount * sizeof(CImVector), __FILE__, __LINE__, 0));
    b->indices = static_cast<uint16_t*>(SMemAlloc(indexCount * sizeof(uint16_t), __FILE__, __LINE__, 0));
    b->ranges = static_cast<DetailBatch::Range*>(SMemAlloc(rangeCount * sizeof(DetailBatch::Range), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    b->complete = complete;

    uint32_t v = 0;
    uint32_t n = 0;
    lastID = -1;
    DetailBatch::Range* range = nullptr;

    for (uint32_t oi = 0; oi < chunk.detailCount; oi++) {
        const DetailPlacement& p = chunk.details[order[oi]];
        DetailMesh* mesh = GetDetailMesh(p.doodadID);

        if (!mesh || !mesh->ready) {
            continue;
        }

        if (v + mesh->positions.size() > vertexCount) {
            break;
        }

        if (p.doodadID != lastID) {
            lastID = p.doodadID;
            range = &b->ranges[b->rangeCount++];
            range->texture = mesh->texture;
            range->blend = mesh->blend;
            range->indexStart = n;
            range->indexCount = 0;
        }

        float cy = cosf(p.yaw);
        float sy = sinf(p.yaw);
        CImVector color = ChunkColorAt(chunk, p.position);
        color.a = 0xFF; // the chunk colour's alpha is the terrain's ambient ratio, not an opacity
        uint32_t base = v;

        for (size_t k = 0; k < mesh->positions.size(); k++) {
            const C3Vector& l = mesh->positions[k];
            float lx = l.x * p.scale;
            float ly = l.y * p.scale;
            float lz = l.z * p.scale;
            // Chunk-local, to match the terrain pass this geometry sits on
            b->positions[v] = {
                p.position.x + lx * cy - ly * sy - chunk.origin.x,
                p.position.y + lx * sy + ly * cy - chunk.origin.y,
                p.position.z + lz - chunk.origin.z
            };
            b->uvs[v] = mesh->uvs[k];
            b->colors[v] = color;
            v++;
        }

        for (size_t k = 0; k < mesh->indices.size(); k++) {
            b->indices[n++] = static_cast<uint16_t>(base + mesh->indices[k]);
        }

        range->indexCount += static_cast<uint32_t>(mesh->indices.size());
    }

    b->vertexCount = v;
    b->indexCount = n;
    chunk.detailBatch = b;

    if (!complete) {
        chunk.detailDirty = true;
    }
}

} // namespace (detail batches)

void DetailDoodadRender() {
    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    float dist = CWorldParam::cvar_groundEffectDist ? CWorldParam::cvar_groundEffectDist->GetFloat() : 70.0f;

    // groundEffectDensity scales how much of each chunk's scatter is drawn. It was registered and
    // then ignored, so the slider did nothing until this read was added.
    //
    // **This is not what the reference does with it, and the difference only shows above the
    // default.** Its callback (FUN_0078dab0) accepts 16 to 256 and hands the value to a setter
    // (FUN_00780710) that raises a scatter-rebuild flag, so density controls how many doodads are
    // PLACED, with 16 as the minimum. Here it is a draw fraction with 16 as the maximum. The two
    // agree exactly at the default of 16; above it the reference adds doodads and this does
    // nothing.
    //
    // A first reading of this guessed that density sets a per-chunk placement count. It does not.
    // The rebuild flag its setter raises is consumed at 0x007b2a86, and what happens there is
    // `density << 6` clamped to 0x1000: a GLOBAL POOL SIZE of doodad instances, 1024 at the default
    // 16 and 4096 at the cap, which then sizes several derived buffers. So density bounds how many
    // detail doodads can be resident at once, not how many any one chunk scatters. frozen has no
    // such pool at all -- it pre-builds a per-chunk scatter and draws a fraction of it -- so this
    // is an architectural difference and closing it is a rewrite of the system, not an edit.
    //
    // groundEffectDist is closer: the reference clamps it to [0, 140] on the way in and caches its
    // square (FUN_00780730); this reads the raw CVar and squares it per frame. Defaults match.
    float density = 1.0f;

    if (CWorldParam::cvar_groundEffectDensity) {
        density = CWorldParam::cvar_groundEffectDensity->GetFloat() / 16.0f;
        density = density < 0.0f ? 0.0f : (density > 1.0f ? 1.0f : density);
    }

    if (dist <= 0.0f || density <= 0.0f) {
        return;
    }

    float distSq = dist * dist;

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_DepthWrite, 1);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, s_fogActive ? 1 : 0);
    // Ground doodads sit exactly on the terrain surface, so they must be transformed by the SAME
    // vertex program as the terrain pass: two programs computing the same world position differ by
    // ULPs, and with a LESSEQUAL test the grass bases then shimmer. Fall back to the UI shader only
    // where the terrain itself was drawn with it (the fixed-function path).
    bool detailShaded = s_useTerrainShader && s_terrainVS && s_detailPS && s_detailPS->Valid();

    GxRsSet(GxRs_VertexShader, detailShaded ? s_terrainVS : s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, detailShaded ? s_detailPS : s_uiPixelShader);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&s_viewProjT), 4);

    uint32_t builtThisFrame = 0;

    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (auto& chunk : tile.chunks) {
            if (!chunk.valid || !chunk.detailCount) {
                continue;
            }

            float cx = (chunk.boundsMin.x + chunk.boundsMax.x) * 0.5f - s_cameraPos.x;
            float cy = (chunk.boundsMin.y + chunk.boundsMax.y) * 0.5f - s_cameraPos.y;
            float chunkRadius = CHUNK_SIZE * 0.71f;

            if (cx * cx + cy * cy > (dist + chunkRadius) * (dist + chunkRadius)) {
                // Out of range: drop the batch so memory follows the camera
                if (chunk.detailBatch) {
                    FreeDetailBatch(chunk);
                }

                continue;
            }

            if ((chunk.detailDirty || !chunk.detailBatch) && builtThisFrame < 4) {
                BuildDetailBatch(chunk);
                builtThisFrame++;
            }

            DetailBatch* b = chunk.detailBatch;

            if (!b || !b->indexCount || !BoxVisible(chunk.boundsMin, chunk.boundsMax)) {
                continue;
            }

            // Always, not just on the shaded path: BuildDetailBatch stores positions relative to
            // the chunk origin, so both vertex programs need the matrix that folds it back in.
            C44Matrix chunkT = ChunkMatrixT(chunk.origin);
            GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&chunkT), 4);

            GxPrimLockVertexPtrs(
                b->vertexCount,
                b->positions, sizeof(C3Vector),
                nullptr, 0,
                b->colors, sizeof(CImVector),
                nullptr, 0,
                b->uvs, sizeof(C2Vector),
                nullptr, 0
            );

            uint32_t ranges = static_cast<uint32_t>(b->rangeCount * density + 0.5f);

            if (!ranges && b->rangeCount) {
                ranges = 1;
            }

            for (uint32_t r = 0; r < ranges; r++) {
                const DetailBatch::Range& range = b->ranges[r];
                EGxBlend blend = range.blend == 0 ? GxBlend_Opaque : (range.blend == 1 ? GxBlend_AlphaKey : GxBlend_Alpha);
                GxRsSet(GxRs_BlendingMode, blend);
                GxRsSet(GxRs_AlphaRef, static_cast<int32_t>(CGxDevice::s_alphaRef[blend]));
                GxRsSet(GxRs_Texture0, range.texture ? TextureGetGxTex(range.texture, 0, nullptr) : nullptr);
                GxDrawLockedElements(GxPrim_Triangles, range.indexCount, b->indices + range.indexStart);
            }

            GxPrimUnlockVertexPtrs();
        }
    }

    (void)distSq;
    GxRsPop();
}

namespace {

// MCLQ (the pre-MH2O chunk liquid the reference still reads when a chunk has no MH2O data): the
// MCNK flags name the kind (0x4 river, 0x8 ocean, 0x10 magma, 0x20 slime), then min/max height,
// a 9x9 vertex grid (8 bytes each, height in the second float) and 8x8 tile flags (low nibble
// 0xF = no liquid).
void ParseLegacyLiquid(TerrainChunk& chunk, const uint8_t* mcnk, uint32_t mcnkSize) {
    if (!chunk.valid) {
        return;
    }

    uint32_t mcnkFlags = *reinterpret_cast<const uint32_t*>(mcnk + 0x00);
    uint32_t ofsLiquid = *reinterpret_cast<const uint32_t*>(mcnk + 0x60);
    uint32_t sizeLiquid = *reinterpret_cast<const uint32_t*>(mcnk + 0x64);

    if (!ofsLiquid || sizeLiquid <= 8 || ofsLiquid + sizeLiquid > mcnkSize + 8) {
        return;
    }

    int32_t liquidType;

    if (mcnkFlags & 0x10) {
        liquidType = 3; // magma
    } else if (mcnkFlags & 0x20) {
        liquidType = 4; // slime
    } else if (mcnkFlags & 0x8) {
        liquidType = 2; // ocean
    } else if (mcnkFlags & 0x4) {
        liquidType = 1; // river
    } else {
        return;
    }

    // Some files carry an MCLQ sub-chunk header; the reference's ofsLiquid points at the data
    const uint8_t* liq = mcnk + ofsLiquid;

    if (sizeLiquid < 8 + 81 * 8 + 64) {
        return;
    }

    const uint8_t* verts = liq + 8;
    const uint8_t* tiles = verts + 81 * 8;

    uint32_t covered = 0;

    for (int32_t t = 0; t < 64; t++) {
        if ((tiles[t] & 0x0F) != 0x0F) {
            covered++;
        }
    }

    if (!covered) {
        return;
    }

    // ParseLiquid may have allocated the array and then found every layer unusable, leaving a
    // live pointer with a zero count; overwriting it here would leak that block.
    if (chunk.liquids) {
        SMemFree(chunk.liquids, __FILE__, __LINE__, 0);
        chunk.liquids = nullptr;
    }

    chunk.liquids = static_cast<ChunkLiquid*>(SMemAlloc(sizeof(ChunkLiquid), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    ChunkLiquid& l = chunk.liquids[0];
    l.liquidType = liquidType;

    auto rec = g_liquidTypeDB.GetRecord(liquidType);
    l.kind = rec ? rec->m_type : (liquidType - 1);
    l.xOffset = 0; l.yOffset = 0; l.width = 8; l.height = 8;

    for (int32_t m = 0; m < 8; m++) {
        uint8_t mask = 0;

        for (int32_t c = 0; c < 8; c++) {
            if ((tiles[m * 8 + c] & 0x0F) != 0x0F) {
                mask |= static_cast<uint8_t>(1 << c);
            }
        }

        l.cellMask[m] = mask;
    }

    float posX = chunk.position[0].x;
    float posY = chunk.position[0].y;

    l.vertCount = 81;
    l.verts = static_cast<C3Vector*>(SMemAlloc(81 * sizeof(C3Vector), __FILE__, __LINE__, 0));
    l.uvs = static_cast<C2Vector*>(SMemAlloc(81 * sizeof(C2Vector), __FILE__, __LINE__, 0));
    l.indices = static_cast<uint16_t*>(SMemAlloc(covered * 6 * sizeof(uint16_t), __FILE__, __LINE__, 0));

    for (int32_t j = 0; j < 9; j++) {
        for (int32_t i = 0; i < 9; i++) {
            int32_t k = j * 9 + i;
            float h = *reinterpret_cast<const float*>(verts + k * 8 + 4);
            l.verts[k].x = posX - j * UNIT_SIZE;
            l.verts[k].y = posY - i * UNIT_SIZE;
            l.verts[k].z = h;
            l.uvs[k].x = i * 0.25f;
            l.uvs[k].y = j * 0.25f;

            if (k == 0) {
                l.boundsMin = l.boundsMax = l.verts[k];
            } else {
                const C3Vector& v = l.verts[k];
                l.boundsMin.x = v.x < l.boundsMin.x ? v.x : l.boundsMin.x;
                l.boundsMin.y = v.y < l.boundsMin.y ? v.y : l.boundsMin.y;
                l.boundsMin.z = v.z < l.boundsMin.z ? v.z : l.boundsMin.z;
                l.boundsMax.x = v.x > l.boundsMax.x ? v.x : l.boundsMax.x;
                l.boundsMax.y = v.y > l.boundsMax.y ? v.y : l.boundsMax.y;
                l.boundsMax.z = v.z > l.boundsMax.z ? v.z : l.boundsMax.z;
            }
        }
    }

    uint32_t n = 0;

    for (int32_t j = 0; j < 8; j++) {
        for (int32_t i = 0; i < 8; i++) {
            if ((tiles[j * 8 + i] & 0x0F) == 0x0F) {
                continue;
            }

            uint16_t a = static_cast<uint16_t>(j * 9 + i);
            uint16_t b = static_cast<uint16_t>(a + 1);
            uint16_t c = static_cast<uint16_t>(a + 9);
            uint16_t d = static_cast<uint16_t>(c + 1);
            l.indices[n++] = a; l.indices[n++] = c; l.indices[n++] = b;
            l.indices[n++] = b; l.indices[n++] = c; l.indices[n++] = d;
        }
    }

    l.indexCount = n;
    chunk.liquidCount = 1;
}

} // namespace

void TerrainLoad(const char* mapName, int32_t mapID) {
    TerrainUnload();
    SStrCopy(s_mapName, mapName, sizeof(s_mapName));
    s_mapID = mapID;

    if (!s_indicesBuilt) {
        BuildIndices();
    }
}

// Release the sky's private scenes (stars and the zone skybox). They live outside the world M2
// scene and were never freed, so every map change leaked one of each and left the previous zone's
// skybox model resident.
// Release the sky's own models on unload. The stars and skybox live in their own CM2Scene objects
// rather than the world scene, and as function-local statics they used to survive every map change,
// leaving the previous zone's skybox resident. The scenes themselves are kept and reused: the port
// has M2CreateScene but no matching destroy, so inventing one here would be guesswork.
void SkyRelease() {
    CloudsRelease();

    if (s_starsModel) {
        s_starsModel->DetachFromScene();
        s_starsModel->Release();
        s_starsModel = nullptr;
    }

    s_starsTried = false;

    if (s_skyboxModel) {
        s_skyboxModel->DetachFromScene();
        s_skyboxModel->Release();
        s_skyboxModel = nullptr;
    }

    s_skyboxLoaded[0] = 0;
}

void TerrainUnload() {
    SkyRelease();

    for (auto& tile : s_tiles) {
        if (tile.loaded) {
            FreeTile(tile);
        }
    }

    // Per-map state that outlives the tiles. FreeTile erases each tile's own unique ids, but clear
    // the set outright so a half-freed tile cannot leave a stale id behind and make the next map
    // silently skip a WMO it thinks is already placed.
    s_loadedUnique.clear();
    s_cameraLiquidKind = -1;
    CWorld::SetCameraUnderLiquid(false);
}

void TerrainUpdate(const C3Vector& cameraPos) {
    s_cameraPos = cameraPos;

    if (!s_mapName[0]) {
        return;
    }

    // Liquid under the camera (reference CWorldScene FUN_00790920, run first in CMap::Render): it
    // switches the light to the underwater parameter set and suppresses the sky.
    {
        float surfaceZ;
        s_cameraLiquidType = -1;
        s_cameraLiquidKind = LiquidAt(cameraPos, surfaceZ);
        CWorld::SetCameraUnderLiquid(s_cameraLiquidKind >= 0);
    }

    // Recompute the outdoor light for the current time of day; models and the sky read it directly,
    // and terrain is re-baked from its stored inputs when the light shifts enough (day/night cycle).
    CWorld::UpdateOutdoorLight();

    int32_t centerCol = static_cast<int32_t>(32.0f - cameraPos.y / TILE_SIZE);
    int32_t centerRow = static_cast<int32_t>(32.0f - cameraPos.x / TILE_SIZE);

    // Stream enough tiles to fill the view distance, like the reference: a tile is 533 yds, so a
    // 727 yd far clip needs the camera's tile plus two rings to guarantee the ground reaches the
    // clip plane in every direction (otherwise terrain ends in a hard, unfogged edge near a tile
    // boundary). A small far clip needs fewer rings; the pool caps the radius at MAX_TILE_RADIUS.
    int32_t radius = static_cast<int32_t>(ceilf(CWorld::GetHorizonFarClip() / TILE_SIZE));

    if (radius < 1) {
        radius = 1;
    } else if (radius > MAX_TILE_RADIUS) {
        radius = MAX_TILE_RADIUS;
    }

    // Free tiles that have moved outside the load window so their slots become available for the new
    // tiles the camera reveals as it crosses tile boundaries. Without this, once every slot is full
    // the loader below can never find a free slot and terrain streaming stalls, leaving holes.
    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        int32_t dCol = tile.x - centerCol;
        int32_t dRow = tile.y - centerRow;

        if (dCol < -radius || dCol > radius || dRow < -radius || dRow > radius) {
            FreeTile(tile);
        }
    }

    for (int32_t dy = -radius; dy <= radius; dy++) {
        for (int32_t dx = -radius; dx <= radius; dx++) {
            int32_t tileX = centerCol + dx;
            int32_t tileY = centerRow + dy;

            if (tileX < 0 || tileX > 63 || tileY < 0 || tileY > 63) {
                continue;
            }

            bool found = false;

            for (auto& tile : s_tiles) {
                if (tile.loaded && tile.x == tileX && tile.y == tileY) {
                    found = true;
                    break;
                }
            }

            if (found) {
                continue;
            }

            for (auto& tile : s_tiles) {
                if (!tile.loaded) {
                    LoadTile(tile, tileX, tileY);
                    break;
                }
            }
        }
    }

    // Re-bake resident terrain when the outdoor light has shifted enough (day/night progression).
    // Newly loaded tiles above already baked at the current light in ParseChunk.
    const C3Vector& amb = CWorld::GetOutdoorAmbient();
    const C3Vector& dif = CWorld::GetOutdoorDiffuse();
    float sig = amb.x + amb.y + amb.z + dif.x + dif.y + dif.z;

    static float s_lastBakeSig = -1000.0f;

    // A fine threshold keeps terrain lighting close to the every-frame models and sky; the rebake is
    // spread over frames so a small threshold no longer risks a hitch.
    if (sig < s_lastBakeSig - 0.01f || sig > s_lastBakeSig + 0.01f) {
        s_lastBakeSig = sig;

        for (auto& tile : s_tiles) {
            if (tile.loaded) {
                tile.needsRebake = true;
            }
        }
    }

    // Relight at most a couple of dirty tiles per frame so day/night transitions never hitch
    int32_t rebakeBudget = 2;

    for (auto& tile : s_tiles) {
        if (!tile.loaded || !tile.needsRebake) {
            continue;
        }

        if (rebakeBudget-- <= 0) {
            break;
        }

        tile.needsRebake = false;

        for (auto& chunk : tile.chunks) {
            if (chunk.valid) {
                RebakeChunkColors(chunk);
            }
        }

        // Re-light exterior WMO groups too; interior groups keep their static torch-lit MOCV
        if (tile.wmos) {
                for (uint32_t wi = 0; wi < tile.wmoCount; wi++) {
                    WmoInstance& w = tile.wmos[wi];

                    for (uint32_t gi = 0; gi < w.groupCount; gi++) {
                        WmoGroup& grp = w.groups[gi];

                        if (grp.interior || !grp.ndotl) {
                            continue;
                        }

                        for (uint32_t v = 0; v < grp.vertexCount; v++) {
                            float nd = grp.ndotl[v] / 255.0f;
                            float addR = grp.mocvAdd ? grp.mocvAdd[v].r : 0.0f;
                            float addG = grp.mocvAdd ? grp.mocvAdd[v].g : 0.0f;
                            float addB = grp.mocvAdd ? grp.mocvAdd[v].b : 0.0f;
                            float fr = (amb.x + dif.x * nd) * 255.0f + addR;
                            float fg = (amb.y + dif.y * nd) * 255.0f + addG;
                            float fb = (amb.z + dif.z * nd) * 255.0f + addB;
                            grp.colors[v].r = static_cast<uint8_t>(fr > 255.0f ? 255.0f : fr);
                            grp.colors[v].g = static_cast<uint8_t>(fg > 255.0f ? 255.0f : fg);
                            grp.colors[v].b = static_cast<uint8_t>(fb > 255.0f ? 255.0f : fb);
                            grp.colors[v].a = 0xFF;
                        }
                    }
                }
            }
        }
    }

// The per-frame view half of the terrain pass: the frustum every other system culls against, the
// fog render states and doodad visibility. Split out of TerrainRender so the frame can establish
// visibility before the shadow map renders its casters, which has to happen before any drawing.
// Rebuild every loaded liquid surface's vertex colours from the current light.
//
// Colour and alpha are baked per vertex at load, so without this a water surface keeps the light
// parameters of whatever zone it happened to load under -- crossing a zone boundary or waiting for
// dusk would leave it stale. The reference has the same problem and solves it by rebuilding its
// gradient table; this rebuilds the baked vertices, which is the same idea against a different
// layout. Only runs when the light parameter set actually changes.
void TerrainRebakeLiquidColors() {
    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (auto& chunk : tile.chunks) {
            for (uint32_t l = 0; l < chunk.liquidCount; l++) {
                ChunkLiquid& liq = chunk.liquids[l];

                if (!liq.colors || !liq.depthRamp || liq.kind == 2 || liq.kind == 3) {
                    continue;
                }

                const C3Vector& shallowC = CWorld::GetLiquidShallow(liq.kind == 1);
                const C3Vector& deepC = CWorld::GetLiquidDeep(liq.kind == 1);
                float shallowA = CWorld::GetLiquidAlpha(liq.kind == 1, 0);
                float deepA = CWorld::GetLiquidAlpha(liq.kind == 1, 1);

                for (uint32_t k = 0; k < liq.vertCount; k++) {
                    float t = liq.depthRamp[k] / 255.0f;

                    liq.colors[k].r = static_cast<uint8_t>((shallowC.x + (deepC.x - shallowC.x) * t) * 255.0f);
                    liq.colors[k].g = static_cast<uint8_t>((shallowC.y + (deepC.y - shallowC.y) * t) * 255.0f);
                    liq.colors[k].b = static_cast<uint8_t>((shallowC.z + (deepC.z - shallowC.z) * t) * 255.0f);
                    liq.colors[k].a = static_cast<uint8_t>((shallowA + (deepA - shallowA) * t) * 255.0f);
                }
            }
        }
    }
}

void TerrainUpdateView() {
    if (!s_mapName[0]) {
        return;
    }

    // Water is baked per vertex, so it has to follow the light rather than the load. Rebake only
    // when the parameter set actually changes -- walking every loaded liquid every frame would be
    // pure waste, and the colours only move when the light does.
    static int32_t s_bakedParams = -1;

    if (CWorld::GetOutdoorParamsID() != s_bakedParams) {
        s_bakedParams = CWorld::GetOutdoorParamsID();
        TerrainRebakeLiquidColors();
    }

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

    // Frustum-cull the doodads: only those in view animate and draw, matching the reference and
    // sparing the scene from processing thousands of out-of-view props each frame.
    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (uint32_t i = 0; i < tile.doodadCount; i++) {
            C3Vector c = DoodadCullCenter(tile.doodads[i]);
            float r = DoodadCullRadius(tile.doodads[i], tile.doodadScale[i]);
            bool vis = SphereVisible(c, r);

            tile.doodads[i]->SetVisible(vis ? 1 : 0);
            tile.doodads[i]->SetAnimating(vis ? 1 : 0);
        }

        if (tile.wmos) {
            for (uint32_t wi = 0; wi < tile.wmoCount; wi++) {
                WmoInstance& w = tile.wmos[wi];

                for (uint32_t i = 0; i < w.doodadCount; i++) {
                    C3Vector c = DoodadCullCenter(w.doodads[i]);
                    float r = DoodadCullRadius(w.doodads[i], w.doodadScale[i]);
                    bool vis = SphereVisible(c, r);

                    w.doodads[i]->SetVisible(vis ? 1 : 0);
                    w.doodads[i]->SetAnimating(vis ? 1 : 0);
                }
            }
        }
    }

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

    const C44Matrix& viewProjT = s_viewProjT;

    if (haveShaded) {
        RenderShaded(viewProjT);
    } else {
        RenderFallback(viewProjT);
    }

    WmoUpdateVisibility(s_cameraPos);
    RenderWmos(viewProjT);

    // Opaque liquids (magma, slime) belong to CMap::Render like the terrain and buildings
    LiquidRender(0);

    GxRsPop();
}


// ------------------------------------------------------------------------------------------------
// Liquids (MH2O). The reference builds Liquid::CInstance objects per chunk layer and draws them in
// two sorted buckets (FUN_008a2240(cam, 0) opaque inside CMap::Render, (cam, 1) transparent in the
// world frame's transparent block) with the animated LiquidType surface textures.
// ------------------------------------------------------------------------------------------------

// Animated surface textures per LiquidType ("XTextures\\river\\lake_a.%d.blp", frames from 1)
struct LiquidTextures {
    int32_t liquidType = -1;
    HTEXTURE frames[32] = { nullptr };
    uint32_t frameCount = 0;
};

const int32_t MAX_LIQUID_TEXTURE_SETS = 16;
LiquidTextures s_liquidTextures[MAX_LIQUID_TEXTURE_SETS];

LiquidTextures* GetLiquidTextures(int32_t liquidType) {
    for (int32_t i = 0; i < MAX_LIQUID_TEXTURE_SETS; i++) {
        if (s_liquidTextures[i].liquidType == liquidType) {
            return &s_liquidTextures[i];
        }
    }

    for (int32_t i = 0; i < MAX_LIQUID_TEXTURE_SETS; i++) {
        LiquidTextures& set = s_liquidTextures[i];

        if (set.liquidType != -1) {
            continue;
        }

        set.liquidType = liquidType;
        auto rec = g_liquidTypeDB.GetRecord(liquidType);

        if (rec && rec->m_texture[0] && *rec->m_texture[0]) {
            // Stop at the first frame that does not exist. These sets have 30 frames
            // ("XTextures\river\lake_a.1.blp" .. ".30.blp"); probing past the end and relying on
            // TextureCreate to fail does not work, because a missing file still yields a
            // placeholder texture -- which showed up as the water flashing green once per cycle.
            for (uint32_t f = 1; f <= 32; f++) {
                char path[260];
                SStrPrintf(path, sizeof(path), rec->m_texture[0], f);

                if (!SFile::FileExists(path)) {
                    break;
                }

                CStatus status;
                HTEXTURE tex = TextureCreate(path, CGxTexFlags(GxTex_LinearMipLinear, 1, 1, 0, 0, 0, 1), &status, 0);

                if (!tex) {
                    break;
                }

                set.frames[set.frameCount++] = tex;
            }
        }

        return &set;
    }

    return nullptr;
}

// One entry per liquid vertex. A terrain MH2O layer is at most 9x9 = 81, but a WMO MLIQ grid runs
// to (xtiles+1)*(ytiles+1) and is much larger, so this grows to the biggest surface drawn; a fixed
// 81 read off the end of the array for any WMO liquid.
std::vector<CImVector> s_liquidColor;
uint8_t s_liquidAlpha = 0;

void LiquidRender(int32_t bucket) {
    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    bool opaque = bucket == 0;

    // Gather the visible layers of this bucket; the transparent ones sort farthest first
    struct LiquidRef { const ChunkLiquid* liq; float dist; };
    static std::vector<LiquidRef> s_refs;
    s_refs.clear();

    auto consider = [&](const ChunkLiquid& liq) {
        bool isOpaque = liq.kind == 2 || liq.kind == 3;

        if (isOpaque != opaque || !liq.indexCount || !BoxVisible(liq.boundsMin, liq.boundsMax)) {
            return;
        }

        float cx = (liq.boundsMin.x + liq.boundsMax.x) * 0.5f - s_cameraPos.x;
        float cy = (liq.boundsMin.y + liq.boundsMax.y) * 0.5f - s_cameraPos.y;
        float cz = (liq.boundsMin.z + liq.boundsMax.z) * 0.5f - s_cameraPos.z;
        s_refs.push_back({ &liq, cx * cx + cy * cy + cz * cz });
    };

    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (auto& chunk : tile.chunks) {
            for (uint32_t l = 0; l < chunk.liquidCount; l++) {
                consider(chunk.liquids[l]);
            }
        }

        // WMO liquids follow their group's portal visibility from this frame's walk
        for (uint32_t wi = 0; wi < tile.wmoCount; wi++) {
            const WmoInstance& w = tile.wmos[wi];

            for (uint32_t l = 0; l < w.liquidCount; l++) {
                const ChunkLiquid& liq = w.liquids[l];

                if (liq.group >= 0 && static_cast<uint32_t>(liq.group) < w.groupCount && w.groups[liq.group].visFrame != s_visFrame) {
                    continue;
                }

                consider(liq);
            }
        }
    }

    if (s_refs.empty()) {
        return;
    }

    if (!opaque) {
        std::sort(s_refs.begin(), s_refs.end(), [](const LiquidRef& a, const LiquidRef& b) { return a.dist > b.dist; });
    }

    // Surface animation: the frame sets cycle at ~20 fps off the world clock
    uint32_t now = CWorld::GetM2Scene() ? CWorld::GetM2Scene()->m_time : 0;

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_DepthWrite, opaque ? 1 : 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, opaque ? GxBlend_Opaque : GxBlend_Alpha);
    GxRsSet(GxRs_AlphaRef, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, s_fogActive ? 1 : 0);
    GxRsSet(GxRs_VertexShader, s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, s_uiPixelShader);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&s_viewProjT), 4);

    int32_t lastType = -1;

    for (const LiquidRef& ref : s_refs) {
        const ChunkLiquid& liq = *ref.liq;

        if (liq.liquidType != lastType) {
            lastType = liq.liquidType;
            LiquidTextures* set = GetLiquidTextures(liq.liquidType);
            HTEXTURE tex = (set && set->frameCount) ? set->frames[(now / 50) % set->frameCount] : nullptr;
            GxRsSet(GxRs_Texture0, tex ? TextureGetGxTex(tex, 0, nullptr) : nullptr);

            // Water and ocean read as tinted, translucent surfaces; magma and slime are solid
            uint8_t alpha = opaque ? 0xFF : (liq.kind == 1 ? 0xD8 : 0xC0);
            s_liquidAlpha = alpha;
        }

        // Tint from the light data rather than drawing white. LightIntBand bands 14/15 (river) and
        // 16/17 (ocean) are the shallow and deep endpoints -- confirmed in the reference's
        // FUN_008a2bf0, which interpolates the pair across a 512-entry gradient indexed by depth.
        // Only the shallow endpoint is used here: the depth gradient itself is still to port, and
        // magma and slime are not light-driven at all, so they keep their own colour.
        CImVector tint = { 0xFF, 0xFF, 0xFF, s_liquidAlpha };

        if (liq.kind == 0 || liq.kind == 1) {
            const C3Vector& shallow = CWorld::GetLiquidShallow(liq.kind == 1);
            tint.r = static_cast<uint8_t>(shallow.x * 255.0f);
            tint.g = static_cast<uint8_t>(shallow.y * 255.0f);
            tint.b = static_cast<uint8_t>(shallow.z * 255.0f);
        }

        if (s_liquidColor.size() < liq.vertCount
            || (liq.vertCount && (s_liquidColor[0].a != tint.a || s_liquidColor[0].r != tint.r))) {
            s_liquidColor.assign(liq.vertCount > s_liquidColor.size() ? liq.vertCount : s_liquidColor.size(), tint);
        }

        // Depth-shaded surfaces bring their own per-vertex alpha (already scaled by the kind's
        // base opacity at load); the rest use the flat fill.
        const CImVector* colorStream = liq.colors ? liq.colors : s_liquidColor.data();

        GxPrimLockVertexPtrs(
            liq.vertCount,
            liq.verts, sizeof(C3Vector),
            nullptr, 0,
            colorStream, sizeof(CImVector),
            nullptr, 0,
            liq.uvs, sizeof(C2Vector),
            nullptr, 0
        );
        GxDrawLockedElements(GxPrim_Triangles, liq.indexCount, liq.indices);
        GxPrimUnlockVertexPtrs();
    }

    GxRsPop();
}

void UnderwaterOverlayRender() {
    if (s_cameraLiquidKind < 0) {
        return;
    }

    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    // The surface texture of the liquid the camera is in, scrolled slowly for the caustic look.
    // LiquidAt recorded the exact type when it decided the camera was submerged, so there is no
    // need to walk every tile and chunk again looking for a surface of the same kind.
    LiquidTextures* set = s_cameraLiquidType >= 0 ? GetLiquidTextures(s_cameraLiquidType) : nullptr;
    uint32_t now = CWorld::GetM2Scene() ? CWorld::GetM2Scene()->m_time : 0;
    HTEXTURE tex = (set && set->frameCount) ? set->frames[(now / 50) % set->frameCount] : nullptr;

    // A quad one yard in front of the eye, wide enough to cover any field of view
    const C3Vector& fwd = CWorld::GetCameraDir();
    C3Vector right = { fwd.y, -fwd.x, 0.0f };
    float rl = sqrtf(right.x * right.x + right.y * right.y);
    if (rl < 1e-4f) { right = { 1.0f, 0.0f, 0.0f }; } else { right.x /= rl; right.y /= rl; }
    C3Vector up = { right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y - right.y * fwd.x };
    C3Vector c = { s_cameraPos.x + fwd.x, s_cameraPos.y + fwd.y, s_cameraPos.z + fwd.z };
    const float H = 3.0f;

    C3Vector pos[4] = {
        { c.x - right.x * H + up.x * H, c.y - right.y * H + up.y * H, c.z - right.z * H + up.z * H },
        { c.x + right.x * H + up.x * H, c.y + right.y * H + up.y * H, c.z + right.z * H + up.z * H },
        { c.x + right.x * H - up.x * H, c.y + right.y * H - up.y * H, c.z + right.z * H - up.z * H },
        { c.x - right.x * H - up.x * H, c.y - right.y * H - up.y * H, c.z - right.z * H - up.z * H },
    };
    float scroll = static_cast<float>(now % 20000) / 20000.0f;
    C2Vector uv[4] = { { scroll, scroll }, { scroll + 2.0f, scroll }, { scroll + 2.0f, scroll + 1.5f }, { scroll, scroll + 1.5f } };

    const C3Vector& fog = CWorld::GetFogColor();
    CImVector col[4];
    for (int32_t i = 0; i < 4; i++) {
        col[i].b = static_cast<uint8_t>(fog.z * 255.0f);
        col[i].g = static_cast<uint8_t>(fog.y * 255.0f);
        col[i].r = static_cast<uint8_t>(fog.x * 255.0f);
        col[i].a = 0x50;
    }
    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_AlphaRef, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_VertexShader, s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, s_uiPixelShader);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&s_viewProjT), 4);
    GxRsSet(GxRs_Texture0, tex ? TextureGetGxTex(tex, 0, nullptr) : (s_skyWhite ? TextureGetGxTex(s_skyWhite, 0, nullptr) : nullptr));
    GxPrimLockVertexPtrs(4, pos, sizeof(C3Vector), nullptr, 0, col, sizeof(CImVector), nullptr, 0, uv, sizeof(C2Vector), nullptr, 0);
    GxDrawLockedElements(GxPrim_Triangles, 6, idx);
    GxPrimUnlockVertexPtrs();
    GxRsPop();
}


// ------------------------------------------------------------------------------------------------
// Blob shadows (the reference's CWorldScene FUN_00793980: per scene entity with a model, project
// Textures\ShadowBlob.blp onto the ground through FUN_007e4480). The chunk mesh under the entity
// is redrawn with the blob texture and planar texture coordinates centred on the entity, so the
// decal follows the terrain exactly; depth is tested less-equal against the identical geometry.
// ------------------------------------------------------------------------------------------------

HTEXTURE s_shadowBlob = nullptr;
bool s_shadowBlobTried = false;
bool s_blobActive = false;

void BlobShadowsBegin() {
    s_blobActive = false;

    if (!s_shadowBlobTried) {
        s_shadowBlobTried = true;
        CStatus status;
        // Clamp both axes: outside the blob the texture edge is transparent, so a chunk that only
        // partly overlaps the shadow footprint stays untouched beyond it.
        s_shadowBlob = TextureCreate("Textures\\ShadowBlob.blp", CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 0);
    }

    // The decal pass only works where the base terrain pass used the terrain shader: it re-draws
    // the same triangles through the SAME vertex program and constants so the interpolated depth
    // is bit-identical, then selects the visible surface with a depth-EQUAL test. That is how the
    // reference keeps coplanar decals from fighting (FUN_007e4480 sets GxRs_DepthFunc = 1);
    // it does not use depth bias -- which is just as well, since CGxDeviceD3d ignores
    // GxRs_PolygonOffset entirely. On the fixed-function fallback path the base pass uses the UI
    // shader and its layers are drawn multiple times, so there is no single depth to match; skip
    // shadows there rather than draw fighting ones.
    if (!s_shadowBlob || !s_useTerrainShader || !s_terrainVS || !s_blobDecalPS || !s_blobDecalPS->Valid()) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 1); // EQUAL: CGxDeviceD3d::s_cmpFunc[1] == D3DCMP_EQUAL
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    // Modulate, like the reference: the decal multiplies the receiver instead of lerping it toward
    // black, so a shadow reads the same over bright and dark ground.
    GxRsSet(GxRs_BlendingMode, GxBlend_Mod);
    GxRsSet(GxRs_AlphaRef, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0); // the receiver already carries the frame's fog from the base pass
    GxRsSet(GxRs_VertexShader, s_terrainVS);
    GxRsSet(GxRs_PixelShader, s_blobDecalPS);
    GxRsSet(GxRs_Texture0, TextureGetGxTex(s_shadowBlob, 0, nullptr));

    s_blobActive = true;
}

// How much light a fully shadowed spot loses.
//
// The opacity was hard-coded to 1.0, which under MOD blending multiplies the receiver by zero at the
// centre of the blob -- a solid black disc, which is what it looked like on screen. A shadow does not
// remove all light: it removes the DIFFUSE contribution and leaves the AMBIENT, so the right
// multiplier at full coverage is ambient / (ambient + diffuse).
//
// Derived from the outdoor light rather than picked, so it tracks time of day: shadows are strong at
// midday when the sun dominates and weak at dusk when ambient does. Both terms are verified against
// the reference by the memcompare harness.
//
// NOT the reference's own calculation, but the reason recorded here was wrong: the ShadowAdd and
// ShadowMod ramps are now decoded (2026-09-16, parity-shadows.md), and they are NOT where the
// reference gets this value. They are 64x8 textures bound to stage 1 as a fade along the projection
// axis -- a separate effect frozen does not implement at all. Where the reference's shadow strength
// comes from is still unread, so this stays a principled stand-in.
//
// Note the sense: this returns how much light is REMOVED. The shader emits 1 - coverage, so the
// multiplier that actually reaches the framebuffer at full coverage is ambient / (ambient + diffuse),
// which is the quantity the paragraph above describes.
float BlobShadowStrength() {
    const C3Vector& ambient = CWorld::GetOutdoorAmbient();
    const C3Vector& diffuse = CWorld::GetOutdoorDiffuse();

    float ambientLuma = ambient.x * 0.3f + ambient.y * 0.59f + ambient.z * 0.11f;
    float diffuseLuma = diffuse.x * 0.3f + diffuse.y * 0.59f + diffuse.z * 0.11f;
    float total = ambientLuma + diffuseLuma;

    if (total <= 0.0f) {
        return 0.5f;
    }

    float strength = diffuseLuma / total;

    // Never a pure black hole, never invisible.
    return strength < 0.15f ? 0.15f : (strength > 0.85f ? 0.85f : strength);
}

void BlobShadowDraw(const C3Vector& pos, float radius) {
    if (!s_blobActive || radius <= 0.0f) {
        return;
    }

    // Only the tiles the footprint can touch (the unit's own and, near an edge, its neighbours)
    int32_t colMin = static_cast<int32_t>(32.0f - (pos.y + radius) / TILE_SIZE);
    int32_t colMax = static_cast<int32_t>(32.0f - (pos.y - radius) / TILE_SIZE);
    int32_t rowMin = static_cast<int32_t>(32.0f - (pos.x + radius) / TILE_SIZE);
    int32_t rowMax = static_cast<int32_t>(32.0f - (pos.x - radius) / TILE_SIZE);
    float inv = 0.5f / radius;

    for (auto& tile : s_tiles) {
        if (!tile.loaded || tile.x < colMin || tile.x > colMax || tile.y < rowMin || tile.y > rowMax) {
            continue;
        }

        for (auto& chunk : tile.chunks) {
            if (!chunk.valid) {
                continue;
            }

            // Footprint box against the chunk box; the vertical extent keeps a decal from being
            // painted onto ground far above or below the entity (bridges, cliffs).
            if (chunk.boundsMax.x < pos.x - radius || chunk.boundsMin.x > pos.x + radius
                || chunk.boundsMax.y < pos.y - radius || chunk.boundsMin.y > pos.y + radius
                || chunk.boundsMax.z < pos.z - 4.0f * radius - 2.0f || chunk.boundsMin.z > pos.z + 2.0f * radius + 2.0f) {
                continue;
            }

            // Same local vertices and the same per-chunk matrix as the base pass: that is what
            // makes the depths bit-identical so the EQUAL test selects the visible surface.
            C44Matrix chunkT = ChunkMatrixT(chunk.origin);
            GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&chunkT), 4);

            // The blob's placement rides in a pixel-shader constant, not a vertex stream, since the
            // streams have to stay byte-identical to the base pass. The shader recovers the vertex
            // XY from the terrain VS output (oT1 = pos.xy * 0.2), which is now chunk-local, so the
            // centre is passed relative to the chunk origin too.
            float decal[4] = { pos.x - chunk.origin.x, pos.y - chunk.origin.y, inv, BlobShadowStrength() };
            GxShaderConstantsSet(GxSh_Pixel, 0, decal, 1);

            GxPrimLockVertexPtrs(
                145,
                chunk.localPos, sizeof(C3Vector),
                nullptr, 0,
                chunk.color, sizeof(CImVector),
                nullptr, 0,
                chunk.texcoord, sizeof(C2Vector),
                nullptr, 0
            );

            if (chunk.holes) {
                uint32_t n = BuildHoleIndices(chunk.holes, s_holeIndices);
                GxDrawLockedElements(GxPrim_Triangles, n, s_holeIndices);
            } else {
                GxDrawLockedElements(GxPrim_Triangles, 768, s_indices);
            }

            GxPrimUnlockVertexPtrs();
        }
    }
}

// Blob shadows on WMO floors. Same technique as the terrain receivers: re-draw the receiver's own
// triangles through the same vertex program and per-instance matrix the base pass used, with a
// depth-EQUAL test, and derive the blob coordinate from the vertex XY (instance-local here).
// Scratch index list for the WMO shadow receiver gather, reused every call.
std::vector<uint16_t> s_shadowIndices;

// Bucket a group's up-facing triangles into a uniform XY grid, once.
//
// Only up-facing triangles go in: a blob shadow belongs on floors, not on the walls beside them, and
// filtering here means the per-frame gather never looks at a wall triangle at all.
void BuildWmoShadowGrid(WmoGroup& grp) {
    if (grp.shadowGridBuilt) {
        return;
    }

    grp.shadowGridBuilt = true;

    if (!grp.indexCount || !grp.positions) {
        return;
    }

    // Anchor the grid on the vertices it is about to bin, not on grp.bounds.
    //
    // bounds stayed in world space when the group's vertices were rebased onto the instance
    // origin, so anchoring on it mixed frames by that origin -- thousands of yards for most
    // buildings. Both the binning and the lookup use local coordinates, so they agreed with each
    // other and the results stayed correct; every cell index simply came out far negative and
    // clamped to the corner. The whole group landed in one cell and every lookup read it, which
    // is the exhaustive per-caster scan this grid exists to avoid.
    float minX = grp.positions[0].x;
    float minY = grp.positions[0].y;
    float maxX = minX;
    float maxY = minY;

    for (uint32_t v = 1; v < grp.vertexCount; v++) {
        const C3Vector& p = grp.positions[v];
        minX = p.x < minX ? p.x : minX;
        minY = p.y < minY ? p.y : minY;
        maxX = p.x > maxX ? p.x : maxX;
        maxY = p.y > maxY ? p.y : maxY;
    }

    float spanX = maxX - minX;
    float spanY = maxY - minY;

    if (spanX <= 0.0f || spanY <= 0.0f) {
        return;
    }

    // Cells a few yards across: small enough that a blob touches only a handful, large enough that
    // the grid stays small for a building-sized group.
    const float CELL = 8.0f;
    const int32_t MAX_CELLS = 128;

    grp.shadowCellSize = CELL;
    grp.shadowGridMinX = minX;
    grp.shadowGridMinY = minY;
    grp.shadowCellsX = static_cast<int32_t>(spanX / CELL) + 1;
    grp.shadowCellsY = static_cast<int32_t>(spanY / CELL) + 1;

    // A pathologically large group would otherwise allocate a huge grid; widen the cells instead.
    while (grp.shadowCellsX > MAX_CELLS || grp.shadowCellsY > MAX_CELLS) {
        grp.shadowCellSize *= 2.0f;
        grp.shadowCellsX = static_cast<int32_t>(spanX / grp.shadowCellSize) + 1;
        grp.shadowCellsY = static_cast<int32_t>(spanY / grp.shadowCellSize) + 1;
    }

    grp.shadowGrid.clear();
    grp.shadowGrid.resize(static_cast<size_t>(grp.shadowCellsX) * grp.shadowCellsY);

    for (uint32_t i = 0; i + 2 < grp.indexCount; i += 3) {
        uint16_t a = grp.indices[i];
        uint16_t b = grp.indices[i + 1];
        uint16_t c = grp.indices[i + 2];

        if (a >= grp.vertexCount || b >= grp.vertexCount || c >= grp.vertexCount) {
            continue;
        }

        const C3Vector& pa = grp.positions[a];
        const C3Vector& pb = grp.positions[b];
        const C3Vector& pc = grp.positions[c];

        float ux = (pb.x - pa.x) * (pc.y - pa.y) - (pc.x - pa.x) * (pb.y - pa.y);

        if (ux <= 0.0f) {
            continue;
        }

        float minX = pa.x < pb.x ? (pa.x < pc.x ? pa.x : pc.x) : (pb.x < pc.x ? pb.x : pc.x);
        float maxX = pa.x > pb.x ? (pa.x > pc.x ? pa.x : pc.x) : (pb.x > pc.x ? pb.x : pc.x);
        float minY = pa.y < pb.y ? (pa.y < pc.y ? pa.y : pc.y) : (pb.y < pc.y ? pb.y : pc.y);
        float maxY = pa.y > pb.y ? (pa.y > pc.y ? pa.y : pc.y) : (pb.y > pc.y ? pb.y : pc.y);

        int32_t c0 = static_cast<int32_t>((minX - grp.shadowGridMinX) / grp.shadowCellSize);
        int32_t c1 = static_cast<int32_t>((maxX - grp.shadowGridMinX) / grp.shadowCellSize);
        int32_t r0 = static_cast<int32_t>((minY - grp.shadowGridMinY) / grp.shadowCellSize);
        int32_t r1 = static_cast<int32_t>((maxY - grp.shadowGridMinY) / grp.shadowCellSize);

        c0 = c0 < 0 ? 0 : c0;
        r0 = r0 < 0 ? 0 : r0;
        c1 = c1 >= grp.shadowCellsX ? grp.shadowCellsX - 1 : c1;
        r1 = r1 >= grp.shadowCellsY ? grp.shadowCellsY - 1 : r1;

        for (int32_t cy = r0; cy <= r1; cy++) {
            for (int32_t cx = c0; cx <= c1; cx++) {
                grp.shadowGrid[static_cast<size_t>(cy) * grp.shadowCellsX + cx].push_back(i);
            }
        }
    }
}

void BlobShadowDrawWmo(const C3Vector& pos, float radius) {
    if (!s_blobActive || radius <= 0.0f) {
        return;
    }

    float inv = 0.5f / radius;

    for (auto& tile : s_tiles) {
        if (!tile.loaded || !tile.wmos) {
            continue;
        }

        for (uint32_t wi = 0; wi < tile.wmoCount; wi++) {
            WmoInstance& w = tile.wmos[wi];

            if (w.hasBounds && (w.bboxMax.x < pos.x - radius || w.bboxMin.x > pos.x + radius
                || w.bboxMax.y < pos.y - radius || w.bboxMin.y > pos.y + radius)) {
                continue;
            }

            C44Matrix instanceT = ChunkMatrixT(w.origin);
            bool matrixSet = false;

            for (uint32_t gi = 0; gi < w.groupCount; gi++) {
                WmoGroup& grp = w.groups[gi];

                if (!grp.vertexCount || !grp.batchCount || grp.visFrame != s_visFrame) {
                    continue;
                }

                // Only the group the caster is standing in or above, and only near its floor
                if (grp.boundsMax.x < pos.x - radius || grp.boundsMin.x > pos.x + radius
                    || grp.boundsMax.y < pos.y - radius || grp.boundsMin.y > pos.y + radius
                    || grp.boundsMax.z < pos.z - 4.0f * radius - 2.0f || grp.boundsMin.z > pos.z + 2.0f * radius + 2.0f) {
                    continue;
                }

                // Gather only the triangles under the caster instead of re-drawing the whole group.
                // A WMO group can be an entire building floor -- tens of thousands of triangles --
                // and redrawing that once per caster would cost more than the rest of the frame.
                // The reference does the same thing (a receiver gather with a CPU cull) rather than
                // re-submitting the receiver's own index buffer.
                float lx = pos.x - w.origin.x;
                float ly = pos.y - w.origin.y;

                BuildWmoShadowGrid(grp);

                s_shadowIndices.clear();

                // Only the grid cells the footprint touches. That is the whole point of the grid:
                // the previous form walked grp.indexCount for every caster, every frame.
                if (grp.shadowCellsX > 0 && grp.shadowCellsY > 0) {
                    int32_t c0 = static_cast<int32_t>((lx - radius - grp.shadowGridMinX) / grp.shadowCellSize);
                    int32_t c1 = static_cast<int32_t>((lx + radius - grp.shadowGridMinX) / grp.shadowCellSize);
                    int32_t r0 = static_cast<int32_t>((ly - radius - grp.shadowGridMinY) / grp.shadowCellSize);
                    int32_t r1 = static_cast<int32_t>((ly + radius - grp.shadowGridMinY) / grp.shadowCellSize);

                    c0 = c0 < 0 ? 0 : c0;
                    r0 = r0 < 0 ? 0 : r0;
                    c1 = c1 >= grp.shadowCellsX ? grp.shadowCellsX - 1 : c1;
                    r1 = r1 >= grp.shadowCellsY ? grp.shadowCellsY - 1 : r1;

                    for (int32_t cy = r0; cy <= r1; cy++) {
                        for (int32_t cx = c0; cx <= c1; cx++) {
                            for (uint32_t i : grp.shadowGrid[cy * grp.shadowCellsX + cx]) {
                                uint16_t a = grp.indices[i];
                                uint16_t b = grp.indices[i + 1];
                                uint16_t c = grp.indices[i + 2];

                                const C3Vector& pa = grp.positions[a];
                                const C3Vector& pb = grp.positions[b];
                                const C3Vector& pc = grp.positions[c];

                                float minX = pa.x < pb.x ? (pa.x < pc.x ? pa.x : pc.x) : (pb.x < pc.x ? pb.x : pc.x);
                                float maxX = pa.x > pb.x ? (pa.x > pc.x ? pa.x : pc.x) : (pb.x > pc.x ? pb.x : pc.x);
                                float minY = pa.y < pb.y ? (pa.y < pc.y ? pa.y : pc.y) : (pb.y < pc.y ? pb.y : pc.y);
                                float maxY = pa.y > pb.y ? (pa.y > pc.y ? pa.y : pc.y) : (pb.y > pc.y ? pb.y : pc.y);

                                if (maxX < lx - radius || minX > lx + radius
                                    || maxY < ly - radius || minY > ly + radius) {
                                    continue;
                                }

                                s_shadowIndices.push_back(a);
                                s_shadowIndices.push_back(b);
                                s_shadowIndices.push_back(c);
                            }
                        }
                    }
                }

                if (s_shadowIndices.empty()) {
                    continue;
                }

                if (!matrixSet) {
                    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&instanceT), 4);
                    matrixSet = true;
                }

                float decal[4] = { lx, ly, inv, 1.0f };
                GxShaderConstantsSet(GxSh_Pixel, 0, decal, 1);

                GxPrimLockVertexPtrs(
                    grp.vertexCount,
                    grp.positions, sizeof(C3Vector),
                    nullptr, 0,
                    grp.colors, sizeof(CImVector),
                    nullptr, 0,
                    grp.texcoords, sizeof(C2Vector),
                    nullptr, 0
                );
                GxDrawLockedElements(GxPrim_Triangles, static_cast<uint32_t>(s_shadowIndices.size()), s_shadowIndices.data());
                GxPrimUnlockVertexPtrs();
            }
        }
    }
}

void BlobShadowsEnd() {
    if (s_blobActive) {
        GxRsPop();
        s_blobActive = false;
    }
}

bool TerrainSphereVisible(const C3Vector& center, float radius) {
    return SphereVisible(center, radius);
}


int32_t TerrainCameraLiquidKind() {
    return s_cameraLiquidKind;
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

void TerrainForEachDoodad(void (*fn)(CM2Model* model, void* arg), void* arg) {
    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (uint32_t i = 0; i < tile.doodadCount; i++) {
            if (tile.doodads[i]) {
                fn(tile.doodads[i], arg);
            }
        }

        if (tile.wmos) {
            for (uint32_t wi = 0; wi < tile.wmoCount; wi++) {
                WmoInstance& w = tile.wmos[wi];

                for (uint32_t i = 0; i < w.doodadCount; i++) {
                    if (w.doodads[i]) {
                        fn(w.doodads[i], arg);
                    }
                }
            }
        }
    }
}

// If the world point lies inside a loaded WMO's interior group, report that WMO's interior ambient.
// Entities (and the player) standing inside a building are lit by its interior lighting instead of
// the outdoor sun, exactly as the reference switches a unit's lighting by the volume it occupies.
// Bucket every triangle of a group into a uniform XY grid, once. Mirrors BuildWmoShadowGrid but
// without the up-facing filter.
void BuildWmoContainGrid(WmoGroup& grp) {
    if (grp.containGridBuilt) {
        return;
    }

    grp.containGridBuilt = true;

    if (!grp.indexCount || !grp.positions || !grp.vertexCount) {
        return;
    }

    // Vertices are instance-local while the bounds are world-space, so the grid is anchored on the
    // local extent rather than on boundsMin.
    float minX = grp.positions[0].x;
    float minY = grp.positions[0].y;
    float maxX = minX;
    float maxY = minY;

    for (uint32_t i = 1; i < grp.vertexCount; i++) {
        minX = grp.positions[i].x < minX ? grp.positions[i].x : minX;
        minY = grp.positions[i].y < minY ? grp.positions[i].y : minY;
        maxX = grp.positions[i].x > maxX ? grp.positions[i].x : maxX;
        maxY = grp.positions[i].y > maxY ? grp.positions[i].y : maxY;
    }

    float spanX = maxX - minX;
    float spanY = maxY - minY;

    if (spanX <= 0.0f || spanY <= 0.0f) {
        return;
    }

    const float CELL = 8.0f;
    const int32_t MAX_CELLS = 128;

    grp.containMinX = minX;
    grp.containMinY = minY;
    grp.containCellSize = CELL;
    grp.containCellsX = static_cast<int32_t>(spanX / CELL) + 1;
    grp.containCellsY = static_cast<int32_t>(spanY / CELL) + 1;

    while (grp.containCellsX > MAX_CELLS || grp.containCellsY > MAX_CELLS) {
        grp.containCellSize *= 2.0f;
        grp.containCellsX = static_cast<int32_t>(spanX / grp.containCellSize) + 1;
        grp.containCellsY = static_cast<int32_t>(spanY / grp.containCellSize) + 1;
    }

    grp.containGrid.clear();
    grp.containGrid.resize(static_cast<size_t>(grp.containCellsX) * grp.containCellsY);

    for (uint32_t i = 0; i + 2 < grp.indexCount; i += 3) {
        uint16_t a = grp.indices[i];
        uint16_t b = grp.indices[i + 1];
        uint16_t c = grp.indices[i + 2];

        if (a >= grp.vertexCount || b >= grp.vertexCount || c >= grp.vertexCount) {
            continue;
        }

        const C3Vector& pa = grp.positions[a];
        const C3Vector& pb = grp.positions[b];
        const C3Vector& pc = grp.positions[c];

        float tMinX = pa.x < pb.x ? (pa.x < pc.x ? pa.x : pc.x) : (pb.x < pc.x ? pb.x : pc.x);
        float tMaxX = pa.x > pb.x ? (pa.x > pc.x ? pa.x : pc.x) : (pb.x > pc.x ? pb.x : pc.x);
        float tMinY = pa.y < pb.y ? (pa.y < pc.y ? pa.y : pc.y) : (pb.y < pc.y ? pb.y : pc.y);
        float tMaxY = pa.y > pb.y ? (pa.y > pc.y ? pa.y : pc.y) : (pb.y > pc.y ? pb.y : pc.y);

        int32_t c0 = static_cast<int32_t>((tMinX - grp.containMinX) / grp.containCellSize);
        int32_t c1 = static_cast<int32_t>((tMaxX - grp.containMinX) / grp.containCellSize);
        int32_t r0 = static_cast<int32_t>((tMinY - grp.containMinY) / grp.containCellSize);
        int32_t r1 = static_cast<int32_t>((tMaxY - grp.containMinY) / grp.containCellSize);

        c0 = c0 < 0 ? 0 : c0;
        r0 = r0 < 0 ? 0 : r0;
        c1 = c1 >= grp.containCellsX ? grp.containCellsX - 1 : c1;
        r1 = r1 >= grp.containCellsY ? grp.containCellsY - 1 : r1;

        for (int32_t cy = r0; cy <= r1; cy++) {
            for (int32_t cx = c0; cx <= c1; cx++) {
                grp.containGrid[static_cast<size_t>(cy) * grp.containCellsX + cx].push_back(i);
            }
        }
    }
}

// Is a local-space point actually inside this group's geometry?
//
// A group's bounding box is much larger than the room it describes. For one big structure like Ebon
// Hold, an interior hall's box reaches out over the open deck, so a box test alone calls the deck
// indoors. Measured 2026-09-16: standing outdoors at (2355.6, -5677.9, 429.8) the box test matched
// interior group 3, whose box spans z 414 to 544.
//
// The test used instead is physical: a point inside a room has group geometry BOTH above and below
// it, a ceiling and a floor. Out on a deck there is a floor but open sky above. The reference decides
// this properly through the group BSP and portals; this is a cheap approximation of the same idea.
bool WmoGroupContains(WmoGroup& grp, float lx, float ly, float lz) {
    BuildWmoContainGrid(grp);

    if (grp.containCellsX <= 0 || grp.containCellsY <= 0) {
        return false;
    }

    int32_t cx = static_cast<int32_t>((lx - grp.containMinX) / grp.containCellSize);
    int32_t cy = static_cast<int32_t>((ly - grp.containMinY) / grp.containCellSize);

    if (cx < 0 || cy < 0 || cx >= grp.containCellsX || cy >= grp.containCellsY) {
        return false;
    }

    bool above = false;
    bool below = false;

    for (uint32_t i : grp.containGrid[static_cast<size_t>(cy) * grp.containCellsX + cx]) {
        const C3Vector& pa = grp.positions[grp.indices[i]];
        const C3Vector& pb = grp.positions[grp.indices[i + 1]];
        const C3Vector& pc = grp.positions[grp.indices[i + 2]];

        // Point in triangle in XY: the three edge cross products must share a sign.
        float d1 = (lx - pb.x) * (pa.y - pb.y) - (pa.x - pb.x) * (ly - pb.y);
        float d2 = (lx - pc.x) * (pb.y - pc.y) - (pb.x - pc.x) * (ly - pc.y);
        float d3 = (lx - pa.x) * (pc.y - pa.y) - (pc.x - pa.x) * (ly - pa.y);

        bool anyNeg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
        bool anyPos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;

        if (anyNeg && anyPos) {
            continue;
        }

        float tMinZ = pa.z < pb.z ? (pa.z < pc.z ? pa.z : pc.z) : (pb.z < pc.z ? pb.z : pc.z);
        float tMaxZ = pa.z > pb.z ? (pa.z > pc.z ? pa.z : pc.z) : (pb.z > pc.z ? pb.z : pc.z);

        // A small lift keeps the floor the unit is standing on from reading as being above it.
        float probe = lz + 0.5f;

        if (tMinZ > probe) {
            above = true;
        } else if (tMaxZ < probe) {
            below = true;
        }

        if (above && below) {
            return true;
        }
    }

    return false;
}

// The AreaTable.dbc row for the terrain chunk under a world position, or 0 when that chunk is not
// loaded.
//
// Each MCNK carries its own area id, so this is a chunk lookup rather than a zone polygon test --
// which is why a subzone boundary in the reference follows chunk edges.
uint32_t TerrainAreaIDAt(const C3Vector& pos) {
    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (auto& chunk : tile.chunks) {
            if (!chunk.valid) {
                continue;
            }

            if (pos.x >= chunk.boundsMin.x && pos.x <= chunk.boundsMax.x &&
                pos.y >= chunk.boundsMin.y && pos.y <= chunk.boundsMax.y) {
                return chunk.areaID;
            }
        }
    }

    return 0;
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
    for (auto& tile : s_tiles) {
        if (!tile.loaded || !tile.wmos) {
            continue;
        }

        for (uint32_t wi = 0; wi < tile.wmoCount; wi++) {
            WmoInstance& w = tile.wmos[wi];

            if (w.hasBounds && (pos.x < w.bboxMin.x || pos.x > w.bboxMax.x ||
                                pos.y < w.bboxMin.y || pos.y > w.bboxMax.y ||
                                pos.z < w.bboxMin.z || pos.z > w.bboxMax.z)) {
                continue;
            }

            for (uint32_t gi = 0; gi < w.groupCount; gi++) {
                WmoGroup& grp = w.groups[gi];

                if (!grp.interior || !grp.vertexCount) {
                    continue;
                }

                if (pos.x >= grp.boundsMin.x && pos.x <= grp.boundsMax.x &&
                    pos.y >= grp.boundsMin.y && pos.y <= grp.boundsMax.y &&
                    pos.z >= grp.boundsMin.z && pos.z <= grp.boundsMax.z &&
                    WmoGroupContains(grp, pos.x - w.origin.x, pos.y - w.origin.y,
                                     pos.z - w.origin.z)) {
                    return true;
                }
            }
        }
    }

    return false;
}

// The light for a unit standing on a WMO floor, by the reference's mechanism: a probe from one
// yard above its feet to twelve below, through the interior groups' BSP, sampling the MOCV at the
// floor face it lands on (CMapEntity::FloorLight, FUN_007a0d60). The reference reaches the
// entity's MapObjDef and group through its parent links; without that graph every loaded instance
// whose box holds the probe is tried, and within it every interior group whose box holds it, first
// hit wins. Exterior groups never answer, so a unit out on a deck is lit by the sky again.
bool TerrainWmoFloorLightAt(const C3Vector& pos, CImVector* diffuse, CImVector* ambient) {
    for (auto& tile : s_tiles) {
        if (!tile.loaded || !tile.wmos) {
            continue;
        }

        for (uint32_t wi = 0; wi < tile.wmoCount; wi++) {
            WmoInstance& w = tile.wmos[wi];

            if (w.hasBounds && (pos.x < w.bboxMin.x || pos.x > w.bboxMax.x ||
                                pos.y < w.bboxMin.y || pos.y > w.bboxMax.y ||
                                pos.z + 1.0f < w.bboxMin.z || pos.z - 12.0f > w.bboxMax.z)) {
                continue;
            }

            // Into the model's own space: undo the placement translation, then its yaw, so the
            // probe matches WmoGroup::queryVerts and the BSP planes that were built alongside them.
            float dx = pos.x - w.origin.x;
            float dy = pos.y - w.origin.y;

            C3Vector local = {
                dx * w.yawCos + dy * w.yawSin,
                dy * w.yawCos - dx * w.yawSin,
                pos.z - w.origin.z
            };

            for (uint32_t gi = 0; gi < w.groupCount; gi++) {
                WmoGroup& grp = w.groups[gi];

                // A group without a BSP or vertex colours cannot answer the probe
                if (!grp.vertexCount || !grp.objGroup.m_bspNodes || !grp.objGroup.m_colors) {
                    continue;
                }

                if (pos.x < grp.boundsMin.x || pos.x > grp.boundsMax.x ||
                    pos.y < grp.boundsMin.y || pos.y > grp.boundsMax.y ||
                    pos.z + 1.0f < grp.boundsMin.z || pos.z - 12.0f > grp.boundsMax.z) {
                    continue;
                }

                uint32_t flags = 0;
                uint8_t alpha = 0;

                if (CMapEntity::FloorLight(local, &w.mapObj, &grp.objGroup, diffuse, ambient, &flags, &alpha)) {
                    return true;
                }
            }
        }
    }

    return false;
}


// ------------------------------------------------------------------------------------------------
// Sun and moons (reference FUN_007eecc0 positions them, FUN_009ac660 draws each as one screen-
// aligned quad). Each body is a direction, not an orbit: the position is the camera plus a fixed
// radius of 12 along a direction built from two day-driven angle bands. Both angle tables and the
// size tables were recovered by disassembly; see docs/ref/parity-sky-bodies.md.
//
// Note the azimuth is CONSTANT for the sun and the first moon (45 degrees), so they rise and set in
// the same compass direction -- which agrees with the outdoor light direction, whose azimuth is 225
// degrees pointing away from the light. The second moon drifts in azimuth on a 1.7-day cycle.
// ------------------------------------------------------------------------------------------------

const float SKY_BODY_RADIUS = 12.0f;

struct SkyBodyKey { float time; float value; };

// Wrap-around linear interpolation over (time, value) pairs. The reference passes the key count
// in ESI and the table in EDI, clamps the parameter to [0, 1], and wraps the last key round to
// the first across the end of the day. Every band in the sky bodies, the glare and the sky
// highlight is read through it.
// ref: FUN_007ed3b0
float InterpBodyBand(const SkyBodyKey* keys, int32_t count, float t) {
    if (count <= 0) {
        return 0.0f;
    }

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    int32_t hi = 0;

    while (hi < count && keys[hi].time <= t) {
        hi++;
    }

    int32_t lo;

    if (hi == count) {
        hi = 0;
        lo = count - 1;
    } else if (hi == 0) {
        lo = count - 1;
    } else {
        lo = hi - 1;
    }

    float span = keys[hi].time - keys[lo].time;

    if (span < 0.0f) {
        span += 1.0f;
    }

    if (span < 0.001f) {
        return keys[lo].value;
    }

    float f = t - keys[lo].time;

    if (f < 0.0f) {
        f += 1.0f;
    }

    return keys[lo].value + (keys[hi].value - keys[lo].value) * (f / span);
}

const SkyBodyKey SUN_THETA[5] = {
    { 0.2291667f, 1.7453293f }, { 0.4965278f, 0.0872665f }, { 0.5000000f, 0.0872665f },
    { 0.5034722f, 0.0872665f }, { 0.8958333f, 1.7453293f },
};
const SkyBodyKey SUN_PHI[3] = { { 0.2291667f, 0.7853982f }, { 0.5f, 0.7853982f }, { 0.8958333f, 0.7853982f } };
const SkyBodyKey SUN_SIZE[4] = { { 0.25f, 2.0f }, { 0.28125f, 1.0f }, { 0.84375f, 1.0f }, { 0.875f, 2.0f } };

const SkyBodyKey MOON_THETA[5] = {
    { 0.0000000f, 0.6108652f }, { 0.0034722f, 0.6108652f }, { 0.1666667f, 1.7453293f },
    { 0.9166667f, 1.7453293f }, { 0.9965278f, 0.6108652f },
};
const SkyBodyKey MOON1_PHI[3] = { { 0.0f, 0.7853982f }, { 0.1666667f, 0.7853982f }, { 0.9166667f, 0.7853982f } };
const SkyBodyKey MOON2_PHI[3] = { { 0.0000000f, 2.3561945f }, { 0.1666667f, 2.6179938f }, { 0.9166667f, 2.8797934f } };
const SkyBodyKey MOON_SIZE[4] = { { 0.0416667f, 1.0f }, { 0.1666667f, 1.5f }, { 0.9166667f, 1.5f }, { 0.9993056f, 1.0f } };

HTEXTURE s_bodyTexture[3] = { nullptr, nullptr, nullptr };
HTEXTURE s_glareTexture[2] = { nullptr, nullptr }; // sun, moon 1 (moon 2 has no glare)
bool s_bodyTried = false;

// Glare visibility over the day, from the glare objects at 0x00d38ea8 (sun) and 0x00d38f58 (moon);
// base sizes 1.0 and 2.0 respectively.
const SkyBodyKey SUN_GLARE_VIS[4] = { { 0.2708333f, 0.0f }, { 0.3125f, 1.0f }, { 0.8125f, 1.0f }, { 0.875f, 0.0f } };
const SkyBodyKey MOON_GLARE_VIS[4] = { { 0.0833333f, 1.0f }, { 0.1354167f, 0.0f }, { 0.9479167f, 0.0f }, { 0.9993056f, 1.0f } };

void DrawSkyBody(const SkyBodyKey* theta, const SkyBodyKey* phi, int32_t phiCount,
                 const SkyBodyKey* sizeBand, float baseSize, float t, HTEXTURE texture,
                 const C3Vector& right, const C3Vector& up) {
    if (!texture) {
        return;
    }

    float th = InterpBodyBand(theta, 5, t);
    float ph = InterpBodyBand(phi, phiCount, t);
    float size = InterpBodyBand(sizeBand, 4, t) * baseSize;

    float st = sinf(th);
    C3Vector dir = { cosf(ph) * st, sinf(ph) * st, cosf(th) };

    // Below the eye-level plane the reference clips the quad away entirely; a body parked ten
    // degrees under the horizon is simply not drawn.
    if (dir.z <= 0.0f) {
        return;
    }

    // CAMERA-RELATIVE: the sky pass builds its matrix from the untranslated view (see SkyRender),
    // so everything it draws is centred on the origin, like the dome and the cloud sheet. Adding
    // the camera's world position here would have flung the body thousands of yards off screen.
    C3Vector c = { dir.x * SKY_BODY_RADIUS, dir.y * SKY_BODY_RADIUS, dir.z * SKY_BODY_RADIUS };

    float h = size * 0.5f;
    C3Vector pos[4] = {
        { c.x - right.x * h + up.x * h, c.y - right.y * h + up.y * h, c.z - right.z * h + up.z * h },
        { c.x + right.x * h + up.x * h, c.y + right.y * h + up.y * h, c.z + right.z * h + up.z * h },
        { c.x + right.x * h - up.x * h, c.y + right.y * h - up.y * h, c.z + right.z * h - up.z * h },
        { c.x - right.x * h - up.x * h, c.y - right.y * h - up.y * h, c.z - right.z * h - up.z * h },
    };
    C2Vector uv[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

    // Tint from LightIntBand band 9, as the reference does.
    const C3Vector& tint = CWorld::GetBodyTint();
    CImVector col[4];

    for (int32_t i = 0; i < 4; i++) {
        col[i].b = static_cast<uint8_t>((tint.z > 1.0f ? 1.0f : tint.z) * 255.0f);
        col[i].g = static_cast<uint8_t>((tint.y > 1.0f ? 1.0f : tint.y) * 255.0f);
        col[i].r = static_cast<uint8_t>((tint.x > 1.0f ? 1.0f : tint.x) * 255.0f);
        col[i].a = 0xFF;
    }

    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

    GxRsSet(GxRs_Texture0, TextureGetGxTex(texture, 0, nullptr));
    GxPrimLockVertexPtrs(4, pos, sizeof(C3Vector), nullptr, 0, col, sizeof(CImVector), nullptr, 0, uv, sizeof(C2Vector), nullptr, 0);
    GxDrawLockedElements(GxPrim_Triangles, 6, idx);
    GxPrimUnlockVertexPtrs();
}

// Glare halo around a body, drawn additively at the body's position and brightest when the body is
// near the centre of view.
//
// The visibility band and the base sizes are recovered constants. What is NOT recovered is the
// reference's size law for the quad -- FUN_007ef6e0 builds it and was not transcribed instruction
// by instruction -- so the base size is used directly, in the same world units as the disc sizes.
// If the halo reads too small or too large, that is the first constant to revisit.
void DrawGlare(const SkyBodyKey* theta, const SkyBodyKey* phi, int32_t phiCount,
               const SkyBodyKey* visBand, float baseSize, float t, HTEXTURE texture,
               const C3Vector& right, const C3Vector& up, const C3Vector& fwd) {
    if (!texture) {
        return;
    }

    float vis = InterpBodyBand(visBand, 4, t);

    if (vis <= 0.01f) {
        return;
    }

    float th = InterpBodyBand(theta, 5, t);
    float ph = InterpBodyBand(phi, phiCount, t);
    float st = sinf(th);
    C3Vector dir = { cosf(ph) * st, sinf(ph) * st, cosf(th) };

    if (dir.z <= 0.0f) {
        return;
    }

    // Brightest looking straight at it, gone once it is well off to the side
    float facing = dir.x * fwd.x + dir.y * fwd.y + dir.z * fwd.z;

    if (facing <= 0.0f) {
        return;
    }

    float intensity = vis * facing * facing;

    // CAMERA-RELATIVE: the sky pass builds its matrix from the untranslated view (see SkyRender),
    // so everything it draws is centred on the origin, like the dome and the cloud sheet. Adding
    // the camera's world position here would have flung the body thousands of yards off screen.
    C3Vector c = { dir.x * SKY_BODY_RADIUS, dir.y * SKY_BODY_RADIUS, dir.z * SKY_BODY_RADIUS };

    float h = baseSize * 0.5f;
    C3Vector pos[4] = {
        { c.x - right.x * h + up.x * h, c.y - right.y * h + up.y * h, c.z - right.z * h + up.z * h },
        { c.x + right.x * h + up.x * h, c.y + right.y * h + up.y * h, c.z + right.z * h + up.z * h },
        { c.x + right.x * h - up.x * h, c.y + right.y * h - up.y * h, c.z + right.z * h - up.z * h },
        { c.x - right.x * h - up.x * h, c.y - right.y * h - up.y * h, c.z - right.z * h - up.z * h },
    };
    C2Vector uv[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

    const C3Vector& tint = CWorld::GetBodyTint();
    CImVector col[4];

    for (int32_t i = 0; i < 4; i++) {
        float r = tint.x * intensity;
        float g = tint.y * intensity;
        float b = tint.z * intensity;
        col[i].b = static_cast<uint8_t>((b > 1.0f ? 1.0f : b) * 255.0f);
        col[i].g = static_cast<uint8_t>((g > 1.0f ? 1.0f : g) * 255.0f);
        col[i].r = static_cast<uint8_t>((r > 1.0f ? 1.0f : r) * 255.0f);
        col[i].a = 0xFF;
    }

    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

    GxRsSet(GxRs_Texture0, TextureGetGxTex(texture, 0, nullptr));
    GxPrimLockVertexPtrs(4, pos, sizeof(C3Vector), nullptr, 0, col, sizeof(CImVector), nullptr, 0, uv, sizeof(C2Vector), nullptr, 0);
    GxDrawLockedElements(GxPrim_Triangles, 6, idx);
    GxPrimUnlockVertexPtrs();
}

void SkyBodiesRender() {
    if (!s_bodyTried) {
        s_bodyTried = true;
        CStatus status;
        auto flags = CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1);
        s_bodyTexture[0] = TextureCreate("Textures\\sunCenter.blp", flags, &status, 0);
        s_bodyTexture[1] = TextureCreate("Textures\\moon.blp", flags, &status, 0);
        s_bodyTexture[2] = TextureCreate("Textures\\moon02.blp", flags, &status, 0);
        s_glareTexture[0] = TextureCreate("Textures\\sunGlare.blp", flags, &status, 0);
        s_glareTexture[1] = TextureCreate("Textures\\moonGlare.blp", flags, &status, 0);
    }

    // One-shot report: whether the sky bodies are even reachable, and whether their textures
    // loaded. A missing sun was reported from a real session, and "the pass runs but the texture is
    // null" looks identical on screen to "the pass never runs".
    static bool reported = false;

    if (!reported) {
        reported = true;
        fprintf(stderr, "SkyBodies: sun %s moon %s moon02 %s sunGlare %s moonGlare %s\n",
                s_bodyTexture[0] ? "ok" : "MISSING", s_bodyTexture[1] ? "ok" : "MISSING",
                s_bodyTexture[2] ? "ok" : "MISSING", s_glareTexture[0] ? "ok" : "MISSING",
                s_glareTexture[1] ? "ok" : "MISSING");
    }

    float t = CWorld::GetDayProgress();

    // Screen-aligned billboards
    const C3Vector& fwd = CWorld::GetCameraDir();
    C3Vector right = { fwd.y, -fwd.x, 0.0f };
    float rl = sqrtf(right.x * right.x + right.y * right.y);

    if (rl < 1e-4f) {
        right = { 1.0f, 0.0f, 0.0f };
    } else {
        right.x /= rl; right.y /= rl;
    }

    C3Vector up = { right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y - right.y * fwd.x };

    GxRsSet(GxRs_BlendingMode, GxBlend_Add);

    DrawSkyBody(SUN_THETA, SUN_PHI, 3, SUN_SIZE, 1.0f, t, s_bodyTexture[0], right, up);
    DrawSkyBody(MOON_THETA, MOON1_PHI, 3, MOON_SIZE, 1.75f, t, s_bodyTexture[1], right, up);

    // The second moon runs on its own 1.7-day cycle, so its phase is not the day fraction
    float day = t; // frozen has no absolute day counter yet; phase folds back to the day fraction
    float t2 = day / 1.7f;
    t2 -= static_cast<float>(static_cast<int32_t>(t2));

    DrawSkyBody(MOON_THETA, MOON2_PHI, 3, MOON_SIZE, 1.0f, t2, s_bodyTexture[2], right, up);

    // Glare over the discs; moon 2 has none.
    DrawGlare(SUN_THETA, SUN_PHI, 3, SUN_GLARE_VIS, 1.0f, t, s_glareTexture[0], right, up, fwd);
    DrawGlare(MOON_THETA, MOON1_PHI, 3, MOON_GLARE_VIS, 2.0f, t, s_glareTexture[1], right, up, fwd);
}

// ------------------------------------------------------------------------------------------------
// The sky highlight -- the dome's azimuthal colour variation, reference FUN_007f0530.
//
// The four rings between the zenith and 45 degrees are not painted flat. Their colour is pushed per
// segment by a profile band sampled at the segment's azimuth relative to the camera, so one half of
// the dome brightens and the opposite half darkens toward the zenith colour. It is the sunrise and
// sunset glow, and it is off for most of the day.
//
// Every number below was read out of WoW.exe, not inferred:
//   * strength = StrengthBand(dayFraction) * LightParams.highlightSky. The band is at 0x00af4b7c
//     and peaks only around 06:30 and 21:30; the scale is DNInfo+0x128, which FUN_007ebff0 fills
//     with `fildl 0x4(%edi)` at 0x007ec1cd -- LightParams column 1, highlightSky, 0 or 1. A zone
//     whose row carries 0 therefore gets no highlight at all, which is most of them.
//   * the segment parameter is wrap01(yaw / (2pi) + 0.25 + seg * (-1 / segCount)). The two
//     constants are 0.159155 at 0x00a41ca8 and 0.25 at 0x00a41b00, and the -1 is at 0x009e2ef4.
//     yaw is atan2(forward.y, forward.x) normalised to [0, 2pi), computed at 0x007f3920 from the
//     camera forward vector the DayNight block keeps at +0x30.
//   * profile = ProfileBand(that parameter), the band at 0x00af4bac. It is positive across one
//     half of the dome and negative across the other.
//
// The two branches, taken verbatim from 0x007f06b1 and 0x007f070b (the sign of the profile picks
// between them, with zero going to the first):
//
//     local = lerp(ringColor, topRingColor, strength)
//     profile >= 0:  out = lerp(ringColor, local, (profile - 1) * strength)
//     profile <  0:  out = lerp(local, lerp(local, zenithColor, strength * 0.7), -profile * strength)
//
// One deliberate divergence. The reference lerps 0-255 bytes and casts the result back to a byte
// with no clamp (FUN_007ed2d0), so an out-of-range channel would wrap; frozen keeps floats and
// clamps to [0, 255]. Both branches can leave the 0..1 range because their factors extrapolate,
// and a wrapped channel would be a garish artefact rather than a faithful colour.
const SkyBodyKey SKY_HIGHLIGHT_STRENGTH[6] = {
    { 0.125000f, 0.0f }, { 0.270833f, 1.0f }, { 0.291667f, 0.0f },
    { 0.854167f, 0.0f }, { 0.895833f, 1.0f }, { 0.999306f, 0.0f },
};

const SkyBodyKey SKY_HIGHLIGHT_PROFILE[6] = {
    { 0.125f, 1.0f }, { 0.375f, 0.0f }, { 0.500f, -0.5f },
    { 0.625f, -0.7f }, { 0.750f, -0.5f }, { 0.875f, 0.0f },
};

// Per-channel linear interpolation, matching FUN_007ed2d0 apart from the clamp noted above.
C3Vector SkyLerp(const C3Vector& a, const C3Vector& b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

void SkyRender() {
    // The reference skips the whole sky pass while the camera is under liquid; the clear colour
    // (the underwater fog) is the backdrop instead.
    if (s_cameraLiquidKind >= 0) {
        return;
    }

    EnsureShaders();

    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    if (!s_skyBuilt) {
        BuildSkyDome();
    }

    if (!s_skyWhite) {
        for (int32_t i = 0; i < SKY_WHITE_DIM * SKY_WHITE_DIM; i++) {
            s_skyWhitePixels[i] = CImVector { 0xFF, 0xFF, 0xFF, 0xFF };
        }

        // Last argument 0: a non-zero value replaces the filter above with the global mipmapped
        // one, which makes the device ask this callback for mip levels it cannot supply.
        s_skyWhite = TextureCreate(SKY_WHITE_DIM, SKY_WHITE_DIM, GxTex_Argb8888, GxTex_Argb8888, CGxTexFlags(GxTex_Linear, 1, 1, 0, 0, 0, 1), s_skyWhitePixels, SkyWhiteCallback, __FILE__, 0);
    }

    // One band per ring, straight across -- no gradient maths. Ring i takes sky band i, and the
    // bottom two rings both take band 7, which is the fog colour. That flat assignment IS the
    // reference's shading (FUN_007f0530 writes 1 zenith colour, then 4 rings of 24 from successive
    // bands, then 24 + 1 of the fog band).
    //
    // On top of that the four middle rings carry the sky highlight, which varies their colour per
    // segment with the camera azimuth; see the block above SkyRender.
    C3Vector ringColor[SKY_RINGS + 1];

    for (int32_t ring = 0; ring <= SKY_RINGS; ring++) {
        ringColor[ring] = CWorld::GetSkyColor(ring < 5 ? ring : 5);
    }

    float highlight = InterpBodyBand(SKY_HIGHLIGHT_STRENGTH, 6, CWorld::GetDayProgress())
        * CWorld::GetSkyHighlight();

    // The reference rebuilds this per ring; the yaw cannot change inside a frame, so it is hoisted.
    const C3Vector& camDir = CWorld::GetCameraDir();
    float yaw = atan2f(camDir.y, camDir.x);

    if (yaw < 0.0f) {
        yaw += 6.2831855f;
    }

    float segParam0 = yaw * 0.159155f + 0.25f;

    if (segParam0 > 1.0f) {
        segParam0 -= 1.0f;
    }

    const float segStep = -1.0f / SKY_SEGS;

    int32_t v = 0;

    for (int32_t ring = 0; ring <= SKY_RINGS; ring++) {
        const C3Vector& base = ringColor[ring];

        // Rings 1..4 are the reference's four highlighted rings: the zenith vertex above them and
        // the two fog-band rings below are flat there too.
        bool highlighted = highlight > 0.0f && ring >= 1 && ring <= 4;
        C3Vector local = highlighted ? SkyLerp(base, ringColor[1], highlight) : base;
        float p = segParam0;

        for (int32_t seg = 0; seg <= SKY_SEGS; seg++) {
            C3Vector c = base;

            if (highlighted) {
                if (p < 0.0f) {
                    p += 1.0f;
                }

                float profile = InterpBodyBand(SKY_HIGHLIGHT_PROFILE, 6, p);

                if (profile >= 0.0f) {
                    c = SkyLerp(base, local, (profile - 1.0f) * highlight);
                } else {
                    C3Vector toward = SkyLerp(local, ringColor[0], highlight * 0.7f);
                    c = SkyLerp(local, toward, -profile * highlight);
                }

                p += segStep;
            }

            float r = c.x * 255.0f;
            float g = c.y * 255.0f;
            float b = c.z * 255.0f;
            r = r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r);
            g = g < 0.0f ? 0.0f : (g > 255.0f ? 255.0f : g);
            b = b < 0.0f ? 0.0f : (b > 255.0f ? 255.0f : b);

            s_skyCol[v].r = static_cast<uint8_t>(r);
            s_skyCol[v].g = static_cast<uint8_t>(g);
            s_skyCol[v].b = static_cast<uint8_t>(b);
            s_skyCol[v].a = 0xFF;
            v++;
        }
    }

    // The dome is centred on the camera: use the eye-at-origin view (no camera translation)
    C44Matrix view;
    GxXformView(view);
    C44Matrix proj;
    GxXformProjNative(proj);
    C44Matrix viewProj = view * proj;
    C44Matrix viewProjT = viewProj.Transpose();

    // The reference (FUN_007f09b0) draws the sky AFTER the opaque world, through a viewport whose
    // depth range is squeezed to [0.999, 1.0] (DAT_00adeef0/DAT_00adeef4). Every sky pixel then
    // lands at the far end of the depth buffer and the ordinary less-equal test lets it through
    // only where the cleared depth (1.0) is still untouched, so the sky can never draw over
    // terrain, buildings or models, and the skybox model needs no depth clear afterwards.
    float vpMinX, vpMaxX, vpMinY, vpMaxY, vpMinZ, vpMaxZ;
    GxXformViewport(vpMinX, vpMaxX, vpMinY, vpMaxY, vpMinZ, vpMaxZ);
    GxXformSetViewport(vpMinX, vpMaxX, vpMinY, vpMaxY, SKY_VIEWPORT_MIN_Z, SKY_VIEWPORT_MAX_Z);

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 0); // less-equal
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_VertexShader, s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, s_uiPixelShader);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<float*>(&viewProjT), 4);

    // The bodies go down first and the dome is ADDED over them, which is how the reference layers
    // the sky (the dome is GxBlend_Add there). That only works because the scene clears to black
    // under an open sky -- see CGWorldFrame::OnWorldRender -- so the dome's colour IS the sky.
    SkyBodiesRender();

    GxRsSet(GxRs_BlendingMode, GxBlend_Add);
    GxRsSet(GxRs_Texture0, s_skyWhite ? TextureGetGxTex(s_skyWhite, 0, nullptr) : nullptr);

    // A zone whose light names a skybox (LightParams -> LightSkybox) draws that sky model as the
    // real sky -- e.g. the Death Knight start's IceCrownSky. The reference (FUN_007f09b0) skips the
    // stars, the dome layers and the clouds entirely while such a skybox is up at full alpha, so
    // the gradient dome is only drawn for zones without one.
    const char* skyboxPath = CWorld::GetSkyboxPath();

    // Only suppress the gradient once the zone's skybox model is actually up. A zone can name a
    // skybox that fails to load, or is still streaming on the first frames after a map change, and
    // skipping the dome on the strength of the name alone left the sky black.
    // ...and only once it is actually SUBMITTING GEOMETRY. Checking m_m2DataLoaded alone was too
    // weak: on map 609 the model loads, the dome switches itself off on that evidence, and the
    // skybox then contributes nothing -- measured as array54[M2PASS_0].Count() == 0 every frame --
    // leaving the sky black with the sun, moon and clouds hidden behind nothing at all.
    //
    // The element count is from the previous frame (this runs before the skybox is animated below),
    // which is exactly what is wanted: the dome keeps drawing until the skybox has demonstrably
    // taken over, and resumes the moment it stops.
    bool skyboxDrawing = s_skyboxScene
        && (s_skyboxScene->array54[M2PASS_0].Count() > 0 || s_skyboxScene->array44.Count() > 0);

    bool skyboxUp = skyboxPath && s_skyboxModel && s_skyboxModel->m_shared
        && s_skyboxModel->m_shared->m_m2DataLoaded && skyboxDrawing;

    if (!skyboxUp) {
        GxPrimLockVertexPtrs(
            SKY_VERTS,
            s_skyPos, sizeof(C3Vector),
            nullptr, 0,
            s_skyCol, sizeof(CImVector),
            nullptr, 0,
            s_skyUv, sizeof(C2Vector),
            nullptr, 0
        );
        GxDrawLockedElements(GxPrim_Triangles, s_skyIdxCount, s_skyIdx);
        GxPrimUnlockVertexPtrs();
    }

    GxRsPop();

    // Clouds over the gradient, under the stars and the skybox model.
    {
        uint32_t nowMs = CWorld::GetM2Scene() ? CWorld::GetM2Scene()->m_time : 0;
        static uint32_t s_cloudLast = 0;
        float dt = s_cloudLast ? static_cast<float>(nowMs - s_cloudLast) * 0.001f : 0.0f;
        s_cloudLast = nowMs;

        if (dt > 0.25f) {
            dt = 0.25f;
        }

        CloudsUpdate(dt);
        CloudsRender(viewProjT, s_cameraPos, s_uiVertexShader[0], s_uiPixelShader);
    }

    // Stars (reference FUN_009abd50: Environments\Stars\stars.mdl in its own scene, passes 0/1).
    // The model's animation spans the whole day, so it is seeked to the time of day like the
    // skybox; its materials fade the stars in only at night. Drawn through the same far-depth
    // viewport, over the gradient. Suppressed only once the zone's own skybox model is really up,
    // for the same reason as the dome above.
    if (!skyboxUp) {

        if (!s_starsTried) {
            s_starsTried = true;
            s_starsScene = M2CreateScene();

            if (s_starsScene) {
                s_starsModel = s_starsScene->CreateModel("Environments\\Stars\\stars.mdl", 0);

                if (s_starsModel) {
                    s_starsModel->SetLightingCallback(&SkyboxLightingCallback, nullptr);
                    s_starsModel->SetBoneSequence(-1, 0, -1, 0, 1.0f, 0, 1);
                    s_starsModel->SetAnimating(1);
                    s_starsModel->SetVisible(1);
                    s_starsModel->m_flag10000 = 1;
                }
            }
        }

        if (s_starsScene && s_starsModel) {
            s_starsModel->SetWorldTransform(s_cameraPos, 0.0f, 1.0f);

            uint32_t animDur = 0;

            if (s_starsModel->m_shared && s_starsModel->m_shared->m_m2DataLoaded && s_starsModel->m_shared->m_data->sequences.Count() > 0) {
                animDur = s_starsModel->m_shared->m_data->sequences[0].duration;
            }

            if (animDur > 0) {
                uint32_t target = static_cast<uint32_t>(CWorld::GetDayProgress() * animDur);
                uint32_t cur = s_starsScene->m_time % animDur;
                uint32_t delta = (target >= cur) ? (target - cur) : (target + animDur - cur);
                s_starsScene->AdvanceTime(delta);
            }

            s_starsScene->Animate(s_cameraPos);
            s_starsScene->Draw(M2PASS_0);
            s_starsScene->Draw(M2PASS_1);
        }
    }

    if (skyboxPath) {

        if (!s_skyboxScene) {
            s_skyboxScene = M2CreateScene();
        }

        if (s_skyboxScene && SStrCmpI(skyboxPath, s_skyboxLoaded, 0x7FFFFFFF) != 0) {
            if (s_skyboxModel) {
                s_skyboxModel->DetachFromScene();
                s_skyboxModel->Release();
                s_skyboxModel = nullptr;
            }

            s_skyboxModel = s_skyboxScene->CreateModel(skyboxPath, 0);

            if (s_skyboxModel) {
                s_skyboxModel->SetLightingCallback(&SkyboxLightingCallback, nullptr);
                s_skyboxModel->SetBoneSequence(-1, 0, -1, 0, 1.0f, 0, 1);
                s_skyboxModel->SetAnimating(1);
                s_skyboxModel->SetVisible(1);
                s_skyboxModel->m_flag10000 = 1;
            }

            SStrCopy(s_skyboxLoaded, skyboxPath, sizeof(s_skyboxLoaded));
        }

        if (s_skyboxModel) {
            // Centre it on the camera so it sits at infinity; the scene's Animate re-centres to the eye.
            // EVERY frame, not just at creation. CM2Scene::Animate clears m_flag8 and unlinks the
            // model from m_animateList as it walks them, and AnimateMT enqueues for drawing only
            // `if (m_flag8)`. A one-shot SetVisible/SetAnimating therefore draws for exactly one
            // frame -- the skybox loaded, resolved all its textures, reported drawable, and still
            // submitted zero elements every frame after the first.
            s_skyboxModel->SetVisible(1);
            s_skyboxModel->SetAnimating(1);

            s_skyboxModel->SetWorldTransform(s_cameraPos, 0.0f, 1.0f);

            // Advance the skybox in REAL TIME.
            //
            // Seeking the scene clock to the time of day starves GLOBAL SEQUENCES: a track with a
            // loopIndex other than 0xFFFF is timed by m_loops[i] = (m_scene->m_time - uint74) %
            // loopLength -- free-running loops that make IceCrownSky swirl and flicker. Pinning the
            // clock left the delta near zero each frame, so the sky was static.
            uint32_t nowMs = CWorld::GetM2Scene() ? CWorld::GetM2Scene()->m_time : 0;
            static uint32_t s_skyLastMs = 0;
            uint32_t skyDelta = s_skyLastMs && nowMs > s_skyLastMs ? nowMs - s_skyLastMs : 0;
            s_skyLastMs = nowMs;

            if (skyDelta > 250) {
                skyDelta = 250;
            }

            s_skyboxScene->AdvanceTime(skyDelta);

            // Drawn through the same far-depth viewport as the dome: the model's own depth writes
            // land at ~1.0, behind everything the world has already drawn.
            s_skyboxScene->Animate(s_cameraPos);

            // The gradient dome stops drawing once this model is up, so if it submits nothing the
            // sky is simply black. Count what each pass actually draws.
            s_skyboxScene->Draw(M2PASS_0);
            s_skyboxScene->Draw(M2PASS_1);
        }
    }

    GxXformSetViewport(vpMinX, vpMaxX, vpMinY, vpMaxY, vpMinZ, vpMaxZ);
}
