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

    // Raw MCNK ground-effect inputs, still parsed out of the chunk: per 8x8 cell the dominant
    // layer (predTex, 2 bits per cell) and the cells that get no doodad at all. The map's own
    // scatter reads these from CMapChunk instead, so nothing here consumes them any more.

    bool valid = false;
};


struct TerrainTile {
    int32_t x = -1;
    int32_t y = -1;
    bool loaded = false;
    TerrainChunk chunks[256];

    // Placement uniqueIds this tile loaded (a large object listed in several tiles is owned by the
    // first tile to load it; the rest skip it). Removed from the global registry on unload.

    // WMO building instances placed on this tile (MODF)

    bool needsRebake = false; // outdoor light changed; relight this tile's vertices (spread over frames)
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

// Load a WMO (root + group files), transform all group geometry into world space using the
// placement derived from the MODF entry, and build per-material textured batches. The placement
// convention (local X is up; local Y/Z are the horizontal plane rotated by the yaw) was verified
// against the MODF world-space bounding box.

// Read one ADT and keep only what is still wanted from it: the per-chunk height, colour and
// texcoord grids the terrain blob-shadow receiver re-draws under a decal.
//
// Everything else this used to take out of a tile has moved to the map. MTEX ground textures,
// MH2O liquid, MDDF doodads and the whole MODF/MWMO/MWID WMO path are all CMapArea's and
// CMapChunk's now, so the chunk scan looks at MCIN and nothing else.
void LoadTile(TerrainTile& tile, int32_t tileX, int32_t tileY) {
    tile.x = tileX;
    tile.y = tileY;
    tile.loaded = true;

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
    const uint8_t* mcin = nullptr;
    uint32_t offset = 0;

    while (offset + 8 <= size) {
        uint32_t chunkSize;
        uint32_t tag = ReadChunkTag(bytes, offset, chunkSize);

        if (tag == FourCC("MCIN")) {
            mcin = bytes + offset + 8;
        }

        offset += 8 + chunkSize;
    }

    if (mcin) {
        for (int32_t i = 0; i < 256; i++) {
            uint32_t mcnkOffset = *reinterpret_cast<const uint32_t*>(mcin + i * 16);

            if (mcnkOffset && mcnkOffset + 8 <= size) {
                uint32_t mcnkSize = *reinterpret_cast<const uint32_t*>(bytes + mcnkOffset + 4);
                ParseChunk(tile.chunks[i], bytes + mcnkOffset + 8, mcnkSize);
            }
        }
    }

    SMemFree(data, __FILE__, __LINE__, 0);
}

void FreeTile(TerrainTile& tile) {
    // Release this tile's claimed placement ids so a neighbour can own them when it next loads


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

    if (!s_weatherAlive || CWorld::IsCameraUnderLiquid()) {
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
        // The camera liquid is CWorldScene::UpdateCameraLiquid's now, terrain and indoor halves
        // both, and it sets CWorld::SetCameraUnderLiquid itself.
        SkySetCameraState(cameraPos);
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
    // Both the gate and the liquid kind come from the reference camera-liquid query now
    // (CWorldScene::UpdateCameraLiquid), which records the LiquidType row the camera is submerged
    // in. 0 means not submerged.
    int32_t cameraLiquidType = static_cast<int32_t>(CWorldScene::s_cameraLiquidType);

    if (!cameraLiquidType) {
        return;
    }

    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    // The surface texture of the liquid the camera is in, scrolled slowly for the caustic look.
    // LiquidAt recorded the exact type when it decided the camera was submerged, so there is no
    // need to walk every tile and chunk again looking for a surface of the same kind.
    LiquidTextures* set = GetLiquidTextures(cameraLiquidType);
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

