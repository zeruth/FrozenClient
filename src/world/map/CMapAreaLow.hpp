#ifndef WORLD_MAP_C_MAP_AREA_LOW_HPP
#define WORLD_MAP_C_MAP_AREA_LOW_HPP

#include <storm/List.hpp>
#include <cstdint>

// The low-detail area record ("WAREALOW" heap, 16 to a block). Not a CMapBaseObj: the reference
// object is 0x5c bytes with the mem handle first, the constructor (FUN_007c06e0) zeroes +0x4..+0x58,
// and CMap::FreeAreaLow unlinks +0x4c before returning it to the heap.
class CMapAreaLow {
    public:
        // Member variables
        uint32_t m_memHandle = 0;         // +0
        // TODO +0x4..+0x48
        TSLink<CMapAreaLow> m_link;       // +0x4c
        // TODO +0x54..+0x58
};

#endif
