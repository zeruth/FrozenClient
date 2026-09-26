#ifndef WORLD_MAP_C_CHUNK_LIQUID_HPP
#define WORLD_MAP_C_CHUNK_LIQUID_HPP

#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <tempest/Box.hpp>
#include <tempest/Rect.hpp>
#include "util/BitArray.hpp"
#include <cstdint>

class CMapChunk;

namespace Liquid {
    class CVertexData;
    class CInstance;
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
        // How long the layer has gone unseen, in seconds. Every frame the layer is visible
        // resets it to zero; two seconds after it stops being so the surface is let go and this
        // turns negative, which is what drops the layer off the frame list.
        float m_animTime = 0.0f;          // +0x30
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
        // One bit per tile of the rectangle, saying which squares are wet. MH2O layers keep it;
        // MCLQ ones leave it empty and answer out of m_tileMask instead.
        BitArray m_exists;                    // +0x48
        // MCLQ's own eight-by-eight grid, a byte a tile. Null on an MH2O layer.
        const uint8_t* m_tileMask = nullptr;  // +0x54
        // The drawn surface this layer belongs to, shared with any other layer it merged
        // with. Made on first sight, let go two seconds after the layer stops being visible.
        Liquid::CInstance* m_surface = nullptr;  // +0x58
        CMapChunk* m_chunk = nullptr;     // +0x5c
        TSLink<CChunkLiquid> m_link;      // +0x60
        // The frame's link, used by two lists in turn and never by both: the distance row the
        // layer lands in when its chunk is found visible, and then the frame-wide list the row
        // visit moves it to. That is why the visit reads the next pointer before moving a node.
        TSLink<CChunkLiquid> m_frameLink; // +0x68
        TSLink<CChunkLiquid> m_chunkLink; // +0x70
        // The layer's vertices, in the chunk's own space: x and y step out from the chunk corner
        // by a tile at a time, and z is the height above it.
        C3Vector m_vertices[MAX_VERTICES];  // +0x78

        // Member functions
        // Place the layer's vertices under its chunk's corner, reading each height through
        // m_vertexData. ref: FUN_007cdf80
        void BuildVertices();

        // Make the layer's surface if it has none and mark it seen this frame.
        void UpdateForFrame();

        // Let the surface go, if one was ever built.
        void ReleaseSurface();

        // The layer's world box: its chunk's, with the height range narrowed to the layer's own.
        // ref: FUN_007cde80
        void GetBounds(CAaBox* box) const;

        // Age the layer by one frame. ref: FUN_007cde30
        void UpdateAnim();

        // Whether the layer actually covers one tile of its chunk's eight-by-eight grid. An
        // MH2O layer answers from its wet-tile bits, an MCLQ one from its own grid.
        // ref: FUN_007ce1f0
        bool CoversTile(uint32_t x, uint32_t y) const;

        // One tile of an MCLQ layer's grid: which liquid it holds, and the two flags the file
        // carries beside it. Returns whether the tile is this layer's. ref: FUN_007ce180
        bool ReadTileFlags(uint32_t x, uint32_t y, uint32_t* kind, uint32_t* fishable, uint32_t* shared) const;

        // The surface height at a point inside one tile, `frac` being how far across it lies.
        // False when the tile is outside the layer's rectangle. ref: FUN_007ce0b0
        bool GetHeightAt(const float* frac, const uint32_t* tile, float* height) const;

        // The layer's tile rectangle, +0x34 through +0x40 read as one CiRect the way the reference
        // reads it. Its "Y" slot is the m_tileX axis; see the note on those fields.
        const CiRect& TileRect() const;

        // The layer as a receiver for a projected decal: its wet triangles inside the caster's
        // box go into the shared hit-record pool, and the decal module re-draws them. So a blob
        // shadow falls on water. `cellRect` counts cells of the chunk's eight-by-eight grid and
        // `box` is in the chunk's own space, both as the world query hands them over.
        // ref: FUN_007ce960
        bool QueryBox(void* object, const CAaBox& box, const CiRect& cellRect);
        // ref: FUN_007ce5d0
        bool RecordHits(void* object, const uint8_t* outcodes, const CiRect& rect, const int32_t* span);
};

#endif
