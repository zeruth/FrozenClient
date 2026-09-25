#ifndef WORLD_MAP_LIQUID_VERTEX_DATA_HPP
#define WORLD_MAP_LIQUID_VERTEX_DATA_HPP

#include "world/map/CMapLiquidData.hpp"
#include <cstdint>

// A tile can carry its water in either of two formats, and both have four layouts of their own.
// Rather than convert one to the other, the reference reads both through a small interface with
// one subclass per file format: the surface build asks for a vertex's height, its depth and its
// texture coordinates, and never learns which format it came from.
//
// The layouts, numbered the same in both formats (LiquidMaterial.dbc calls the number LVF):
//
//   0  heights, then depths          ordinary water
//   1  heights, then coordinates     magma and slime, which scroll their own texture
//   2  depths only                   ocean, whose surface is flat at the layer's height
//   3  heights, coordinates, depths  not read by 3.3.5a; the reference treats it as none of
//                                    the above, so heights and coordinates come back empty
//
// The names are recovered from the runtime type information: the namespace is Liquid.
namespace Liquid {

// What GetCoord hands back for a layer that carries no coordinates. ref: DAT_00a59494
extern const uint8_t s_noCoord[4];

class CVertexData {
    public:
        // Member variables
        uint32_t m_unused = 0;  // +0x04: nothing writes it

        // Virtual member functions
        // ref: FUN_008a32c0
        virtual ~CVertexData() {}

        // The vertex's height above the map, or zero for a layout that stores none.
        virtual float GetHeight(uint32_t index) const = 0;

        // How deep the water is under the vertex, 0 for dry through 0xff for opaque. A layout
        // that stores none reads as dry.
        virtual uint8_t GetDepth(uint32_t index) const = 0;

        // The vertex's scrolling texture coordinates, as a pair of signed bytes.
        virtual const uint8_t* GetCoord(uint32_t index) const = 0;
};

// MCLQ, the pre-Wrath per-chunk format. The layout is not stored with the data, so the layer
// passes the one LiquidMaterial.dbc gave it; the vertices are eight bytes each either way.
class CVertexDataMCLQ : public CVertexData {
    public:
        // Member variables
        const uint8_t* m_verts = nullptr;   // +0x08: the MCLQ layer's vertex block
        uint32_t m_format = 0;              // +0x0c: LVF

        // Member functions
        // ref: FUN_008a31e0
        CVertexDataMCLQ(const uint8_t* verts, uint32_t format);

        // Virtual member functions
        float GetHeight(uint32_t index) const override;          // ref: FUN_008a3200
        uint8_t GetDepth(uint32_t index) const override;         // ref: FUN_008a3220
        const uint8_t* GetCoord(uint32_t index) const override;  // ref: FUN_008a3250
};

// MH2O, where each of a layer's arrays is separate and only the ones its layout uses are
// present. The vertex count decides where one array ends and the next begins.
class CVertexDataMH2O : public CVertexData {
    public:
        // Member variables
        const uint8_t* m_body = nullptr;            // +0x08: the tile's MH2O body
        const SMLiquidInstance* m_layer = nullptr;  // +0x0c
        uint32_t m_vertexCount = 0;                 // +0x10: one more than the tile count each way

        // Member functions
        // ref: FUN_008a3280
        CVertexDataMH2O(const CMapLiquidData* data, const SMLiquidInstance* layer);

        // Virtual member functions
        float GetHeight(uint32_t index) const override;          // ref: FUN_008a3130
        uint8_t GetDepth(uint32_t index) const override;         // ref: FUN_008a3160
        const uint8_t* GetCoord(uint32_t index) const override;  // ref: FUN_008a31b0
};

}

#endif
