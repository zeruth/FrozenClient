#ifndef WORLD_MAP_V_B_B_LIST_HPP
#define WORLD_MAP_V_B_B_LIST_HPP

#include <cstdint>
#include <storm/List.hpp>

#include "gx/buffer/Types.hpp"

class CGxBuf;
class CGxPool;

// The map's own GPU buffer allocator. Terrain chunks and map object groups each ask it for
// a run of vertices or indices and hold onto the block it hands back; the block owns the
// pool and the buffer under it, and knows the slot that points at it so it can clear that
// slot when it goes away.
//
// The reference carries a second, sharing mode where one big pool is carved into blocks and
// neighbours are coalesced on free. Nothing turns it on in 3.3.5a -- both lists are built
// with the flag clear -- so the paths behind it are marked rather than written.
//
// The name is the reference's own, recovered from the RTTI string on its block record.
class VBBList {
    public:
        // One allocation: a pool, a buffer in it, and the slot that points here.
        class Block {
            public:
                TSLink<Block> link;                 // +0x00
                uint32_t offset = 0;                // +0x08: sharing mode only
                char* base = nullptr;               // +0x0c: sharing mode only
                uint32_t size = 0;                  // +0x10: bytes the pool was made for
                CGxPool* pool = nullptr;            // +0x14
                CGxBuf* buf = nullptr;              // +0x18
                Block** owner = nullptr;            // +0x1c: the slot holding this block
        };

        // Static variables
        // The two lists the map allocates from (0x00aeee58 and 0x00aeee80, file statics of
        // the reference's MapChunk.cpp).
        static VBBList s_vertexList;
        static VBBList s_indexList;

        // Static functions
        // ref: FUN_007cb990
        static void InitializeLists();

        // Member variables
        int32_t m_shared = 0;                       // +0x00: always clear in 3.3.5a
        EGxPoolTarget m_target = GxPoolTarget_Vertex;// +0x04
        EGxPoolUsage m_usage = GxPoolUsage_Static;  // +0x08
        CGxPool* m_sharedPool = nullptr;            // +0x0c: sharing mode only
        STORM_EXPLICIT_LIST(Block, link) m_freeList;// +0x14: sharing mode only
        STORM_EXPLICIT_LIST(Block, link) m_blocks;  // +0x20

        // Member functions
        // ref: FUN_007cb3b0
        Block* NewBlock();
        // Give `slot` a block holding `count` items of `stride` bytes. ref: FUN_007cbbc0
        void Alloc(Block** slot, uint32_t stride, uint32_t count);
        // ref: FUN_007cb9f0
        void Free(Block* block);
};

#endif
