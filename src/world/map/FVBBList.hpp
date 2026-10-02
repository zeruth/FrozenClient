#ifndef WORLD_MAP_F_V_B_B_LIST_HPP
#define WORLD_MAP_F_V_B_B_LIST_HPP

#include "gx/buffer/Types.hpp"
#include <cstdint>
#include <storm/List.hpp>

class CGxBuf;
class CGxPool;

// A fixed set of equal GPU buffers handed out to whoever asks and taken back from whoever has held
// one longest. The reference's name, from the RTTI string on its block record (".?AUBlock@FVBBList@@")
// and the pool names it makes ("FVBBList_vtx", "FVBBList_idx"). The map keeps one, for the low-detail
// horizon tiles (0x00adfbcc).
class FVBBList {
    public:
        // One buffer and whoever holds it (0x14 bytes).
        struct Block {
            TSLink<Block> link;             // +0x00
            uint32_t frame = 0;             // +0x08, the frame it was last drawn on
            CGxBuf* buf = nullptr;          // +0x0c
            Block** owner = nullptr;        // +0x10, the slot that points here
        };

        // Member variables
        CGxPool* m_pool = nullptr;                          // +0x00
        uint32_t m_blockBytes = 0;                          // +0x04
        STORM_EXPLICIT_LIST(Block, link) m_free;            // +0x0c
        STORM_EXPLICIT_LIST(Block, link) m_used;            // +0x18

        // Member functions
        // `count` buffers of `itemCount` items of `itemSize` bytes in one pool. ref: FUN_007d58b0
        void Initialize(EGxPoolTarget target, EGxPoolUsage usage, uint32_t itemSize, uint32_t itemCount, uint32_t count);
        // Give `slot` a buffer, taking it from the longest-idle holder if none is free -- unless
        // even that one was drawn in the last ten frames, in which case `slot` stays empty.
        // ref: FUN_007d5de0
        void Acquire(Block** slot);
        // ref: FUN_007d5540
        Block* Take(Block** slot);
        // Take a buffer back from its holder. ref: FUN_007d55b0
        void Release(Block* block);
};

#endif
