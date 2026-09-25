#ifndef WORLD_MAP_C_MAP_LIQUID_DATA_HPP
#define WORLD_MAP_C_MAP_LIQUID_DATA_HPP

#include <cstdint>

// MH2O: one tile's water, as the file lays it out. The area does not unpack it. It keeps the
// chunk body and reads through it on demand, which is why the reference's setter is one line --
// everything else here is an accessor over the mapped file.
//
// The tile has one entry per chunk, in row-major order, and each entry names a run of layers and
// a pair of masks. A layer covers a rectangle of the chunk's eight-by-eight tile grid, not
// necessarily the whole chunk.

// One chunk's entry in the header, 12 bytes, 256 of them.
struct SMLiquidChunk {
    uint32_t instanceOffset;    // +0x00: from the start of the MH2O body
    uint32_t layerCount;        // +0x04
    uint32_t attributeOffset;   // +0x08: zero when the chunk is entirely ordinary
};

static_assert(sizeof(SMLiquidChunk) == 0xc, "SMLiquidChunk is 12 bytes");

// One layer of one chunk, 24 bytes.
struct SMLiquidInstance {
    uint16_t liquidType;        // +0x00: into LiquidType.dbc
    uint16_t material;          // +0x02: which of the vertex layouts the heights use
    float minHeight;            // +0x04
    float maxHeight;            // +0x08
    uint8_t tileX;              // +0x0c: where in the eight-by-eight grid the layer starts
    uint8_t tileY;              // +0x0d
    uint8_t tileWidth;          // +0x0e
    uint8_t tileHeight;         // +0x0f
    uint32_t existsOffset;      // +0x10: a bit per tile saying which are wet; zero means all
    uint32_t vertexOffset;      // +0x14
};

static_assert(sizeof(SMLiquidInstance) == 0x18, "SMLiquidInstance is 24 bytes");

// The area's handle on its MH2O body. Eight bytes, only the second word of which is used; the
// reference leaves the first alone and nothing ever reads it.
class CMapLiquidData {
    public:
        // Static variables
        // What the accessors hand back when a chunk or a layer carries no mask of its own: a
        // chunk with no layers is wet nowhere, and one with layers but no attribute block is
        // ordinary everywhere, which the reference spells as a full mask.
        static const uint8_t s_maskNone[8];    // DAT_00a59484
        static const uint8_t s_maskAll[8];     // DAT_00a5948c

        // Member variables
        uint32_t m_unused = 0;              // +0x00
        const uint8_t* m_body = nullptr;    // +0x04: the MH2O chunk's contents, header first

        // Member functions
        // ref: FUN_008a3050
        CMapLiquidData();

        // Point it at a tile's MH2O body. ref: FUN_007d4f10
        void SetBody(const uint8_t* body);

        // Whether the tile carries any water at all. ref: FUN_008a3060
        bool HasLiquid() const;

        // One chunk's entry, by its position in the tile. ref: FUN_008a3070
        const SMLiquidChunk* Chunk(uint32_t x, uint32_t y) const;

        // One layer of a chunk. ref: FUN_008a3090
        const SMLiquidInstance* Layer(const SMLiquidChunk* chunk, uint32_t layer) const;

        // Which of a chunk's tiles you can fish in. ref: FUN_008a30b0
        const uint8_t* FishableMask(const SMLiquidChunk* chunk) const;

        // Which of a chunk's tiles are deep enough to swim in. ref: FUN_008a30e0
        const uint8_t* DeepMask(const SMLiquidChunk* chunk) const;

        // Which tiles of a layer's rectangle are wet. ref: FUN_008a3110
        const uint8_t* ExistsMask(const SMLiquidInstance* layer) const;
};

#endif
