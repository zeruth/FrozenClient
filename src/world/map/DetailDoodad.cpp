#include "world/map/DetailDoodad.hpp"
#include "world/CWorldParam.hpp"
#include "world/map/CMapChunk.hpp"
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
    s_indexCount = s_perChunk * 2;

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

        auto indexBuf = GxBufCreate(s_indexPool, 2, s_perChunk, i * s_perChunk * 2);
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

    // TODO FUN_007b1b10 as the loaded callback: the reference is told when the model lands so it
    // can work out the kind's bounds. Not ported, so nothing reacts to the load.

    return true;
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
        // has to be turned up the right way and normalized -- which is what the reference's
        // PlaneFromPoints hands back.
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
uint32_t Scatter(CMapChunk* chunk, SPlacement* out, uint32_t maxOut) {
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

    for (uint32_t i = 0; i < density && placed < maxOut; i++) {
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

        for (uint32_t n = 0; n < perCell && placed < maxOut; n++) {
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


            SPlacement& p = out[placed++];

            p.doodadId = static_cast<uint32_t>(kind);
            p.position.x = x;
            p.position.y = y;
            p.position.z = z;
            p.rotation = (SignedUnit(CRandom::uint32(seed)) + 1.0f) * TAU_HALF;
            p.scale = SignedUnit(CRandom::uint32(seed)) * SCALE_JITTER + 1.0f;
            p.plane = plane;
            p.color = 0xff000000u
                    | (static_cast<uint32_t>(c[2] + ROUND_BIAS) << 16)
                    | (static_cast<uint32_t>(c[1] + ROUND_BIAS) << 8)
                    | static_cast<uint32_t>(c[0] + ROUND_BIAS);
        }
    }

    return placed;
}

}
