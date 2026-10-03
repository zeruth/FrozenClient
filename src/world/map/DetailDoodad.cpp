#include "world/map/DetailDoodad.hpp"
#include "world/ShadowMap.hpp"
#include "world/DayNightLight.hpp"
#include "world/map/CMapLight.hpp"
#include "model/CM2Lighting.hpp"
#include "gx/shader/CGxShader.hpp"
#include "gx/Gx.hpp"
#include "world/CWorldParam.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMap.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/buffer/CGxPool.hpp"
#include "gx/Buffer.hpp"
#include "gx/RenderState.hpp"
#include "gx/CGxCaps.hpp"
#include "gx/Transform.hpp"
#include "world/CWorld.hpp"
#include <tempest/Matrix.hpp>
#include <storm/Memory.hpp>
#include <new>
#include <tempest/Random.hpp>
#include <cstring>
#include <cmath>
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "world/CWorld.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Model.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <new>
#include "gx/Buffer.hpp"
#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/buffer/CGxPool.hpp"

namespace DetailDoodad {

TSGrowableArray<CDoodadModel*> s_models;
uint32_t s_perChunk = 0;
uint32_t s_vertexBytes = 0;
uint32_t s_indexCount = 0;
CGxPool* s_vertexPool = nullptr;
CGxPool* s_indexPool = nullptr;
TSGrowableArray<CGxBuf*> s_buffers;
int32_t s_rebuild = 1;
int32_t s_useShaders = 0;
float s_fadeDistance = 70.0f;
HTEXTURE s_fadeTexture = nullptr;

// DAT_00cd766c: the grass alpha cutoff, 0x80 unless a CVar frozen does not register moves it.
// Set after the blending mode, which would otherwise have just put the device default there.
static uint32_t s_alphaRef = 0x80;

// The module's own shaders, loaded by Initialize when the world has both shader kinds: the
// vertex programs in two triples by shadow level (DAT_00d1c4a8) and the pixel programs by shadow
// level and hardware PCF (DAT_00d1c488).
static CGxShader* s_vertexShaders[6];
static CGxShader* s_pixelShaders[8];

// The 23 vertex registers the frame setup fills and every chunk sends (DAT_00d1c518):
//   c0-c3 placement x view (per chunk)   c4-c7 projection       c8  fog ramp
//   c9    distance fade                  c10   view-space sun   c11 ambient
//   c12   diffuse                        c13   specular         c14-c22 three point lights
// Read off the shipped vs_3_0 DetailDoodad.bls, which consumes c0-c12 exactly so.
static float s_shaderConstants[23][4];

static void FadeTextureCallback(EGxTexCommand, uint32_t, uint32_t, uint32_t,
                                uint32_t, void*, uint32_t&, const void*&);

// How many the density setting scatters per chunk before the ceiling applies.
static const uint32_t PER_DENSITY_UNIT = 0x40;

// ref: FUN_007b29b0
void ReleaseBuffers() {
    for (uint32_t i = 0; i < s_buffers.Count(); i++) {
        if (s_buffers[i]) {
            // TODO the reference hands each buffer back through CGxDevice::BufStream and drops
            // it from the chunk's block array when the device returns a different one. Frozen
            // has neither the block array nor that path, so the buffers are simply forgotten.
            s_buffers[i] = nullptr;
        }
    }

    s_buffers.SetCount(0);

    // TODO the reference gives both pools back through a device method frozen has not mapped
    // (vtable slot 0xd4). Dropping the pointers without it would leak the pool; keeping them
    // means a rebuild reuses the pools it already has, which is wrong only if the density
    // changes mid-session. Left pointing at them deliberately until the device method is
    // identified -- the alternative is a guess about which slot frees a pool.
}

// ref: FUN_007b2a80
// One pool for vertices and one for indices, each big enough for the whole ring, and the ring
// carved out of them at fixed offsets. Nothing is allocated per chunk afterwards: a chunk that
// wants to scatter takes the next pair and writes over whatever was there.
void CreateBuffers() {
    if (!s_rebuild) {
        return;
    }

    // TODO FUN_0079e730 runs first. Not identified.

    ReleaseBuffers();

    uint32_t density = CWorldParam::cvar_groundEffectDensity
        ? static_cast<uint32_t>(CWorldParam::cvar_groundEffectDensity->GetInt())
        : 0;

    s_perChunk = density * PER_DENSITY_UNIT;

    if (s_perChunk > MAX_PER_CHUNK) {
        s_perChunk = MAX_PER_CHUNK;
    }

    s_vertexBytes = s_perChunk * VERTEX_STRIDE;

    // THE INDEX CAPACITY IS s_perChunk, NOT s_perChunk * 2, and getting that wrong overflowed the
    // index buffer by up to 2x and crashed in DrawBatch a few seconds after world entry.
    //
    // The reference keeps two globals here and only one of them means anything. DAT_00d1c4cc holds
    // s_perChunk and is read in exactly two places -- BufCreate's element count below, and the room
    // check in AddPlacement -- so the batch can never accumulate more indices than its buffer
    // holds. _DAT_00d1c4c4 holds s_perChunk * 2 and, checked against the whole .text, is WRITTEN
    // three times and never read: it is dead. This was ported against the dead one.
    s_indexCount = s_perChunk;

    uint32_t pairs = BUFFER_COUNT / 2;

    if (!s_vertexPool) {
        s_vertexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic,
                                                    s_perChunk * VERTEX_STRIDE * pairs,
                                                    GxPoolHintBit_Unk3, "CDetailDoodad_vtx");
    }

    if (!s_indexPool) {
        s_indexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Dynamic,
                                                   s_perChunk * 2 * pairs,
                                                   GxPoolHintBit_Unk0, "CDetailDoodad_idx");
    }

    if (!s_vertexPool || !s_indexPool) {
        return;
    }

    s_buffers.Reserve(BUFFER_COUNT, 1);

    for (uint32_t i = 0; i < pairs; i++) {
        auto vertexBuf = GxBufCreate(s_vertexPool, VERTEX_STRIDE, s_perChunk,
                                     i * s_perChunk * VERTEX_STRIDE);
        s_buffers.Add(1, &vertexBuf);

        auto indexBuf = GxBufCreate(s_indexPool, 2, s_indexCount, i * s_indexCount * 2);
        s_buffers.Add(1, &indexBuf);
    }

    s_rebuild = 0;
}

