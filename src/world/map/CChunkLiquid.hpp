#ifndef WORLD_MAP_C_CHUNK_LIQUID_HPP
#define WORLD_MAP_C_CHUNK_LIQUID_HPP

#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CMapChunk;

namespace Liquid {
    class CVertexData;
}

// A chunk's liquid layer ("WCHUNKLIQUID" heap, 64 to a block, 0x444 bytes each). Not a
// CMapBaseObj: the mem handle is the first field. CMap::AllocChunkLiquid links it into
// CMap::s_chunkLiquidList by m_link; its chunk keeps it in CMapChunk::m_liquidList by
// m_chunkLink.
//
// The field map comes from the creation function, CMapChunk::CreateLiquid, which builds these
// from either of the two liquid formats a tile can carry: the old per-chunk MCLQ, up to four
// layers picked out by the chunk's own flag bits, or MH2O, which the area keeps whole and hands
// over a layer at a time. Either way the layer reads its vertices through a Liquid::CVertexData,
// so nothing below this line knows which format it came from.
class CChunkLiquid {
    public:
        // Static constants
        // A layer covers at most the chunk's whole eight-by-eight tile grid, so at most nine
        // vertices each way. The heap element size proves the array is inline and this big.
        static const uint32_t MAX_VERTICES = 81;

        // Member variables
        uint32_t m_memHandle = 0;         // +0x00
        // Which liquid this is, by LiquidType.dbc id, and how its vertices are laid out on
        // disk -- the LVF that LiquidType's material names. See LiquidVertexData.hpp.
        uint32_t m_liquidType = 0;        // +0x04
        uint32_t m_vertexFormat = 0;      // +0x08
        // The owning chunk's corner, copied so the vertex build needs nothing but this record.
        C3Vector m_origin;                // +0x0c
        // TODO +0x18..+0x24
        float m_minHeight = 0.0f;         // +0x28: CMapChunk::GetBounds lowers the box to it
        float m_maxHeight = 0.0f;         // +0x2c: and raises it to this
        float m_animTime = 0.0f;          // +0x30: the surface's own clock, -1 while it has none
        // The rectangle of the chunk's eight-by-eight tile grid this layer covers. MCLQ layers
        // always cover the whole chunk; an MH2O one can be a corner of it. The two axes are the
        // file's the other way round: "X" here steps along MH2O's y.
        uint32_t m_tileX = 0;             // +0x34
        uint32_t m_tileY = 0;             // +0x38
        uint32_t m_tileEndX = 8;          // +0x3c
        uint32_t m_tileEndY = 8;          // +0x40
        // How to read the layer's vertices, and the per-tile mask saying which squares of the
        // rectangle are actually wet.
        Liquid::CVertexData* m_vertexData = nullptr;  // +0x44
        // TODO +0x48..+0x50
        const uint8_t* m_tileMask = nullptr;  // +0x54
        // TODO +0x58: the drawn surface, released through the Liquid module on destroy
        void* m_surface = nullptr;        // +0x58
        CMapChunk* m_chunk = nullptr;     // +0x5c
        TSLink<CChunkLiquid> m_link;      // +0x60
        // TODO +0x68..+0x6c
        TSLink<CChunkLiquid> m_chunkLink; // +0x70
        // The layer's vertices, in the chunk's own space: x and y step out from the chunk corner
        // by a tile at a time, and z is the height above it.
        C3Vector m_vertices[MAX_VERTICES];  // +0x78

        // Member functions
        // Place the layer's vertices under its chunk's corner, reading each height through
        // m_vertexData. ref: FUN_007cdf80
        void BuildVertices();

        // Let the surface go, if one was ever built. ref: FUN_007cde10
        void ReleaseSurface();
};

#endif
