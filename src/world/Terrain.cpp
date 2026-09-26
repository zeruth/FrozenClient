#include "world/Terrain.hpp"
#include "world/DayNight.hpp"
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




    uint32_t holes = 0; // MCNK holes bitmask: cells the reference does not render (cave/mine mouths)

    C3Vector boundsMin = { 0.0f, 0.0f, 0.0f };
    C3Vector boundsMax = { 0.0f, 0.0f, 0.0f };

    // MH2O liquid layers on this chunk (world-space surface meshes), see ParseLiquid
    struct ChunkLiquid* liquids = nullptr;
    uint32_t liquidCount = 0;

    // Raw MCNK ground-effect inputs, still parsed out of the chunk: per 8x8 cell the dominant
    // layer (predTex, 2 bits per cell) and the cells that get no doodad at all. The map's own
    // scatter reads these from CMapChunk instead, so nothing here consumes them any more.

    bool valid = false;
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
    TerrainChunk chunks[256];

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

    uint32_t batchCount = 0;

    uint8_t* ndotl = nullptr; // static per-vertex sun term for exterior groups (re-lighting)
    CImVector* mocvAdd = nullptr; // exterior MOCV: additive local light (dark except near glows)
    bool interior = false;    // interior groups keep their baked MOCV (torch-lit, do not cycle)

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
    // The group's vertices in the model's own space, before the placement yaw. Only the BSP
    // queries use these; everything that draws uses `positions`. See the note where it is filled.
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

    // M2 doodads placed inside the WMO (MODD); the world scene draws them, we own the references
    CM2Model** doodads = nullptr;
    float* doodadScale = nullptr;  // placement scale, so the cull sphere matches the world size
    C3Vector* doodadAmbient = nullptr; // per-doodad baked lighting colour (MODD colour field)
    uint32_t doodadCount = 0;

    // Average interior (MOCV) brightness; interior doodads are lit by this constant value so they
    // match the torch-lit walls and do not cycle with the outdoor day/night like exterior props.
    C3Vector interiorAmbient = { 0.35f, 0.35f, 0.35f };

    // The root as the group queries reach it: the MOMT copy, the MOHD flags and ambient colour.
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

// Rebuild a chunk's vertex colours from its static lighting inputs and the current outdoor light.
void RebakeChunkColors(TerrainChunk& chunk) {
    const C3Vector& amb = CWorld::GetOutdoorAmbient();
    const C3Vector& dif = CWorld::GetOutdoorDiffuse();

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

int32_t LiquidAt(const C3Vector& pos, float& surfaceZ);


// Scatter the chunk's detail doodads (the reference's DetailDoodad batch builder): per 8x8 cell the
// dominant layer's GroundEffectTexture names up to four doodads with weights and an amount; cells
// flagged in noEffectDoodad and holes get none. Positions are deterministic per chunk so a tile
// reloads identically.

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

    // The layer walk, the alpha-map decode and the per-chunk 64x64 blend texture used to live
    // here. They fed only RenderShaded and RenderFallback, which are gone, so every one of those
    // fields was write-only: layerTex, layerEffect, nLayers, predTex, noEffectDoodad, areaID,
    // alphaCombined and alphaTexture. Dropping them takes 16 KB of alphaCombined per chunk -- about
    // 100 MB across the 5x5 tile pool -- and one 64x64 GPU texture per chunk with it. The layers
    // the terrain actually draws come from CMapRenderChunk, which parses MCLY and MCAL itself.

    chunk.valid = true;
}

