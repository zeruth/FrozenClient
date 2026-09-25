#ifndef WORLD_MAP_C_CHUNK_LIQUID_HPP
#define WORLD_MAP_C_CHUNK_LIQUID_HPP

#include <storm/List.hpp>
#include <cstdint>

// A chunk's liquid layer ("WCHUNKLIQUID" heap, 64 to a block). Not a CMapBaseObj: the mem handle
// is the first field. CMap::AllocChunkLiquid links it into CMap::s_chunkLiquidList by m_link;
// its chunk keeps it in CMapChunk::m_liquidList by m_chunkLink.
class CChunkLiquid {
    public:
        // Member variables
        uint32_t m_memHandle = 0;         // +0
        // TODO +0x4..+0x24
        float m_minHeight = 0.0f;         // +0x28: CMapChunk::GetBounds lowers the box to it
        float m_maxHeight = 0.0f;         // +0x2c: and raises it to this
        // TODO +0x30..+0x5c
        TSLink<CChunkLiquid> m_link;      // +0x60
        // TODO +0x68..+0x6c
        TSLink<CChunkLiquid> m_chunkLink; // +0x70
        // TODO
};

#endif
