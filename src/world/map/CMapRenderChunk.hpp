#ifndef WORLD_MAP_C_MAP_RENDER_CHUNK_HPP
#define WORLD_MAP_C_MAP_RENDER_CHUNK_HPP

#include "gx/CGxBatch.hpp"
#include "gx/Texture.hpp"
#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CGxBuf;
class CMapArea;
class CMapChunk;
class CMapChunkBufBlock;
class CMapRenderChunk;
struct SMLayer;

// One terrain layer of a render chunk (reference 0x14 bytes at +0x34 + n * 0x14)
struct CMapRenderChunkLayer {
    uint16_t flags;                   // +0x0: the MCLY flags (0x40 animated, 0x80 flag-bit into VS c1, 0x100 has alpha, 0x400 specular)
    uint16_t index;                   // +0x2
    HTEXTURE texture = nullptr;       // +0x4
    uint32_t textureId = 0xFFFFFFFF;  // +0x8: index into the tile's texture table
    HTEXTURE alphaTexture = nullptr;  // +0xc: this layer's own alpha texture, when one is built
    CMapRenderChunk* owner = nullptr; // +0x10
};

// One slot of GPU buffers a render chunk draws from (reference 0x1c bytes, two per block)
class CMapChunkBufEntry {
    public:
        CGxBuf* vertexBuf = nullptr;          // +0x0
        CGxBuf* indexBuf = nullptr;           // +0x4
        CMapRenderChunk* renderChunk = nullptr; // +0x8
        CMapChunkBufBlock* block = nullptr;   // +0xc
        uint32_t flags = 0;                   // +0x10: bits 0-1 = the render chunk covers two chunks
        TSLink<CMapChunkBufEntry> link;       // +0x14: CMap::s_freeBufEntryList
};

// Two buffer slots carved out of the render chunk pools (reference 0x4c bytes). A block serves two
// single-chunk render chunks through its entries, or one two-chunk render chunk through entry 0.
class CMapChunkBufBlock {
    public:
        CMapChunkBufBlock();
        ~CMapChunkBufBlock();

        uint32_t index = 0;                   // +0x0: slot index into the pools, in chunk units
        CMapChunkBufEntry entries[2];         // +0x4, +0x20
        TSLink<CMapChunkBufBlock> freeLink;   // +0x3c: CMap::s_freeBufBlockList
        TSLink<CMapChunkBufBlock> link;       // +0x44: CMap::s_bufBlockList
};

// The GPU-side half of a terrain chunk: 0xa0 bytes in the reference, allocated with SMemAlloc and
// recycled through CMap::s_renderChunkFreeList (CMap::AllocRenderChunk / FreeRenderChunk) rather
// than an object heap. One render chunk normally draws one chunk; with the two-chunk batching
// (m_flags bits 0-1) it draws a neighbouring pair from one buffer.
class CMapRenderChunk {
    public:
        CMapRenderChunk();
        ~CMapRenderChunk();

        // Member variables
        TSLink<CMapRenderChunk> m_link;       // +0x0: the free list, or CMap::s_activeRenderChunkList
        uint8_t m_flags = 0;                  // +0x8: bits 0-1 two-chunk batch, bit 3 = every layer texture is loaded
        uint8_t m_layerCount = 0;             // +0x9
        uint16_t m_flags10 = 0;               // +0xa: bit 0 a layer carries flag 0x80, bit 2 a specular layer, bit 3 half-size alpha, bits 3-5 alpha state
        float m_age = 0.0f;                   // +0xc: seconds unused; two of them release the buffers
        CMapChunk* m_chunk = nullptr;         // +0x10
        CMapChunk* m_chunk2 = nullptr;        // +0x14: the second chunk of a two-chunk batch
        C3Vector m_position;                  // +0x18: the draw origin (the chunk's, for local-space vertices)
        C3Vector m_center;                    // +0x24
        float m_radius = 0.0f;                // +0x30
        CMapRenderChunkLayer m_layers[4];     // +0x34
        HTEXTURE m_alphaTexture = nullptr;    // +0x84: the packed layer alphas ("TerrainBlend")
        HTEXTURE m_shadowTexture = nullptr;   // +0x88: the baked shadow
        CMapChunkBufEntry* m_bufEntry = nullptr; // +0x8c
        CGxBatch m_batch;                     // +0x90

        // Member functions
        void Init(CMapChunk* chunk, CMapChunk* chunk2, const C3Vector& position, uint8_t flags);
        void Build();
        void FillBuffers(CGxBuf* vertexBuf, CGxBuf* indexBuf);
        void DrawStreamed();
        void Bind(int32_t setup);
        void AssignLayers();
        void AddLayer(CMapArea* area, const SMLayer* layer, int32_t second);
        void CheckTexturesLoaded();
        void ReleaseLayers();
        void ReleaseBufEntry();
        void UpdateAlphaTextures();
        void SetupVertexShader(int32_t textureCount, int32_t chunkSpecular);
        void DrawLocal();
        void DrawWorld();
        void DrawSolidLocal();
        void DrawSolidWorld();
        void DrawDebug();
};

#endif