// ref: FUN_007b2760
// The table is as long as the highest id the DBC carries, so a kind can be looked up by its id
// with no search; the ids it does not use stay null.
void Initialize() {
    // TODO the reference also makes the "WDETAILDOODADINST" object heap the scattered instances
    // come from, and loads the module's own shaders. Frozen's stand-in already holds a detail
    // pixel shader; the heap waits for the scatter builder that would use it.
    //
    // s_useShaders stays 0 for exactly that reason. The reference sets it from a device setting
    // and then loads Shaders\Vertex\DetailDoodad and Shaders\Pixel\DetailDoodad only if it is
    // on, so with no shaders loaded the vertex fill has to bake the brightness ramp into the
    // vertex colour itself. Setting it without loading them would drop the ramp entirely.
    s_useShaders = 0;

    // The fixed-function fade ramp. 64 by 8 so the device will filter it, ARGB so the alpha is
    // the ramp and the colour stays white.
    if (!s_fadeTexture) {
        s_fadeTexture = TextureCreate(64, 8, GxTex_Argb8888, GxTex_Argb8888, CGxTexFlags(),
                                      nullptr, FadeTextureCallback, __FILE__, __LINE__);
    }

    // The reference takes the highest id straight off the DBC; frozen's WowClientDB keeps that
    // private, so it is found by looking, which comes to the same table.
    int32_t maxId = -1;

    for (int32_t i = 0; i < g_groundEffectDoodadDB.GetNumRecords(); i++) {
        auto rec = g_groundEffectDoodadDB.GetRecordByIndex(i);

        if (rec && rec->m_ID > maxId) {
            maxId = rec->m_ID;
        }
    }

    uint32_t count = maxId < 0 ? 0 : static_cast<uint32_t>(maxId) + 1;

    s_models.SetCount(count);

    for (uint32_t i = 0; i < count; i++) {
        s_models[i] = nullptr;
    }

    for (int32_t i = 0; i < g_groundEffectDoodadDB.GetNumRecords(); i++) {
        auto rec = g_groundEffectDoodadDB.GetRecordByIndex(i);

        if (!rec || rec->m_ID < 0) {
            continue;
        }

        auto entry = static_cast<CDoodadModel*>(
            SMemAlloc(sizeof(CDoodadModel), __FILE__, __LINE__, 0x0));

        if (!entry) {
            continue;
        }

        new (entry) CDoodadModel();

        entry->m_rec = rec;
        s_models[rec->m_ID] = entry;
    }

    // The module's shaders (0x007b28c0 on): on when the world has pixel shaders, off without
    // vertex shaders, and loaded only when on. With them the vertex colour keeps its alpha and
    // the program applies the brightness ramp (ShadeVertexColor stops baking it in).
    for (auto& shader : s_vertexShaders) {
        shader = nullptr;
    }

    for (auto& shader : s_pixelShaders) {
        shader = nullptr;
    }

    s_useShaders = (CWorld::s_enables & CWorld::Enables::Enable_PixelShader) != 0;

    if (!(CWorld::s_enables2 & CWorld::Enables2::Enable_VertexShader)) {
        s_useShaders = 0;
        return;
    }

    if (s_useShaders) {
        g_theGxDevicePtr->ShaderCreate(s_vertexShaders, GxSh_Vertex, "Shaders\\Vertex", "DetailDoodad", 6);
        g_theGxDevicePtr->ShaderCreate(s_pixelShaders, GxSh_Pixel, "Shaders\\Pixel", "DetailDoodad", 8);
    }
}

