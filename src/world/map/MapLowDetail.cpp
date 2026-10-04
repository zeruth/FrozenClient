#include "gx/buffer/CGxBuf.hpp"
#include "gx/RenderState.hpp"
#include "gx/Draw.hpp"
#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/Buffer.hpp"
#include "world/CWorldScene.hpp"
#include "world/CWorld.hpp"
#include "world/DayNightLight.hpp"
#include "world/map/MapLowDetail.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDef.hpp"
#include "util/SFile.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <cmath>

static const float CHUNK_SIZE = 33.33333206176758f;       // DAT_00a3e554
static const float MAP_HALF_EXTENT = 17066.666015625f;    // DAT_009e2acc
static const float TILE_SIZE = 533.3333129882812f;        // DAT_00a0b5e4

// MARE is a 17x17 outer grid and a 16x16 inner one, as int16 heights.
static const uint32_t MARE_HEIGHTS = 17 * 17 + 16 * 16;   // 545

// What one tile's low-detail mesh costs when nothing is holed out: twelve bytes for each of the
// 16x16 cells. DAT_00a3fae8 holds the same 3072.
static const uint32_t LOW_DETAIL_FULL_BYTES = 12 * 16 * 16;

static const uint32_t ID_MWMO = 0x4d574d4f;
static const uint32_t ID_MAHO = 0x4d41484f;

namespace {

// A chunk header in the file: a four-byte id and the size of what follows.
struct SChunkHeader {
    uint32_t id;
    uint32_t size;
};

}

// Give back everything the last load took. Called before a load so changing maps does not leak
// the previous one's file buffer, which is the whole .wdl and is held for the life of the map.
void CMapLowDetail::Free() {
    for (int32_t i = 0; i < 64 * 64; i++) {
        if (this->m_areas[i]) {
            CMap::FreeAreaLow(this->m_areas[i]);
            this->m_areas[i] = nullptr;
        }
    }

    for (uint32_t i = 0; i < this->m_placedDefs.Count(); i++) {
        this->m_placedDefs[i]->m_linkCount--;
    }

    this->m_placedDefs.SetCount(0);

    if (this->m_data) {
        SMemFree(this->m_data, __FILE__, __LINE__, 0);
        this->m_data = nullptr;
    }

    this->m_mapObjNames = nullptr;
    this->m_mapObjNameOffsets = nullptr;
    this->m_mapObjDefs = nullptr;
    this->m_mapObjDefCount = 0;
    this->m_areaOffsets = nullptr;
}

