#ifndef WORLD_MAP_C_MAP_AREA_LOW_HPP
#define WORLD_MAP_C_MAP_AREA_LOW_HPP

#include "world/map/FVBBList.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Sphere.hpp>
#include <cstdint>

// The low-detail area record ("WAREALOW" heap, 16 to a block). Not a CMapBaseObj: the reference
// object is 0x5c bytes with the mem handle first, the constructor (FUN_007c06e0) zeroes +0x4..+0x58,
// and CMap::FreeAreaLow unlinks +0x4c before returning it to the heap.
//
// One per tile of the map's .wdl, built by CMapLowDetail::Load. Everything it points at lives in
// that file's buffer, so an area is only valid while the CMapLowDetail that made it is.
class CGxBuf;

class CMapAreaLow {
    public:
        // Member variables
        uint32_t m_memHandle = 0;         // +0x00

        // +0x04 .. +0x18: the tile's box. The map counts DOWN from its half extent along both
        // axes, so the tile's own corner is the box's t (top) and the far corner its b (bottom).
        CAaBox m_bounds;

        // +0x1c .. +0x28: the same box as a sphere, for the cheap cull.
        CAaSphere m_sphere;

        float m_originX = 0.0f;           // +0x2c: the tile corner again, kept apart from the box
        float m_originY = 0.0f;           // +0x30
        // TODO +0x34
        int32_t m_col = 0;                // +0x38
        int32_t m_row = 0;                // +0x3c

        // +0x40: what this tile's low-detail mesh costs in bytes -- twelve for every cell that is
        // not holed, so 3072 for a tile with no MAHO at all.
        uint32_t m_meshBytes = 0;

        // +0x44: MARE, a 17x17 outer grid then a 16x16 inner one, as int16 heights.
        const int16_t* m_heights = nullptr;

        // +0x48: MAHO, sixteen words with a bit per cell where the tile is holed. Null when the
        // tile has none, which is the common case.
        const uint16_t* m_holes = nullptr;

        TSLink<CMapAreaLow> m_link;       // +0x4c
        // +0x54: the vertex buffer it holds from CMap::s_lowDetailCache, if it holds one.
        FVBBList::Block* m_vertexBlock = nullptr;
        // +0x58: bit 1 set means its holes are drawn as two passes even when it has none.
        uint32_t m_flags = 0;

        // Draw the tile flat in the fog colour, beyond the far clip: the 17x17 corner grid and
        // the 16x16 centre grid as four triangles a cell, the holed cells (if any) in a second,
        // culled pass. ref: FUN_007d5e70
        void Draw();
        // ref: FUN_007d5150
        void FillVertices(CGxBuf* buf);
        // The cells' triangles, all of them, or only the unholed (`holed` 0) or holed (`holed` 1)
        // ones. ref: FUN_007d5240
        static void FillIndices(const uint16_t* holes, int32_t holed, CGxBuf* buf);
};

#endif