// ref: FUN_007b15d0
// The frame's share of the module's vertex constants, the same shape as the terrain's
// (CWorldScene::SetupTerrainConstants): the projection, the outdoor light in view space, the
// fog ramp, the distance fade, and the fog colour to the pixel program. Ghidra names this
// GxXformSet; it is not.
static void SetupShaderConstants() {
    memset(s_shaderConstants, 0, sizeof(s_shaderConstants));

    const C44Matrix& view = g_theGxDevicePtr->m_xforms[GxXform_View].Top();

    // c0-c3 start as the identity; every chunk writes its own placement over them.
    s_shaderConstants[0][0] = 1.0f;
    s_shaderConstants[1][1] = 1.0f;
    s_shaderConstants[2][2] = 1.0f;
    s_shaderConstants[3][3] = 1.0f;

    // c4-c7, the native projection. The reference negates its third row unless the API is
    // D3D; frozen keeps it as the terrain pass does (see SetupTerrainConstants).
    memcpy(&s_shaderConstants[4][0], &g_theGxDevicePtr->m_projNative, sizeof(C44Matrix));

    C3Vector sunDir = { 0.0f, 0.0f, 0.0f };
    CM2Lighting lighting;
    CAaSphere origin = { sunDir, 0.0f };
    lighting.Initialize(nullptr, origin);
    lighting.AddLight(&CMap::s_outdoorLight->m_light);

    auto ambient = reinterpret_cast<C3Vector*>(s_shaderConstants[11]);
    auto diffuse = reinterpret_cast<C3Vector*>(s_shaderConstants[12]);
    auto specular = reinterpret_cast<C3Vector*>(s_shaderConstants[13]);

    if (!lighting.GetSunlight(&sunDir, ambient, diffuse, specular)) {
        s_shaderConstants[10][0] = 0.0f;
        s_shaderConstants[10][1] = 0.0f;
        s_shaderConstants[10][2] = 0.0f;
        s_shaderConstants[10][3] = 1.0f;

        for (int32_t i = 0; i < 4; i++) {
            s_shaderConstants[11][i] = 1.0f;
            s_shaderConstants[12][i] = 0.0f;
            s_shaderConstants[13][i] = 0.0f;
        }
    } else {
        float x = -sunDir.x;
        float y = -sunDir.y;
        float z = -sunDir.z;
        s_shaderConstants[10][0] = view.a0 * x + view.b0 * y + view.c0 * z;
        s_shaderConstants[10][1] = view.a1 * x + view.b1 * y + view.c1 * z;
        s_shaderConstants[10][2] = view.c2 * z + view.b2 * y + view.a2 * x;
        s_shaderConstants[10][3] = 1.0f;
        s_shaderConstants[13][3] = 0.0f;
    }

    // Three point lights, each three registers with a 1 at the start of the third.
    for (int32_t light = 0; light < 3; light++) {
        float* reg = s_shaderConstants[14 + light * 3];
        memset(reg, 0, sizeof(float) * 12);
        reg[8] = 1.0f;
    }

    if (!GxMasterEnable(GxMasterEnable_Fog)) {
        s_shaderConstants[8][0] = 0.0f;
        s_shaderConstants[8][1] = 1.0f;
        s_shaderConstants[8][2] = 1.0f;
        s_shaderConstants[8][3] = 0.0f;

        if (GxCaps().m_notPs30b) {
            GxRsSet(GxRs_Fog, 0);
        }
    } else {
        auto block = DayNightGetBlock();
        float inv = 1.0f / (block->fogEnd - block->fogStart);
        s_shaderConstants[8][0] = -(g_shadowMapFogScale * inv);
        s_shaderConstants[8][1] = inv * block->fogEnd;
        s_shaderConstants[8][2] = block->fogRate;
        s_shaderConstants[8][3] = 0.0f;

        if (!GxCaps().m_notPs30a) {
            // The fog colour, with the alpha cutoff in the fourth float. The reference scales
            // the cutoff by 255.0 (0x009e30c0), not by 1/255; kept as it is.
            const CImVector& fog = block->fogColor;
            float color[4] = {
                fog.r / 255.0f,
                fog.g / 255.0f,
                fog.b / 255.0f,
                static_cast<float>(s_alphaRef) * 255.0f
            };
            GxShaderConstantsSet(GxSh_Pixel, 2, color, 1);
        } else {
            GxRsSet(GxRs_FogColor, block->fogColor.value);
        }

        if (GxCaps().m_notPs30b) {
            GxRsSet(GxRs_Fog, 1);
        }
    }

    // c9, the distance fade: solid to 85% of the fade distance, gone at 100%.
    float fade = 1.0f / (s_fadeDistance - s_fadeDistance * 0.85f);
    s_shaderConstants[9][0] = -1.0f * fade;
    s_shaderConstants[9][1] = fade * s_fadeDistance;
    s_shaderConstants[9][2] = 0.0f;
    s_shaderConstants[9][3] = 0.0f;
}

// ref: FUN_007b10e0
void SetupChunkShader(const C44Matrix& placement, int32_t variant) {
    int32_t level = ShadowMapGetShaderLevel();

    if (level > 2) {
        level = 2;
    }

    GxRsSet(GxRs_VertexShader, s_vertexShaders[variant * 3 + level]);

    const C44Matrix& view = g_theGxDevicePtr->m_xforms[GxXform_View].Top();
    C44Matrix placed = placement * view;
    memcpy(&s_shaderConstants[0][0], &placed, sizeof(C44Matrix));

    GxShaderConstantsSet(GxSh_Vertex, 0, &s_shaderConstants[0][0], 23);
}

// ref: FUN_007b3050
// The DBC gives a name relative to one folder, so the path is that folder and the name.
bool EnsureModel(CDoodadModel* entry) {
    if (entry->m_model) {
        return true;
    }

    char path[260];

    uint32_t n = SStrCopy(path, "World\\NoDXT\\Detail\\", sizeof(path));
    SStrCopy(path + n, entry->m_rec->m_doodadPath, sizeof(path) - n);

    auto scene = CWorld::GetM2Scene();

    entry->m_model = scene ? scene->CreateModel(path, 0) : nullptr;

    if (!entry->m_model) {
        return false;
    }

    entry->m_model->SetLoadedCallback(OnModelLoaded, entry);

    return true;
}

// ref: FUN_007b1b10
// The reference hangs this off CM2Model::SetLoadedCallback, so the texture is resolved exactly
// once, when the model arrives. Only the first texture: a detail doodad is one sheet of grass.
void OnModelLoaded(CM2Model* model, void* param) {
    auto entry = static_cast<CDoodadModel*>(param);

    if (!entry->m_model->IsLoaded(0, 0)) {
        entry->m_model->WaitForLoad(0);
    }

    auto data = entry->m_model->m_shared->m_data;

    entry->m_texture = CMap::LoadTexture(data->textures[0].filename.Data());
}

// ref: FUN_007b3530
// Asking is what starts the load, so a chunk that keeps asking will eventually be told yes.
bool IsReady(int32_t doodadId) {
    if (doodadId < 0 || static_cast<uint32_t>(doodadId) >= s_models.Count()) {
        return false;
    }

    auto entry = s_models[doodadId];

    if (!entry) {
        return false;
    }

    if (entry->m_model) {
        return entry->m_model->IsLoaded(0, 0) != 0;
    }

    EnsureModel(entry);

    return false;
}

// One cell of the chunk's eight-by-eight grid, prepared once before anything is placed on it.
struct SCell {
    C4Plane plane[4];       // the four triangles, in the chunk's own space
    float base[4][3];       // the centre vertex's colour, doubled as the reference doubles it
    float toA[4][3];        // and how it changes towards each of the triangle's two corners
    float toB[4][3];
};