// WMO doodads sit inside buildings, so the reference lights them by the interior/ambient term
// rather than the direct outdoor sun; using the zone ambient keeps them from being blown out.
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

        } else if (tag == FourCC("MOTX")) {
            motx = reinterpret_cast<const char*>(body);
        } else if (tag == FourCC("MOMT")) {
            momt = body;

            // The group queries read the materials through CMapObj, so keep a copy that outlives
            // the root file buffer
            if (sz >= sizeof(SMOMaterial)) {
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

    // The MOMT -> texture load used to sit here, filling WmoInstance::textures for the batch
    // array above. Both are gone: CMapObj::LoadMaterialTextures loads the real copy, so the
    // stand-in was loading every WMO material texture a second time and sampling neither.

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

                // The stand-in used to build its OWN CMapObjGroup here -- a second copy of the
                // MOPY polys, the MOBN BSP nodes, the MOBR face refs, the MOCV colours and a
                // file-space vertex array -- purely so the ported BSP queries had something to walk.
                // TerrainWmoFloorLightAt now runs those queries on the REFERENCE groups, which carry
                // the same data from the same chunks, so all of it is gone: per group that is one
                // BSP node array, one poly array, one face-ref array and two vertex-sized arrays.

                // The WmoBatch array is gone. It carried each batch's texture, blend mode and
                // material flags for RenderWmos, which was deleted once the reference pass took
                // over the WMO draw -- so nothing has read a batch since. Only the COUNT is still
                // live: BlobShadowDrawWmo and WmoUpdateVisibility gate on batchCount to skip a
                // group with no drawable geometry, so MOBA is still counted.
                grp.batchCount = mobaCount;

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
    tile.wmos = nullptr;
    tile.wmoCount = 0;

    for (auto& chunk : tile.chunks) {
        chunk.valid = false;
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

    // The MTEX ground textures used to be loaded here, one HTEXTURE per name per tile. They were
    // sampled only by RenderShaded and RenderFallback; with those gone nothing read them, and
    // CMapRenderChunk loads the real ones itself. The names are still collected because the MTEX
    // walk is part of the chunk scan, but no texture is created from them any more.

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

    // The tile's own doodads (MDDF) are the map's now, placed by CMap::CreateDoodadDef out of
    // CMapChunk::CreateRefs. Nothing is read here, so nothing is allocated for them either.

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

    for (auto& chunk : tile.chunks) {
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
                if (grp.ndotl) SMemFree(grp.ndotl, __FILE__, __LINE__, 0);
                if (grp.mocvAdd) SMemFree(grp.mocvAdd, __FILE__, __LINE__, 0);
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
        }

        SMemFree(tile.wmos, __FILE__, __LINE__, 0);
        tile.wmos = nullptr;
    }

    tile.wmoCount = 0;

    for (auto& chunk : tile.chunks) {

        chunk.valid = false;
    }


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
// The terrain half is the map's now (CMap::GetTerrainLiquid, which reads the real MH2O
// per-tile mask and interpolates the layer's own surface). The WMO half below is still the
// stand-in's: the reference keeps interior pools and canals in a separate query
// (FUN_007a09d0, over the group BSP) that frozen has not ported, and dropping this loop would
// stop the client noticing them at all.
int32_t LiquidAt(const C3Vector& pos, float& surfaceZ) {
    int32_t found = -1;
    surfaceZ = 0.0f;

    {
        uint32_t liquidType = 0;
        float height = 0.0f;

        if (CMap::GetTerrainLiquid(pos, &liquidType, &height, 0)) {
            auto rec = g_liquidTypeDB.GetRecord(static_cast<int32_t>(liquidType));

            found = rec ? rec->m_type : 0;
            s_cameraLiquidType = static_cast<int32_t>(liquidType);
            surfaceZ = height;
        }
    }

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

    // Liquid under the camera. CWorldScene::UpdateCameraLiquid answers the outdoor half from
    // CMap::Render now and keeps the depth with it; this still asks LiquidAt as well, because
    // only LiquidAt knows about the WMO pools the scene's version cannot see yet, and because
    // the stand-in sky and overlay read the kind rather than the type.
    {
        float surfaceZ;
        s_cameraLiquidType = -1;
        s_cameraLiquidKind = LiquidAt(cameraPos, surfaceZ);
        CWorld::SetCameraUnderLiquid(s_cameraLiquidKind >= 0);

        // The sky moved to DayNight.cpp and keeps its own copies of these two; pushing them from
        // here is what makes that a pure relocation -- same values, same moment in the frame.
        SkySetCameraState(cameraPos, s_cameraLiquidKind);
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

        // The exterior WMO groups used to be re-lit here on every light change, writing
        // WmoGroup::colors. Nothing draws those colours any more -- the reference pass lights its
        // own groups -- so the work produced nothing and is gone with the arrays it wrote.
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


    // The terrain chunks draw through the ported map (CMap::Render -> CWorldScene::RenderTerrain)
    // since 2026-09-25, and the two stand-in passes that used to draw them are gone. What still
    // reads this copy of the chunk geometry is the blob shadow receiver walk, which re-draws a
    // chunk's own triangles under a decal; retiring that is what finally frees the tile data.
    (void)haveShaded;

    // The buildings draw through the ported map (CMap::Render -> CWorldScene::RenderMapObjs)
    // since 2026-09-25. The stand-in's own visibility sweep stays: the floor light, the indoor
    // test and the blob shadow receivers all read the instance list it fills.
    WmoUpdateVisibility(s_cameraPos);

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
    GxRsSet(GxRs_Texture0, tex ? TextureGetGxTex(tex, 0, nullptr) : (SkyWhiteTexture() ? TextureGetGxTex(SkyWhiteTexture(), 0, nullptr) : nullptr));
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
// The reference's answer is now known and is NOT this: `FUN_007e4480` takes the strength as its
// third argument, and the call site at 0x007e4a27 passes a CONSTANT 0.4 (the float at 0x009f98d8).
// No time of day, no light ratio. Decoded 2026-09-23; see docs/ref/parity-shadows.md.
//
// Deliberately not swapped for 0.4 yet. What `FUN_007e4370` does with that scalar has not been
// read, so it is not yet known whether 0.4 means the same thing as the value below -- this one is
// the amount of light REMOVED, and blob_decal_ps.hlsl emits `1 - coverage`. Substituting a number
// whose sense is unconfirmed, on a path that cannot be looked at right now, would be guessing at
// the screen rather than porting. It wants the emit read first, then one change and one run.
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

// How dark a blob is. A constant at the reference's own call site, not a light ratio.
// DAT_009f98d8
static const float BLOB_SHADOW_STRENGTH = 0.4f;

// ref: FUN_007e49e0
// The gate every blob caster passes through. A model opts out through its own flag, a box with
// no extent casts nothing, and the whole system stands down when the extended shadow quality is
// turned up -- with real exterior shadows on, blobs would double up on them.
void BlobShadowDrawCaster(CM2Model* model, const CAaBox& box) {
    if (!model || !model->IsDrawable(0, 0) || model->m_flag4000) {
        return;
    }

    if (AaBoxIsDegenerate(box)) {
        return;
    }

    if (CWorldParam::cvar_extShadowQuality && CWorldParam::cvar_extShadowQuality->GetInt() >= 1) {
        return;
    }

    if (!s_blobActive) {
        return;
    }

    // The box is in the model's own space, and the model's matrix places and scales it. The
    // reference turns the two half-extents into an oriented rectangle and spins it with that
    // matrix; frozen's draw still takes a centre and one radius and lays down a circle, so the
    // footprint is round where the reference's is rectangular, and a long thin caster comes out
    // as wide as it is long. Recorded as a divergence on BlobShadowDraw.
    const C44Matrix& placement = model->matrixB4;

    float scale = sqrtf(placement.a0 * placement.a0
                      + placement.a1 * placement.a1
                      + placement.a2 * placement.a2);

    C3Vector local = {
        (box.b.x + box.t.x) * 0.5f,
        (box.b.y + box.t.y) * 0.5f,
        (box.b.z + box.t.z) * 0.5f
    };

    C3Vector centre = {
        placement.a0 * local.x + placement.b0 * local.y + placement.c0 * local.z + placement.d0,
        placement.a1 * local.x + placement.b1 * local.y + placement.c1 * local.z + placement.d1,
        placement.a2 * local.x + placement.b2 * local.y + placement.c2 * local.z + placement.d2
    };

    float ex = (box.t.x - box.b.x) * 0.5f;
    float ey = (box.t.y - box.b.y) * 0.5f;
    float radius = (ex > ey ? ex : ey) * scale;

    if (radius <= 0.0f) {
        return;
    }

    BlobShadowDraw(centre, radius);

    // BlobShadowDrawWmo used to follow, putting the same decal on building floors. It was removed
    // on 2026-09-26 because it had stopped being able to draw anything, silently, when the WMO draw
    // moved to the reference pass.
    //
    // The decal works by re-drawing the receiver's own triangles and selecting with a depth-EQUAL
    // test (GxRs_DepthFunc = 1 in BlobShadowsBegin), so it only lands where the re-draw reproduces
    // the receiver's depth EXACTLY. For terrain that holds: the same terrain vertex program, the
    // same c0..c3 constants, the same local vertices. For a WMO it no longer does --
    // CWorldScene::RenderMapObjs draws a group through CShaderEffect with its world-view at c31 and
    // the group's FILE-space vertices, where this pass used s_terrainVS at c0 over the stand-in's
    // yawed, origin-rebased copy. Different vertices through a different program cannot agree in
    // the low bits of z, so every pixel failed the EQUAL test.
    //
    // Bringing them back means the reference's own method, which is not this: FUN_007e4480 builds a
    // texture PROJECTION matrix and lets the receiver draw itself with an extra stage, which is why
    // the reference can shadow any receiver regardless of its shader. That is recorded as the
    // divergence on FUN_007e4480 in overrides.json.
}

void BlobShadowsEnd() {
    if (s_blobActive) {
        GxRsPop();
        s_blobActive = false;
    }
}

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

void TerrainForEachDoodad(void (*fn)(CM2Model* model, void* arg), void* arg) {
    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
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

// The light for a unit standing on a WMO floor, by the reference's mechanism: a probe from one
// yard above its feet to twelve below, through the interior groups' BSP, sampling the MOCV at the
// floor face it lands on (CMapEntity::FloorLight, FUN_007a0d60). The reference reaches the
// entity's MapObjDef and group through its parent links; without that graph every loaded instance
// whose box holds the probe is tried, and within it every interior group whose box holds it, first
// hit wins. Exterior groups never answer, so a unit out on a deck is lit by the sky again.
bool TerrainWmoFloorLightAt(const C3Vector& pos, CImVector* diffuse, CImVector* ambient) {
    // Runs on the REFERENCE map objects, not the stand-in's copies. CMapEntity::FloorLight was
    // always a ported reference function taking a CMapObj and a CMapObjGroup -- the stand-in was
    // only supplying its own instances of those two, built out of its own arrays. The real ones
    // carry the same MOBN/MOBR BSP and the same MOCV colours, and the portal walk that reaches
    // them has worked since the m_portalRects fix, so this is a redirect rather than a rewrite.
    for (auto def = CMapObjDef::s_uniqueIds.Head(); def;
         def = CMapObjDef::s_uniqueIds.Next(def)) {
        if (!def->m_mapObj) {
            continue;
        }

        // The whole building first, in world space, with the same asymmetric vertical window the
        // stand-in used: a probe reaches a little above the unit and well below it, because the
        // floor being stood on is what is wanted.
        const CAaBox& box = def->m_bounds;

        if (pos.x < box.b.x || pos.x > box.t.x || pos.y < box.b.y || pos.y > box.t.y
            || pos.z + 1.0f < box.b.z || pos.z - 12.0f > box.t.z) {
            continue;
        }

        // Into the building's own space, where the BSP planes and the group bounds live. The
        // stand-in undid the placement by hand from a yaw sine and cosine; the def carries the
        // whole inverse, which also covers the pitch and roll a hand-rolled yaw could not.
        C3Vector local = pos * def->m_inversePlacement;

        for (auto link = def->m_defGroupLinkList.Head(); link;
             link = def->m_defGroupLinkList.Next(link)) {
            auto defGroup = static_cast<CMapObjDefGroup*>(link->owner);

            if (!defGroup) {
                continue;
            }

            uint32_t groupIndex = defGroup->m_groupIndex;
            CMapObjGroup* group = def->m_mapObj->GetGroup(groupIndex, 0);

            // A group with no BSP or no vertex colours cannot answer the probe.
            if (!group || !group->m_bspNodes || !group->m_colors) {
                continue;
            }

            const CAaBox& gb = group->m_bounds;

            if (local.x < gb.b.x || local.x > gb.t.x || local.y < gb.b.y || local.y > gb.t.y
                || local.z + 1.0f < gb.b.z || local.z - 12.0f > gb.t.z) {
                continue;
            }

            uint32_t flags = 0;
            uint8_t alpha = 0;

            if (CMapEntity::FloorLight(local, def->m_mapObj, group, diffuse, ambient, &flags,
                                       &alpha)) {
                return true;
            }
        }
    }

    return false;
}