// ref: FUN_007cc310
// The .wdl, read whole and kept. Its layout is MVER, then optionally MWMO / MWID / MODF for the
// far-away buildings, then MAOF -- a 64 by 64 table of file offsets, one per tile, zero where
// the map has no tile there. Each non-zero one leads to that tile's MARE heights and, if it has
// any holes, a MAHO bitmap.
int32_t CMapLowDetail::Load(const char* path, const char* name) {
    this->Free();

    char filePath[260];

    SStrPrintf(filePath, sizeof(filePath), "%s\\%s.wdl", path, name);

    SFile* file = nullptr;

    if (!SFile::Open(filePath, &file) || !file) {
        // Not every map has one.
        return 0;
    }

    uint32_t size = SFile::GetFileSize(file, nullptr);

    this->m_data = static_cast<uint8_t*>(SMemAlloc(size, __FILE__, __LINE__, 0));

    SFile::Read(file, this->m_data, size, nullptr, nullptr, nullptr);
    SFile::Close(file);

    // Past MVER.
    auto header = reinterpret_cast<const SChunkHeader*>(
        this->m_data + 8 + reinterpret_cast<const SChunkHeader*>(this->m_data)->size);

    auto body = reinterpret_cast<const uint8_t*>(header + 1);

    if (header->id == ID_MWMO) {
        this->m_mapObjNames = reinterpret_cast<const char*>(body);

        // MWID follows MWMO, MODF follows MWID; each is found by stepping over the one before.
        auto mwid = reinterpret_cast<const SChunkHeader*>(body + header->size);

        this->m_mapObjNameOffsets = reinterpret_cast<const uint32_t*>(mwid + 1);

        auto modf = reinterpret_cast<const SChunkHeader*>(
            reinterpret_cast<const uint8_t*>(mwid + 1) + mwid->size);

        this->m_mapObjDefs = reinterpret_cast<const SMODF*>(modf + 1);
        this->m_mapObjDefCount = modf->size / sizeof(SMODF);

        header = reinterpret_cast<const SChunkHeader*>(
            reinterpret_cast<const uint8_t*>(modf + 1) + modf->size);

        body = reinterpret_cast<const uint8_t*>(header + 1);
    }

    // MAOF.
    this->m_areaOffsets = reinterpret_cast<const uint32_t*>(body);

    for (int32_t row = 0; row < 64; row++) {
        for (int32_t col = 0; col < 64; col++) {
            int32_t index = row * 64 + col;

            if (!this->m_areaOffsets[index]) {
                continue;
            }

            auto mare = reinterpret_cast<const SChunkHeader*>(
                this->m_data + this->m_areaOffsets[index]);

            auto heights = reinterpret_cast<const int16_t*>(mare + 1);

            auto area = CMap::AllocAreaLow();

            this->m_areas[index] = area;
            area->m_heights = heights;

            // The tile's height range, straight off the 545 samples.
            int32_t minHeight = 100000;
            int32_t maxHeight = -100000;

            for (uint32_t i = 0; i < MARE_HEIGHTS; i++) {
                int32_t h = heights[i];

                if (h > maxHeight) {
                    maxHeight = h;
                }

                if (h < minHeight) {
                    minHeight = h;
                }
            }

            area->m_row = row;
            area->m_col = col;

            // The map counts down from its half extent along both axes, which is why the tile's
            // own corner is the box's MAXIMUM and the far corner one tile lower.
            float x = MAP_HALF_EXTENT - static_cast<float>(row * 16) * CHUNK_SIZE;
            float y = MAP_HALF_EXTENT + static_cast<float>(col * 16) * -CHUNK_SIZE;

            area->m_bounds.t.x = x;
            area->m_bounds.t.y = y;
            area->m_bounds.t.z = static_cast<float>(maxHeight);

            area->m_bounds.b.x = x - TILE_SIZE;
            area->m_bounds.b.y = y - TILE_SIZE;
            area->m_bounds.b.z = static_cast<float>(minHeight);

            area->m_sphere.c.x = (area->m_bounds.t.x + area->m_bounds.b.x) * 0.5f;
            area->m_sphere.c.y = (area->m_bounds.b.y + area->m_bounds.t.y) * 0.5f;
            area->m_sphere.c.z = (area->m_bounds.b.z + area->m_bounds.t.z) * 0.5f;

            float dx = area->m_bounds.t.x - area->m_sphere.c.x;
            float dy = area->m_bounds.t.y - area->m_sphere.c.y;
            float dz = area->m_bounds.t.z - area->m_sphere.c.z;

            area->m_sphere.r = sqrtf(dx * dx + dy * dy + dz * dz);

            area->m_originX = x;
            area->m_originY = y;

            // MAHO, when the tile has one: sixteen words, one bit per cell, set where the cell is
            // holed. Every cell that is NOT holed costs twelve bytes of mesh.
            auto maho = reinterpret_cast<const SChunkHeader*>(
                reinterpret_cast<const uint8_t*>(mare + 1) + mare->size);

            if (maho->id == ID_MAHO) {
                auto holes = reinterpret_cast<const uint16_t*>(maho + 1);

                area->m_holes = holes;
                area->m_meshBytes = 0;

                for (uint32_t r = 0; r < 16; r++) {
                    for (uint32_t bit = 0; bit < 16; bit++) {
                        if (!(holes[r] & (1 << bit))) {
                            area->m_meshBytes += 12;
                        }
                    }
                }
            } else {
                area->m_holes = nullptr;
                area->m_meshBytes = LOW_DETAIL_FULL_BYTES;
            }
        }
    }

    // The far-away buildings. Each MODF becomes a def exactly as a tile's would, and the def is
    // held here as well so the whole set can be released together.
    this->m_placedDefs.Reserve(this->m_mapObjDefCount, 1);
    this->m_placedDefs.SetCount(0);

    C3Vector origin = { MAP_HALF_EXTENT, MAP_HALF_EXTENT, 0.0f };

    for (uint32_t i = 0; i < this->m_mapObjDefCount; i++) {
        const SMODF* modf = &this->m_mapObjDefs[i];

        auto objName = this->m_mapObjNames + this->m_mapObjNameOffsets[modf->nameId];

        auto def = CMap::CreateMapObjDef(objName, modf, origin, 0);

        if (!def) {
            continue;
        }

        // TODO the reference waits here for the root and every group to be read, unless the
        // no-wait global is set. Frozen's map objects load asynchronously and nothing in the
        // low-detail path reads their geometry yet, so blocking the load would only stall it.

        // The same counter a chunk's reference bumps: the def lives until every holder
        // has let go, and this file is one of them.
        def->m_linkCount++;

        this->m_placedDefs.Add(1, &def);
    }

    return 1;
}