// Part of ref: FUN_007d3390
// Everything about a cell that every doodad standing on it needs: its four triangles as planes,
// and the colour at the centre with the slope towards each corner.
static void PrepareCell(CMapChunk* chunk, uint32_t row, uint32_t col, SCell* cell) {
    float baseX = static_cast<float>(row) * CELL_STEP;
    float baseY = static_cast<float>(col) * CELL_STEP;

    float midX = baseX + CELL_MID;
    float midY = baseY + CELL_MID;

    const float* heights = chunk->m_heights + (row * 17 + col);
    const uint32_t* colors = chunk->m_vertexColors ? chunk->m_vertexColors + (row * 17 + col) : nullptr;

    float hm = heights[9];

    for (uint32_t t = 0; t < 4; t++) {
        uint32_t ca = TRI_CORNER[t][0];
        uint32_t cb = TRI_CORNER[t][1];
        uint32_t va = TRI_VERTEX[t][0];
        uint32_t vb = TRI_VERTEX[t][1];

        float ax = baseX + CELL_CORNER[ca][0];
        float ay = baseY + CELL_CORNER[ca][1];
        float bx = baseX + CELL_CORNER[cb][0];
        float by = baseY + CELL_CORNER[cb][1];

        float ha = heights[va];
        float hb = heights[vb];

        // The plane through the cell's centre and the triangle's two corners, the same one
        // CMapChunk::HeightAt solves.
        float nx = (hb - hm) * (ay - midY) - (ha - hm) * (by - midY);
        float ny = (ha - hm) * (bx - midX) - (hb - hm) * (ax - midX);
        float nz = (by - midY) * (ax - midX) - (bx - midX) * (ay - midY);

        // This winding puts the normal under the ground, and it comes out scaled by twice the
        // triangle's area. CMapChunk::HeightAt can leave both alone because its division
        // cancels them, but MIN_NORMAL_Z is a slope against a unit normal, so here the plane
        // has to be turned up the right way and normalized -- which is what PlaneFromPoints
        // hands back, and frozen does have that (CWorldScene.cpp); it takes three points where
        // this already holds a centre and two corners, so the normalize is written out.
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        float scale = len > 0.0f ? -1.0f / len : 0.0f;

        nx *= scale;
        ny *= scale;
        nz *= scale;

        cell->plane[t].n.x = nx;
        cell->plane[t].n.y = ny;
        cell->plane[t].n.z = nz;
        cell->plane[t].d = -(midX * nx + midY * ny + nz * hm);

        for (uint32_t k = 0; k < 3; k++) {
            cell->base[t][k] = 0.0f;
            cell->toA[t][k] = 0.0f;
            cell->toB[t][k] = 0.0f;
        }

        if (!colors) {
            continue;
        }

        // MCCV is packed blue, green, red, alpha, and the reference doubles every channel.
        const uint8_t* mid = reinterpret_cast<const uint8_t*>(colors + 9);
        const uint8_t* pa = reinterpret_cast<const uint8_t*>(colors + va);
        const uint8_t* pb = reinterpret_cast<const uint8_t*>(colors + vb);

        for (uint32_t k = 0; k < 3; k++) {
            cell->base[t][k] = static_cast<float>(mid[k]) * COLOR_DOUBLE;
            cell->toA[t][k] = static_cast<float>(static_cast<int32_t>(pa[k]) - static_cast<int32_t>(mid[k])) * COLOR_DOUBLE;
            cell->toB[t][k] = static_cast<float>(static_cast<int32_t>(pb[k]) - static_cast<int32_t>(mid[k])) * COLOR_DOUBLE;
        }
    }
}

// Turn a random word into the -1..1 the reference gets from it: the low mantissa bits make a
// float in 1..2, and the sign bit decides which way it is folded around 2.
static float SignedUnit(uint32_t r) {
    uint32_t bits = (r & 0x7fffff) | 0x3f800000;
    float f;

    memcpy(&f, &bits, sizeof(f));

    return (r & 0x80000000u) ? 2.0f - f : f - 2.0f;
}

