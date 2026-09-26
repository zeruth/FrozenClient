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