namespace {

const float LOW_CHUNK_SIZE = 33.33333206176758f;       // DAT_00a3e554
const float LOW_HALF_CHUNK = 16.66666603088379f;       // DAT_00a3fab8
const uint32_t LOW_FULL_INDICES = 0xC00;               // DAT_00a3fae8

}

// ref: FUN_007d5150
void CMapAreaLow::FillVertices(CGxBuf* buf) {
    float* out = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(buf));
    int32_t i = 0;
    float x = this->m_originX;

    for (int32_t row = 0; row < 17; row++) {
        float y = this->m_originY;

        for (int32_t col = 0; col < 17; col++) {
            out[0] = x;
            out[1] = y;
            out[2] = static_cast<float>(this->m_heights[i++]);
            reinterpret_cast<uint32_t*>(out)[3] = 0xFFFFFFFF;
            out += 4;
            y -= LOW_CHUNK_SIZE;
        }

        x -= LOW_CHUNK_SIZE;
    }

    x = this->m_originX - LOW_HALF_CHUNK;

    for (int32_t row = 0; row < 16; row++) {
        float y = this->m_originY - LOW_HALF_CHUNK;

        for (int32_t col = 0; col < 16; col++) {
            out[0] = x;
            out[1] = y;
            out[2] = static_cast<float>(this->m_heights[i++]);
            reinterpret_cast<uint32_t*>(out)[3] = 0xFFFFFFFF;
            out += 4;
            y -= LOW_CHUNK_SIZE;
        }

        x -= LOW_CHUNK_SIZE;
    }

    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->unk1C = 1;
}

// ref: FUN_007d5240
void CMapAreaLow::FillIndices(const uint16_t* holes, int32_t holed, CGxBuf* buf) {
    uint16_t* out = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(buf));
    uint16_t center = 0x121;
    uint16_t corner = 0;

    for (int32_t row = 0; row < 16; row++) {
        for (int32_t col = 0; col < 16; col++) {
            if (holes) {
                bool set = (holes[row] >> col) & 1;

                if (holed ? !set : set) {
                    center++;
                    corner++;
                    continue;
                }
            }

            uint16_t c00 = corner;
            uint16_t c01 = corner + 1;
            uint16_t c10 = corner + 17;
            uint16_t c11 = corner + 18;

            out[0] = center;
            out[1] = c01;
            out[2] = c00;
            out[3] = center;
            out[4] = c11;
            out[5] = c01;
            out[6] = center;
            out[7] = c10;
            out[8] = c11;
            out[9] = center;
            out[10] = c00;
            out[11] = c10;
            out += 12;

            center++;
            corner++;
        }

        corner++;
    }

    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->unk1C = 1;
}