// Part of ref: FUN_007d3390
// The picking and placing half of the scatter, without the instance and batch bookkeeping the
// reference wraps around it.
uint32_t Scatter(CMapChunk* chunk, CDetailDoodadData* instance) {
    if (!chunk->m_header || !chunk->m_heights || !chunk->m_layers || !chunk->m_header->nLayers) {
        return 0;
    }

    if (!chunk->DetailDoodadsReady()) {
        return 0;
    }

    uint32_t density = CWorldParam::cvar_groundEffectDensity
        ? static_cast<uint32_t>(CWorldParam::cvar_groundEffectDensity->GetInt())
        : 0;

    if (!density) {
        return 0;
    }

    if (density > MAX_PER_CHUNK) {
        density = MAX_PER_CHUNK;
    }

    // The chunk's own indices seed it, so it scatters the same way every time it loads.
    CRndSeed seed(static_cast<uint32_t>(chunk->m_indexX) << 16 | static_cast<uint32_t>(chunk->m_indexY));

    static SCell s_cells[64];
    uint8_t prepared[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    static uint8_t s_pickCol[MAX_PER_CHUNK];
    static uint8_t s_pickRow[MAX_PER_CHUNK];

    for (uint32_t i = 0; i < density; i++) {
        uint32_t col = CRandom::uint32(seed) & 7;
        uint32_t row = CRandom::uint32(seed) & 7;

        s_pickCol[i] = static_cast<uint8_t>(col);
        s_pickRow[i] = static_cast<uint8_t>(row);

        // A cell picked twice is prepared once; the second pick still scatters on it.
        if (prepared[row] & (1 << col)) {
            continue;
        }

        prepared[row] |= static_cast<uint8_t>(1 << col);

        PrepareCell(chunk, row, col, &s_cells[col + row * 8]);
    }

    uint32_t placed = 0;

    for (uint32_t i = 0; i < density; i++) {
        uint32_t col = s_pickCol[i];
        uint32_t row = s_pickRow[i];

        // A hole in the chunk, and there is no cell to stand on.

        if (HOLE_MASK[(col >> 1) + (row >> 1) * 4] & chunk->m_header->holes) {
            continue;
        }

        // Which of the chunk's layers the cell takes, two bits a column of the low quality
        // texture map.
        uint32_t layer = (chunk->m_lowQualityTextureMap[row] & TEXMAP_MASK[col]) >> TEXMAP_SHIFT[col];

        // TODO the chunk's predTex bit for this column overrides that choice in the reference;
        // which layer it selects instead is not established, so the map's answer stands.

        if (layer >= chunk->m_header->nLayers) {
            continue;
        }

        auto effect = g_groundEffectTextureDB.GetRecord(chunk->m_layers[layer].effectId);

        if (!effect) {
            continue;
        }

        // Deal the effect's four kinds into sixteen slots by weight, stepping thirteen at a
        // time so they interleave instead of clumping, then fill the rest round-robin.
        int32_t bag[16];
        uint32_t slot = 0;
        uint32_t dealt = 0;

        for (uint32_t k = 0; k < 16; k++) {
            bag[k] = 0;
        }

        for (uint32_t k = 0; k < 4; k++) {
            for (int32_t w = 0; w < effect->m_doodadWeight[k]; w++) {
                bag[slot & 15] = effect->m_doodadID[k];
                slot += 13;
                dealt++;
            }
        }

        for (; dealt < 16; dealt++) {
            bag[slot & 15] = effect->m_doodadID[dealt & 3];
            slot += 13;
        }

        uint32_t perCell = static_cast<uint32_t>(effect->m_amount);

        if (!perCell) {
            perCell = 8;
        }

        const SCell& cell = s_cells[col + row * 8];

        for (uint32_t n = 0; n < perCell; n++) {
            float ja = SignedUnit(CRandom::uint32(seed));
            float jb = SignedUnit(CRandom::uint32(seed));

            int32_t kind = bag[(n + i) & 15];

            if (!kind) {
                continue;
            }

            float ox = ja * CELL_HALF + CELL_HALF;
            float oy = jb * CELL_HALF + CELL_HALF;

            float x = -oy;
            float y = -ox;

            // Which of the cell's four triangles the point landed in, by its two diagonals.
            uint32_t tri = (y - x) < 0.0f ? 1u : 0u;

            if (((-y - CELL_SIZE_POS) - x) > 0.0f) {
                tri += 2;
            }

            const C4Plane& plane = cell.plane[tri];

            // Too steep to stand anything on.
            if (plane.n.z < MIN_NORMAL_Z || plane.n.z == 0.0f) {
                continue;
            }

            x -= static_cast<float>(row) * CELL_SIZE_POS;
            y -= static_cast<float>(col) * CELL_SIZE_POS;

            float z = -((y * plane.n.y + plane.n.x * x + plane.d) / plane.n.z);

            // The colour across the triangle: the larger jitter drives the corner term and what
            // is left of it the other.
            float w = fabsf(jb) > fabsf(ja) ? fabsf(jb) : fabsf(ja);
            float lead = fabsf(jb) > fabsf(ja) ? jb : ja;
            float t2 = 0.5f - lead * 0.5f;

            if ((y - x) < 0.0f) {
                t2 = 1.0f - t2;
            }

            t2 *= w;

            float c[3];

            for (uint32_t k = 0; k < 3; k++) {
                c[k] = cell.base[tri][k] + cell.toA[tri][k] * w + cell.toB[tri][k] * t2;

                if (c[k] > COLOR_MAX) {
                    c[k] = COLOR_MAX;
                }

                if (c[k] < 0.0f) {
                    c[k] = 0.0f;
                }
            }


            C3Vector position;

            position.x = x;
            position.y = y;
            position.z = z;

            float rotation = (SignedUnit(CRandom::uint32(seed)) + 1.0f) * TAU_HALF;
            float scale = SignedUnit(CRandom::uint32(seed)) * SCALE_JITTER + 1.0f;

            // White when the chunk carries no MCCV or the kind asks to ignore it (flag 0x2):
            // the reference's 0xffffff at 0x007d3c2b. Frozen used the zeroed cell colour, which
            // the fixed-function path never showed (its material diffuse was white) but the
            // module's shader multiplies by -- every chunk without MCCV drew its grass black.
            auto entry = s_models[kind];
            bool plain = !chunk->m_vertexColors || (entry && entry->m_rec && (entry->m_rec->m_flags & 0x2));

            uint32_t color = plain
                ? 0xffffffffu
                : 0xff000000u
                    | (static_cast<uint32_t>(c[2] + ROUND_BIAS) << 16)
                    | (static_cast<uint32_t>(c[1] + ROUND_BIAS) << 8)
                    | static_cast<uint32_t>(c[0] + ROUND_BIAS);

            // In the chunk's baked shadow (MCSH, 64x64 bits, a row of eight bytes per 64 cells
            // of the 33.3-yard chunk: 1.92 per yard) the alpha drops to 0, which the vertex
            // ramp (0.3a + 0.7) turns into 70% brightness. 0x007d3c68: each axis is the jitter
            // within the cell plus the cell's own origin, 4.1667 a cell.
            if (chunk->m_shadow) {
                int32_t sx = static_cast<int32_t>(lrintf((ox + static_cast<float>(col) * CELL_SIZE_POS) * 1.92f - 0.5f));
                int32_t sy = static_cast<int32_t>(lrintf((oy + static_cast<float>(row) * CELL_SIZE_POS) * 1.92f - 0.5f));

                if (chunk->m_shadow[(sx >> 3) + sy * 8] & (1 << (sx & 7))) {
                    color &= 0x00ffffffu;
                }
            }

            AddPlacement(instance, kind, position, rotation, scale, plane.n,
                         static_cast<uint16_t>(col + row * 8), color);

            placed++;
        }
    }

    return placed;
}

// ref: FUN_007b31e0
// Batches are keyed by TEXTURE, so grass of several kinds still draws in one call. A placement
// joins the first batch already on its texture that can still fit the model's vertices and
// indices inside one buffer of the ring; failing that it opens a free batch, and failing that
// the chunk is full and the doodad is dropped.
void AddPlacement(CDetailDoodadData* instance, int32_t doodadId, const C3Vector& position,
                  float rotation, float scale, const C3Vector& normal, uint16_t cell,
                  uint32_t color) {
    auto entry = s_models[doodadId];

    if (!entry->m_model->IsLoaded(0, 0)) {
        entry->m_model->WaitForLoad(0);
    }

    // The SKIN's leading magic is why the counts sit where an M2Array's offsets would.
    auto skin = entry->m_model->m_shared->skinProfile;

    uint32_t vertexCount = skin->vertices.Count();
    uint32_t indexCount = skin->indices.Count();

    uint32_t slot = 0;

    for (; slot < 4; slot++) {
        auto& batch = instance->m_batches[slot];

        if (batch.texture == entry->m_texture &&
            batch.vertexTotal + vertexCount < s_perChunk &&
            batch.indexTotal + indexCount < s_indexCount) {
            break;
        }
    }

    if (slot == 4) {
        for (slot = 0; slot < 4; slot++) {
            if (!instance->m_batches[slot].texture) {
                break;
            }
        }

        // All four taken and none of them ours: this chunk has no room left.
        if (slot == 4) {
            return;
        }

        instance->m_batches[slot].texture = entry->m_texture;
    }

    auto& batch = instance->m_batches[slot];

    auto placement = batch.placements.New();

    placement->cell = cell;
    placement->doodadId = doodadId;
    placement->position = position;
    placement->rotation = rotation;
    placement->scale = scale;
    placement->normal = normal;
    placement->color = color;

    batch.vertexTotal += vertexCount;
    batch.indexTotal += indexCount;
}

// ref: FUN_007b2ca0
// The ring is a free list of pairs built up front by CreateBuffers, taken from the end. When it
// is empty -- more chunks drawing at once than the ring was sized for -- the batch falls back
// to a streaming buffer of its own, which costs an allocation but never drops the draw.
void AcquireBuffers(uint32_t vertexCount, CGxBuf** vertexBuf, uint32_t indexCount,
                    CGxBuf** indexBuf) {
    uint32_t count = s_buffers.Count();

    if (!count) {
        *vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, VERTEX_STRIDE, vertexCount);
        *indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, indexCount);

        return;
    }

    *vertexBuf = s_buffers[count - 2];
    *indexBuf = s_buffers[count - 1];

    s_buffers.SetCount(count - 2);

    // Whatever the last chunk left in them is not this chunk's, so both have to be refilled.
    (*vertexBuf)->unk1C = 0;
    (*indexBuf)->unk1C = 0;
}

