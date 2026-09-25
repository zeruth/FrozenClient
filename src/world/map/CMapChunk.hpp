#ifndef WORLD_MAP_C_MAP_CHUNK_HPP
#define WORLD_MAP_C_MAP_CHUNK_HPP

#include "world/map/CChunkLiquid.hpp"
#include "world/map/CMapBaseObj.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CMapRenderChunk;

// The MCNK header, 128 bytes after the chunk's 8-byte IFF header, as it sits in the ADT.
struct SMChunk {
    uint32_t flags;                     // +0x00
    int32_t indexX;                     // +0x04
    int32_t indexY;                     // +0x08
    uint32_t nLayers;                   // +0x0c
    uint32_t nDoodadRefs;               // +0x10
    uint32_t ofsHeight;                 // +0x14
    uint32_t ofsNormal;                 // +0x18
    uint32_t ofsLayer;                  // +0x1c
    uint32_t ofsRefs;                   // +0x20
    uint32_t ofsAlpha;                  // +0x24
    uint32_t sizeAlpha;                 // +0x28
    uint32_t ofsShadow;                 // +0x2c
    uint32_t sizeShadow;                // +0x30
    uint32_t areaId;                    // +0x34
    uint32_t nMapObjRefs;               // +0x38
    uint16_t holes;                     // +0x3c: one bit per 2x2 cell, row-major 4x4
    uint16_t pad3e;                     // +0x3e
    uint16_t lowQualityTextureMap[8];   // +0x40
    uint32_t predTex;                   // +0x50
    uint32_t noEffectDoodad;            // +0x54
    uint32_t ofsSndEmitters;            // +0x58
    uint32_t nSndEmitters;              // +0x5c
    uint32_t ofsLiquid;                 // +0x60
    uint32_t sizeLiquid;                // +0x64
    C3Vector position;                  // +0x68
    uint32_t ofsMCCV;                   // +0x74
    uint32_t ofsMCLV;                   // +0x78
    uint32_t unused7c;                  // +0x7c
};

static_assert(sizeof(SMChunk) == 0x80, "SMChunk is 128 bytes");

// One MCLY entry
struct SMLayer {
    uint32_t textureId;
    uint32_t flags;
    uint32_t offsetInMCAL;
    uint32_t effectId;
};

// The two vertex formats the fillers write. CMap::s_terrainVertexFormat == 1 selects the one
// without the baked colour.
struct CMapChunkVertex {
    C3Vector position;
    C3Vector normal;
};

struct CMapChunkVertexColor {
    C3Vector position;
    C3Vector normal;
    uint32_t color;                     // MCCV, or 0xFF7FFFFF when the chunk has none
};

// What CMapChunk::AppendIndices accumulates one batch of chunk triangles into. Only the three
// fields the reference touches are known (its +0x8, +0xc, +0xe).
struct MAPCHUNKINDEXRANGE {
    uint32_t unk0;
    uint32_t unk4;
    uint32_t indexCount;                // +0x8, kept as a 16-bit sum
    uint16_t minIndex;                  // +0xc
    uint16_t maxIndex;                  // +0xe
};

class CMapChunk : public CMapBaseObj {
    public:
        // Static variables
        // Chunk-local XY of the 145 vertices (9 outer + 8 inner per row, 9 rows), row-major with
        // the inner row interleaved after each outer row as MCVT stores heights. z is unused.
        static float s_vertexTable[145][3];  // DAT_00d25498
        static float s_invCellSize;          // DAT_00d25488: -1 / s_vertexTable[1][1]
        // Added to a batch's max index per chunk appended (DAT_00aeec6e, set at runtime)
        static uint16_t s_vertexSpan;

        // Static functions
        static void Initialize();
        static void BuildVertexTable();

        // Member variables. Reference offsets follow the base object (which ends at +0x24).
        int32_t m_areaChunkX = 0;            // +0x24: index within the area (used & ~1)
        int32_t m_areaChunkY = 0;            // +0x28
        int32_t m_unk2c = 0;                 // +0x2c
        int32_t m_unk30 = 0;                 // +0x30
        int32_t m_indexY = 0;                // +0x34: map-wide chunk index along Y (drives the Y coordinate)
        int32_t m_indexX = 0;                // +0x38: map-wide chunk index along X (drives the X coordinate)
        C3Vector m_center;                   // +0x3c
        float m_radius = 0.0f;               // +0x48
        CAaBox m_bounds;                     // +0x4c .. +0x60
        // TODO +0x64..+0x78
        C3Vector m_position;                 // +0x7c: chunk origin; only z is added to heights
        float m_sortDistance = 0.0f;         // +0x88: distance along the camera forward
        CAaBox m_bounds2;                    // +0x8c .. +0xa0: a copy of m_bounds after ComputeBounds
        void* m_ptrA4 = nullptr;             // +0xa4: released with FUN_007b3960 on destroy
        CMapRenderChunk* m_renderChunk = nullptr; // +0xa8
        int32_t m_renderChunkReady = 0;      // +0xac
        // TODO +0xb0..+0xb8
        TSLink<CMapChunk> m_areaLink;        // +0xbc
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_entityLinkList;     // +0xc4: owners released on destroy
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_mapObjDefLinkList;  // +0xd0: owners released on destroy
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_linkListDc;         // +0xdc
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_linkListE8;         // +0xe8
        // TODO +0xf4: list of SI2SoundHandle (freed with FUN_007c3330 on destroy)
        STORM_EXPLICIT_LIST(CChunkLiquid, m_chunkLink) m_liquidList;        // +0x100
        uint8_t* m_data = nullptr;           // +0x10c: the 'MCNK' IFF chunk
        SMChunk* m_header = nullptr;         // +0x110: m_data + 8
        // TODO +0x114, +0x118
        float* m_heights = nullptr;          // +0x11c: MCVT
        uint32_t* m_vertexColors = nullptr;  // +0x120: MCCV (packed bytes)
        int8_t* m_normals = nullptr;         // +0x124: MCNR
        uint8_t* m_shadow = nullptr;         // +0x128: MCSH
        SMLayer* m_layers = nullptr;         // +0x12c: MCLY
        uint8_t* m_alpha = nullptr;          // +0x130: MCAL
        uint32_t* m_refs = nullptr;          // +0x134: MCRF
        uint8_t* m_liquidData = nullptr;     // +0x138: MCLQ (legacy liquid)
        uint8_t* m_soundEmitters = nullptr;  // +0x13c: MCSE

        // Member functions
        void ParseSubChunks(int32_t fixSizes);
        int16_t BuildIndices(uint16_t* indices, int16_t baseVertex);
        void AppendIndices(uint16_t* indices, MAPCHUNKINDEXRANGE* range);
        void BuildVertices(void* buffer, int32_t vertexBase, int32_t a3);
        void FillVerticesWorld(CMapChunkVertex* dst, int32_t a2);
        void FillVerticesWorldColor(CMapChunkVertexColor* dst, int32_t a2);
        void FillVerticesLocal(CMapChunkVertex* dst);
        void FillVerticesLocalColor(CMapChunkVertexColor* dst);
        void ComputeBounds();
        void GetBounds(CAaBox* box);
};

#endif
