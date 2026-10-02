#include "world/map/FVBBList.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include <storm/Memory.hpp>
#include <new>

// ref: FUN_007d58b0
void FVBBList::Initialize(EGxPoolTarget target, EGxPoolUsage usage, uint32_t itemSize, uint32_t itemCount, uint32_t count) {
    this->m_blockBytes = itemSize * itemCount;
    this->m_pool = g_theGxDevicePtr->PoolCreate(target, usage, itemSize * itemCount * count, GxPoolHintBit_Unk2,
                                                target == GxPoolTarget_Vertex ? "FVBBList_vtx" : "FVBBList_idx");

    for (uint32_t i = 0; i < count; i++) {
        void* mem = SMemAlloc(sizeof(Block), ".?AUBlock@FVBBList@@", -2, 0x8);
        auto block = mem ? new (mem) Block() : nullptr;

        if (!block) {
            continue;
        }

        this->m_free.LinkToHead(block);
        block->buf = g_theGxDevicePtr->BufCreate(this->m_pool, itemSize, itemCount, this->m_blockBytes * i);
    }
}

// ref: FUN_007d5540
FVBBList::Block* FVBBList::Take(Block** slot) {
    Block* block = this->m_free.Head();

    block->link.Unlink();
    this->m_used.LinkToTail(block);
    block->buf->unk1C = 0;
    block->owner = slot;

    return block;
}

// ref: FUN_007d55b0
void FVBBList::Release(Block* block) {
    if (!block) {
        return;
    }

    *block->owner = nullptr;
    block->owner = nullptr;
    block->link.Unlink();
    this->m_free.LinkToTail(block);
}

// ref: FUN_007d5de0
void FVBBList::Acquire(Block** slot) {
    if (!this->m_free.Head()) {
        Block* oldest = this->m_used.Head();
        Block* newest = oldest;

        for (auto block = this->m_used.Head(); block; block = this->m_used.Next(block)) {
            if (block->frame < oldest->frame) {
                oldest = block;
            }

            if (newest->frame < block->frame) {
                newest = block;
            }
        }

        if (!oldest || newest->frame - oldest->frame < 11) {
            return;
        }

        this->Release(oldest);
    }

    *slot = this->Take(slot);
}