// ref: FUN_007b1000
// Turn a basis about its own third axis. The reference passes the axis in, and its one caller
// always passes +Z, so the general axis term collapses to the plain Z rotation it writes here.
static void RotateBasisAboutZ(float* out, const float* basis, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);

    out[0] = basis[0] * c + basis[3] * s;
    out[1] = basis[1] * c + basis[4] * s;
    out[2] = basis[2] * c + basis[5] * s;

    out[3] = basis[0] * -s + basis[3] * c;
    out[4] = basis[1] * -s + basis[4] * c;
    out[5] = basis[2] * -s + basis[5] * c;

    // The Z row is scaled by (1 - c) * axis.z^2 + c, which for a unit +Z axis is 1.
    out[6] = basis[6];
    out[7] = basis[7];
    out[8] = basis[8];
}

// Part of ref: FUN_007b1b50
// An orthonormal frame standing on the terrain triangle: the normal is the third row, and the
// second is the one perpendicular to it that leaves x alone -- the reference multiplies that
// component by a constant which is zero.
static void SlopeBasis(const C3Vector& n, float* basis) {
    float uy = n.z;
    float uz = -n.y;

    float len = sqrtf(uy * uy + uz * uz);
    float inv = len != 0.0f ? 1.0f / len : 0.0f;

    float ux = 0.0f;

    uy *= inv;
    uz *= inv;

    // row 0 = n x u
    basis[0] = n.y * uz - n.z * uy;
    basis[1] = n.z * ux - uz * n.x;
    basis[2] = uy * n.x - n.y * ux;

    basis[3] = ux;
    basis[4] = uy;
    basis[5] = uz;

    basis[6] = n.x;
    basis[7] = n.y;
    basis[8] = n.z;
}

// Part of ref: FUN_007b1b50
// Without the module's shaders the brightness ramp has to be baked in: every channel is scaled
// by a factor the colour's own alpha drives, from about 0.70 at alpha 0 to about 1.00 at 255,
// and the alpha is then thrown away.
static uint32_t ShadeVertexColor(uint32_t color) {
    if (!s_useShaders) {
        uint32_t m = (color >> 24) * 0x4c + 0xb332;

        uint32_t r = ((color & 0xff) * m) >> 16;
        uint32_t g = (((color >> 8) & 0xff) * m) >> 16;
        uint32_t b = (((color >> 16) & 0xff) * m) >> 16;

        color = 0xff000000u | (b << 16) | (g << 8) | r;
    }

    // The device wants the other byte order, so red and blue change places.
    if (g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba) {
        color = (color & 0xff00ff00u) | ((color & 0xff) << 16) | ((color >> 16) & 0xff);
    }

    return color;
}

