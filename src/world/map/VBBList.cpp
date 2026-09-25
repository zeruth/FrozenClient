#include "world/map/VBBList.hpp"

#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"

#include <storm/Memory.hpp>

VBBList VBBList::s_vertexList;
VBBList VBBList::s_indexList;

// ref: FUN_007cb990
// Both lists take the same usage, which CWorld::Initialize has already set to dynamic.
void VBBList::InitializeLists() {
    VBBList::s_vertexList.m_shared = 0;
    VBBList::s_vertexList.m_target = GxPoolTarget_Vertex;
    VBBList::s_vertexList.m_usage = GxPoolUsage_Dynamic;
    VBBList::s_vertexList.m_sharedPool = nullptr;

    VBBList::s_indexList.m_shared = 0;
    VBBList::s_indexList.m_target = GxPoolTarget_Index;
    VBBList::s_indexList.m_usage = GxPoolUsage_Dynamic;
    VBBList::s_indexList.m_sharedPool = nullptr;
}

// ref: FUN_007cb3b0
VBBList::Block* VBBList::NewBlock() {
    if (this->m_shared) {
        // TODO the sharing path takes a block off the free list, or makes one and gives it a
        // buffer in the shared pool. Nothing sets m_shared in 3.3.5a.
        return nullptr;
    }

    auto block = STORM_NEW(VBBList::Block);

    if (!block) {
        return nullptr;
    }

    this->m_blocks.LinkToHead(block);

    return block;
}

// ref: FUN_007cbbc0
// Without sharing, every request gets its own pool and buffer. The pool is made a page
// larger than the run needs, and one item wider, which is what the reference asks for.
void VBBList::Alloc(Block** slot, uint32_t stride, uint32_t count) {
    if (this->m_shared) {
        // TODO the sharing path walks the block list for one big enough to carve, and frees
        // the smallest in use when none is. Nothing sets m_shared in 3.3.5a.
        return;
    }

    auto block = this->NewBlock();

    if (!block) {
        return;
    }

    uint32_t size = (count + 1) * stride;

    block->pool = g_theGxDevicePtr->PoolCreate(
        this->m_target,
        this->m_usage,
        size + 0x1000,
        GxPoolHintBit_Unk3,
        this->m_target == GxPoolTarget_Index ? "VBBList_idx" : "VBBList_vtx"
    );

    block->buf = g_theGxDevicePtr->BufCreate(block->pool, stride, count, 0);
    block->base = nullptr;
    block->size = size;
    block->owner = slot;

    *slot = block;
}

// ref: FUN_007cb9f0
void VBBList::Free(Block* block) {
    if (!block) {
        return;
    }

    if (this->m_shared) {
        // TODO the sharing path folds the block back into whichever of its neighbours are
        // also free. Nothing sets m_shared in 3.3.5a.
        return;
    }

    // TODO the reference destroys the buffer (FUN_006c42b0) and then the pool, through the
    // device vfunc at +0xd4. frozen's device exposes neither, so the GPU side is reclaimed
    // only when the device tears its pools down.
    block->buf = nullptr;
    block->pool = nullptr;

    if (block->owner) {
        *block->owner = nullptr;
    }

    this->m_blocks.UnlinkNode(block);
    STORM_FREE(block);
}