// ref: FUN_007d5e70
void CMapAreaLow::Draw() {
    if (this->m_meshBytes <= 0) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_Fog, 1);
    GxRsSet(GxRs_DepthTest, 3);

    auto block = DayNightGetBlock();
    GxRsSet(GxRs_FogStart, 0.0f);
    GxRsSet(GxRs_FogEnd, 1.0f);
    GxRsSet(GxRs_FogColor, block->fogColor.value);
    GxRsSet(GxRs_Lighting, 0);

    if (!this->m_vertexBlock) {
        CMap::s_lowDetailCache.Acquire(&this->m_vertexBlock);
    }

    if (!this->m_vertexBlock) {
        CGxBuf* stream = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x10, 0x221);
        this->FillVertices(stream);
        GxPrimVertexPtr(stream, GxVBF_PC);
    } else {
        CGxBuf* buf = this->m_vertexBlock->buf;

        if (!buf->unk1C || !buf->unk1D) {
            this->FillVertices(buf);
        }

        this->m_vertexBlock->frame = static_cast<uint32_t>(CWorld::s_updateCount);
        GxPrimVertexPtr(buf, GxVBF_PC);
    }

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_minIndex = 0;
    batch.m_maxIndex = 0x220;

    if (!(this->m_flags & 1) && this->m_meshBytes == LOW_FULL_INDICES) {
        GxRsSet(GxRs_Culling, 0);

        if (!CMap::s_lowDetailIndexBuf->unk1C || !CMap::s_lowDetailIndexBuf->unk1D) {
            CMapAreaLow::FillIndices(nullptr, 0, CMap::s_lowDetailIndexBuf);
        }

        g_theGxDevicePtr->PrimIndexPtr(CMap::s_lowDetailIndexBuf);
        batch.m_count = LOW_FULL_INDICES;
    } else {
        GxRsSet(GxRs_Culling, 0);

        CGxBuf* stream = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, this->m_meshBytes);
        CMapAreaLow::FillIndices(this->m_holes, 0, stream);
        g_theGxDevicePtr->PrimIndexPtr(stream);
        batch.m_count = this->m_meshBytes;
        GxDraw(&batch, 1);

        GxRsSet(GxRs_Culling, 1);

        stream = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, LOW_FULL_INDICES - this->m_meshBytes);
        CMapAreaLow::FillIndices(this->m_holes, 1, stream);
        g_theGxDevicePtr->PrimIndexPtr(stream);
        batch.m_count = LOW_FULL_INDICES - this->m_meshBytes;
    }

    GxDraw(&batch, 1);
    GxRsPop();

    // TODO the reference ends by resetting the matrix stack at device +0x1008 to identity
    // (FUN_0057c340); which of frozen's stacks that is has not been pinned.
}

// ref: FUN_007cc0b0
void CMapLowDetail::QueueVisible(const C3Vector& cameraPos) {
    int32_t cx = static_cast<int32_t>(lrintf(-(cameraPos.x - 17066.666f) * 0.03f - 0.5f));
    int32_t cy = static_cast<int32_t>(lrintf(-(cameraPos.y - 17066.666f) * 0.03f - 0.5f));
    int32_t reach = static_cast<int32_t>(static_cast<int64_t>(CWorld::GetHorizonDistance() * 0.030000001f));

    int32_t minCol = ((cy - reach) >> 4) - 2;
    int32_t minRow = ((cx - reach) >> 4) - 2;
    int32_t maxCol = ((cy + reach) >> 4) + 2;
    int32_t maxRow = ((reach + cx) >> 4) + 2;

    if (minRow < 0) {
        minRow = 0;
    }

    if (minCol < 0) {
        minCol = 0;
    }

    if (63 < maxCol) {
        maxCol = 63;
    }

    if (63 < maxRow) {
        maxRow = 63;
    }

    for (int32_t row = minRow; row <= maxRow; row++) {
        for (int32_t col = minCol; col <= maxCol; col++) {
            CMapAreaLow* area = this->m_areas[row * 64 + col];

            if (area && !CWorldScene::BoxOutsideFrustum(area->m_bounds)
                && !CWorldScene::SphereOccludedByVolumes(area->m_sphere)
                && (CWorld::GetFarClip() <= 300.0f || !CWorldScene::BoxOccluded(area->m_bounds, 0x28))) {
                CWorldScene::QueueLowDetailArea(area);
            }
        }
    }

    for (uint32_t i = 0; i < this->m_placedDefs.Count(); i++) {
        CMapObjDef* def = this->m_placedDefs[i];
        CAaSphere sphere;
        sphere.c = def->m_center;
        sphere.r = def->m_radius;

        if (def->m_mapObj->m_rootLoaded && !CWorldScene::BoxOutsideFrustum(def->m_bounds)
            && !CWorldScene::SphereOccludedByVolumes(sphere)) {
            CWorldScene::QueueLowDetailDef(def);
        }
    }
}

// ref: FUN_007cbfe0
void CMapLowDetail::ReleaseBuffers() {
    for (auto area : this->m_areas) {
        if (area) {
            if (area->m_vertexBlock) {
                CMap::s_lowDetailCache.Release(area->m_vertexBlock);
            }

            area->m_vertexBlock = nullptr;
        }
    }
}