// ref: FUN_007b1b50
// Every placement writes its model out whole: one vertex per entry of the skin's vertex list,
// carrying the TERRAIN's normal rather than the model's, so a blade of grass is lit by the
// ground it stands on. Two ways of placing it, chosen by the kind's own DBC flag: upright and
// merely turned, or stood up along the slope.
//
// The slope frames are cached four at a time. The reference keys that cache on the placement's
// cell -- the low two bits pick the slot and the rest identify the group -- so neighbouring
// blades on one cell share a frame instead of each building its own.
void FillVertexBuffer(SBatch* batch) {
    auto vertices = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(batch->vertexBuf));

    float frames[4][9];
    uint32_t frameGroup = 0xffff;
    uint8_t frameValid = 0;

    for (uint32_t i = 0; i < batch->placements.Count(); i++) {
        const auto& p = batch->placements[i];

        auto entry = s_models[p.doodadId];

        if (!entry->m_model->IsLoaded(0, 0)) {
            entry->m_model->WaitForLoad(0);
        }

        auto data = entry->m_model->m_shared->m_data;

        if (!entry->m_model->IsLoaded(0, 0)) {
            entry->m_model->WaitForLoad(0);
        }

        auto skin = entry->m_model->m_shared->skinProfile;

        uint32_t count = skin->vertices.Count();
        const uint16_t* lookup = skin->vertices.Data();
        const M2Vertex* modelVertices = data->vertices.Data();

        uint32_t color = ShadeVertexColor(p.color);

        const float* frame = nullptr;
        float c = 0.0f;
        float sn = 0.0f;

        if (entry->m_rec->m_flags & 0x1) {
            // Stood up along the slope, from a frame shared across the cell.
            uint32_t group = p.cell & 0xfc;

            if (group != frameGroup) {
                frameGroup = group;
                frameValid = 0;
            }

            uint32_t slot = p.cell & 3;

            if (!(frameValid & (1 << slot))) {
                float basis[9];

                SlopeBasis(p.normal, basis);
                RotateBasisAboutZ(frames[slot], basis, p.rotation);

                frameValid |= static_cast<uint8_t>(1 << slot);
            }

            frame = frames[slot];
        } else {
            // Upright, and merely turned about Z.
            c = cosf(p.rotation);
            sn = sinf(p.rotation);
        }

        for (uint32_t k = 0; k < count; k++) {
            const M2Vertex& v = modelVertices[lookup[k]];

            float x;
            float y;
            float z;

            if (frame) {
                x = v.position.x * frame[0] + v.position.y * frame[3] + v.position.z * frame[6];
                y = v.position.x * frame[1] + v.position.y * frame[4] + v.position.z * frame[7];
                z = v.position.x * frame[2] + v.position.y * frame[5] + v.position.z * frame[8];
            } else {
                x = v.position.x * c - v.position.y * sn;
                y = v.position.x * sn + v.position.y * c;
                z = v.position.z;
            }

            vertices[0] = x * p.scale + p.position.x;
            vertices[1] = y * p.scale + p.position.y;
            vertices[2] = z * p.scale + p.position.z;

            // The ground's normal, not the model's.
            vertices[3] = p.normal.x;
            vertices[4] = p.normal.y;
            vertices[5] = p.normal.z;

            memcpy(&vertices[6], &color, sizeof(color));

            vertices[7] = v.texcoord[0].x;
            vertices[8] = v.texcoord[0].y;

            vertices += 9;
        }
    }

    g_theGxDevicePtr->BufUnlock(batch->vertexBuf, 0);

    batch->vertexBuf->unk1C = 1;
}

// ref: FUN_007b12b0
// Every placement contributes its model's whole index list, shifted by however many vertices
// have been written before it, so one batch is one draw however many kinds it holds.
void FillIndexBuffer(SBatch* batch) {
    auto indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(batch->indexBuf));

    uint32_t written = 0;
    uint32_t vertexBase = 0;

    for (uint32_t i = 0; i < batch->placements.Count(); i++) {
        auto entry = s_models[batch->placements[i].doodadId];

        if (!entry->m_model->IsLoaded(0, 0)) {
            entry->m_model->WaitForLoad(0);
        }

        auto skin = entry->m_model->m_shared->skinProfile;

        uint32_t count = skin->indices.Count();
        const uint16_t* src = skin->indices.Data();

        for (uint32_t k = 0; k < count; k++) {
            indices[written++] = static_cast<uint16_t>(src[k] + vertexBase);
        }

        vertexBase += skin->vertices.Count();
    }

    g_theGxDevicePtr->BufUnlock(batch->indexBuf, 0);

    batch->indexBuf->unk1C = 1;
}

// ref: FUN_007b3390
// A batch is one texture, so it is one draw. Both buffers are filled lazily here rather than at
// scatter time: unk1C says whether the buffer already holds this batch's data, and unk1D
// whether the device still has it at all.
void DrawBatch(SBatch* batch) {
    if (!batch->texture) {
        return;
    }

    auto tex = TextureGetGxTex(batch->texture, 0, nullptr);

    if (!tex) {
        return;
    }

    // No pair yet, or the pair held is the device's STREAM buffers (pool +0xc, the usage, is 2):
    // the ring was empty when this batch took them, and whatever it streamed last frame has been
    // overwritten by every pass since, so it takes a pair again and refills. This used to test
    // the pool's stream cursor (unk1C) instead of its usage, so a batch that once fell back to
    // the stream kept drawing from it, unfilled -- garbage triangles in random colours whenever
    // enough grass was in view to run the ring dry.
    if (!batch->vertexBuf || batch->vertexBuf->m_pool->m_usage == GxPoolUsage_Stream) {
        AcquireBuffers(batch->vertexTotal, &batch->vertexBuf, batch->indexTotal,
                       &batch->indexBuf);
    }

    if (!batch->vertexBuf->unk1C || !batch->vertexBuf->unk1D) {
        FillVertexBuffer(batch);
    }

    GxPrimVertexPtr(batch->vertexBuf, GxVBF_PNCT);

    if (!batch->indexBuf->unk1C || !batch->indexBuf->unk1D) {
        FillIndexBuffer(batch);
    }

    g_theGxDevicePtr->PrimIndexPtr(batch->indexBuf);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, static_cast<void*>(tex));

    CGxBatch draw;

    draw.m_primType = GxPrim_Triangles;
    draw.m_start = 0;
    draw.m_count = batch->indexTotal;
    draw.m_minIndex = 0;
    draw.m_maxIndex = static_cast<uint16_t>(batch->vertexTotal - 1);

    g_theGxDevicePtr->Draw(&draw, 1);
}

