#ifndef WORLD_MAP_C_MAP_RENDER_CHUNK_HPP
#define WORLD_MAP_C_MAP_RENDER_CHUNK_HPP

#include <storm/List.hpp>
#include <cstdint>

// The GPU-side half of a terrain chunk: 0xa0 bytes in the reference, allocated with SMemAlloc and
// recycled through CMap::s_renderChunkFreeList (CMap::AllocRenderChunk / FreeRenderChunk) rather
// than an object heap. The free-list link is its first field; a chunk points at it from +0xa8.
class CMapRenderChunk {
    public:
        // Member variables
        TSLink<CMapRenderChunk> m_link;   // +0
        // TODO +0x8..+0x9c (FUN_007b9690 constructs, FUN_007b9d60 destroys)
};

#endif
