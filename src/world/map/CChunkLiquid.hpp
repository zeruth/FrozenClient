#ifndef WORLD_MAP_C_CHUNK_LIQUID_HPP
#define WORLD_MAP_C_CHUNK_LIQUID_HPP

#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CMapChunk;

// A chunk's liquid layer ("WCHUNKLIQUID" heap, 64 to a block). Not a CMapBaseObj: the mem handle
// is the first field. CMap::AllocChunkLiquid links it into CMap::s_chunkLiquidList by m_link;
// its chunk keeps it in CMapChunk::m_liquidList by m_chunkLink.
//
// The field map comes from the creation function (FUN_007c5690), which builds these from either
// of the two liquid formats a tile can carry: the old per-chunk MCLQ, up to four layers picked
// out by the chunk's own flag bits, or MH2O, which the area parses once and hands over a layer
// at a time. Nothing here is filled yet -- the creation and the mesh builders it calls are a
// module of their own (the reference's 0x008a3xxx block) that frozen has not started.
class CChunkLiquid {
    public:
        // Member variables
        uint32_t m_memHandle = 0;         // +0x00
        // Which liquid this is and how it draws, both resolved through LiquidType.dbc and the
        // material it names.
        uint32_t m_liquidType = 0;        // +0x04
        uint32_t m_material = 0;          // +0x08
        // The owning chunk's corner, copied so the vertex build needs nothing but this record.
        C3Vector m_origin;                // +0x0c
        // TODO +0x18..+0x24
        float m_minHeight = 0.0f;         // +0x28: CMapChunk::GetBounds lowers the box to it
        float m_maxHeight = 0.0f;         // +0x2c: and raises it to this
        // TODO +0x30
        // The rectangle of the chunk's eight-by-eight tile grid this layer covers. MCLQ layers
        // always cover the whole chunk; an MH2O one can be a corner of it.
        uint32_t m_tileX = 0;             // +0x34
        uint32_t m_tileY = 0;             // +0x38
        uint32_t m_tileEndX = 8;          // +0x3c
        uint32_t m_tileEndY = 8;          // +0x40
        // The built surface, and the per-tile mask saying which squares of the rectangle are
        // actually wet.
        void* m_surface = nullptr;        // +0x44
        // TODO +0x48..+0x50
        const uint8_t* m_tileMask = nullptr;  // +0x54
        // TODO +0x58: a sound emitter the row visit starts when the layer comes into view
        CMapChunk* m_chunk = nullptr;     // +0x5c
        TSLink<CChunkLiquid> m_link;      // +0x60
        // TODO +0x68..+0x6c
        TSLink<CChunkLiquid> m_chunkLink; // +0x70
        // TODO
};

#endif