// ref: FUN_007b36b0
void Draw(CDetailDoodadData* instance) {
    for (uint32_t i = 0; i < 4; i++) {
        if (instance->m_batches[i].texture) {
            DrawBatch(&instance->m_batches[i]);
        }
    }
}

// ref: FUN_007b11b0
// A 64 by 8 ramp, white throughout, opaque at one end and clear at the other: alpha steps by
// four across the 64 columns and every row is the same. All eight rows exist only because the
// device wants a power-of-two texture it will filter.
static void BuildFadeTexels(const void** data) {
    static uint32_t s_texels[64 * 8];
    static bool s_built = false;

    if (!s_built) {
        s_built = true;

        memset(s_texels, 0, sizeof(s_texels));
    }

    uint32_t alpha = 0;

    // Filled from the far column back, so the fully clear end is the last one.
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t texel = 0x00ffffffu | (alpha << 24);

        for (uint32_t row = 0; row < 8; row++) {
            s_texels[row * 64 + (63 - i)] = texel;
        }

        alpha += 4;
    }

    *data = s_texels;
}

// ref: FUN_007b1270
// The texture's generator. Only the latch matters: the ramp is static, so it is handed over
// whole with its own pitch.
static void FadeTextureCallback(EGxTexCommand command, uint32_t width, uint32_t, uint32_t,
                                uint32_t, void*, uint32_t& pitch, const void*& data) {
    if (command != GxTex_Latch) {
        return;
    }

    BuildFadeTexels(&data);

    pitch = width * 4;
}

// ref: FUN_007b2d30
// The state the grass draws in. The shader path sets a pixel shader and its constants and
// reports 1; without shaders the fade is done with a second texture stage instead -- the ramp
// above, addressed by a generated coordinate, with a texture matrix that puts the fade in the
// last tenth or so of the draw distance.
int32_t SetupState() {
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, 2);
    GxRsSet(GxRs_AlphaRef, s_alphaRef);
    GxRsSet(GxRs_DepthWrite, 1);

    if (s_useShaders) {
        // The pixel program by shadow level, and the PCF set when the shadow map has it.
        int32_t pcf = (CWorld::s_enables2 >> 1) & 0x1;
        GxRsSet(GxRs_PixelShader, s_pixelShaders[ShadowMapGetShaderLevel() + pcf * 4]);

        SetupShaderConstants();
        ShadowMapBindDetailDoodads();

        return 1;
    }

    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);
    GxRsSet(GxRs_MatDiffuse, -1);

    GxRsSet(GxRs_Texture1, TextureGetGxTex(s_fadeTexture, 0, nullptr));

    GxRsSet(GxRs_ColorOp1, 0);
    GxRsSet(GxRs_AlphaOp1, 0);
    GxRsSet(GxRs_TexGen1, 3);
    GxRsSet(GxRs_Unk62, 1);

    // 1 / (distance * 0.15) is about a tenth of the draw distance, and the translate puts the
    // opaque end of the ramp at 0.85 of it -- so the grass is solid until the last 15%.
    C44Matrix fade;

    fade.Scale(1.0f / (s_fadeDistance * 0.15f));
    fade.RotateAroundY(1.5707963705062866f);

    C3Vector offset;

    offset.x = 0.0f;
    offset.y = 0.0f;
    offset.z = s_fadeDistance * -0.8500000238418579f;

    fade.Translate(offset);

    GxXformSet(GxXform_Tex1, fade);

    return 0;
}

// Take an instance for a chunk and scatter it.
//
// The reference takes these from a "WDETAILDOODADINST" object heap, which frozen does not have;
// this allocates one instead. Recorded rather than hidden: the behaviour is the same, the
// allocator is not.
CDetailDoodadData* CreateInstance(CMapChunk* chunk) {
    auto instance = static_cast<CDetailDoodadData*>(
        SMemAlloc(sizeof(CDetailDoodadData), __FILE__, __LINE__, 0x0));

    if (!instance) {
        return nullptr;
    }

    new (instance) CDetailDoodadData();

    instance->m_chunk = chunk;

    if (!Scatter(chunk, instance)) {
        ReleaseInstance(instance);

        return nullptr;
    }

    return instance;
}

// Part of ref: FUN_007b3780
// Give a batch's buffer pair back to the ring. WITHOUT THIS THE RING DRAINS AND NEVER REFILLS:
// every chunk that scatters takes a pair, so once as many chunks have scattered as the ring has
// pairs, every batch after that falls through to a streaming buffer of its own and the pool
// stops doing anything. That is not hypothetical -- a run measured over 8,000 instances in 200
// frames against a ring of 128.
//
// Only pairs that came FROM the pool go back; a streaming fallback is not the ring's to keep.
// The reference tests the vertex buffer's pool and returns both on that answer, and pushes the
// vertex one first so the index is on top, which is the order AcquireBuffers pops them in.
static void ReturnBuffers(SBatch* batch) {
    if (!batch->vertexBuf || batch->vertexBuf->m_pool != s_vertexPool) {
        batch->vertexBuf = nullptr;
        batch->indexBuf = nullptr;

        return;
    }

    CGxBuf* vertexBuf = batch->vertexBuf;
    CGxBuf* indexBuf = batch->indexBuf;

    s_buffers.Add(1, &vertexBuf);
    s_buffers.Add(1, &indexBuf);

    batch->vertexBuf = nullptr;
    batch->indexBuf = nullptr;
}

// ref: FUN_007b3960
void ReleaseInstance(CDetailDoodadData* instance) {
    if (!instance) {
        return;
    }

    instance->m_frameLink.Unlink();

    // The reference walks the batches backwards, clearing each and handing its buffers back
    // before the placements array goes. ref: FUN_007b3780
    for (int32_t i = 3; i >= 0; i--) {
        SBatch* batch = &instance->m_batches[i];

        batch->texture = nullptr;
        batch->vertexTotal = 0;
        batch->indexTotal = 0;

        ReturnBuffers(batch);
    }

    instance->~CDetailDoodadData();

    SMemFree(instance, __FILE__, __LINE__, 0x0);
}

}
