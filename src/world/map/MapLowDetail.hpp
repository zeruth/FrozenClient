#ifndef WORLD_MAP_MAP_LOW_DETAIL_HPP
#define WORLD_MAP_MAP_LOW_DETAIL_HPP

#include "world/map/CMapAreaLow.hpp"
#include <storm/Array.hpp>
#include <cstdint>

class CMapObjDef;
struct SMODF;

// The map's .wdl: one low-detail height grid per tile, plus the buildings big enough to be worth
// drawing from far away. Named for the reference's own module, which the loader's allocation tag
// (".\MapLowDetail.cpp") gives away.
//
// The whole file is read once and kept: every area points into it rather than copying, so the
// buffer outlives them all and is freed only when the map unloads.
class CMapLowDetail {
    public:
        // Member variables, in the reference's order.
        uint8_t* m_data = nullptr;              // +0x0000: the whole file
        const char* m_mapObjNames = nullptr;    // +0x0004: MWMO, the name strings
        const uint32_t* m_mapObjNameOffsets = nullptr;  // +0x0008: MWID, into MWMO
        const SMODF* m_mapObjDefs = nullptr;    // +0x000c: MODF, the placements
        uint32_t m_mapObjDefCount = 0;          // +0x0010: MODF size / 0x40
        const uint32_t* m_areaOffsets = nullptr;  // +0x0014: MAOF, 64*64 offsets into the file

        // +0x0018: one per tile, null where the map has none. Indexed [row * 64 + col], the same
        // way round as CMap::s_areaGrid.
        CMapAreaLow* m_areas[64 * 64] = {};

        // +0x4018: the defs this file placed, so they can be released together.
        TSGrowableArray<CMapObjDef*> m_placedDefs;

        // Member functions
        // Read <path>\<name>.wdl and build an area for every tile it carries. False when the map
        // has no .wdl at all, which is not an error -- some do not. ref: FUN_007cc310
        int32_t Load(const char* path, const char* name);

        // Release the areas, the defs this file placed, and the file buffer itself.
        void Free();
};

#endif
