#ifndef WORLD_MAP_C_CHUNK_LIQUID_HPP
#define WORLD_MAP_C_CHUNK_LIQUID_HPP

#include <storm/List.hpp>
#include <cstdint>

// A chunk's liquid layer ("WCHUNKLIQUID" heap, 64 to a block). Not a CMapBaseObj: the mem handle
// is the first field and CMap::AllocChunkLiquid links it into CMap::s_chunkLiquidList by m_link.
class CChunkLiquid {
    public:
        // Member variables
        uint32_t m_memHandle = 0;         // +0
        // TODO +0x4..+0x5c
        TSLink<CChunkLiquid> m_link;      // +0x60
        // TODO
};

#endif
