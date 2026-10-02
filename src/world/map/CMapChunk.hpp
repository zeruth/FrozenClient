#ifndef WORLD_MAP_C_MAP_CHUNK_HPP
#define WORLD_MAP_C_MAP_CHUNK_HPP

#include "world/map/CChunkLiquid.hpp"
#include "world/map/CMapBaseObj.hpp"
#include "world/map/DetailDoodad.hpp"
#include "gx/CGxBatch.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Ray.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CMapArea;
class CWFrustum;
class CiRect;
struct CFacetList;
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

// One layer's alpha as the unpackers walk it: the MCLY flags and the next MCAL row
struct SMLayerAlpha {
    uint32_t flags = 0;
    const uint8_t* alpha = nullptr;
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

class CMapChunk : public CMapBaseObj {
    public:
        // Static variables
        // Chunk-local XY of the 145 vertices (9 outer + 8 inner per row, 9 rows), row-major with
        // the inner row interleaved after each outer row as MCVT stores heights. z is unused.
        static float s_vertexTable[145][3];  // DAT_00d25498
        static float s_invCellSize;          // DAT_00d25488: -1 / s_vertexTable[1][1]
        // Row-major bit for each 2x2 cell of a chunk's 4x4 hole grid (DAT_00a3faf0). A static
        // here rather than a file-local table because the terrain decal collector in
        // CMapObjGroup.cpp reads it too, as the reference's own module-level table is read across
        // translation units.
        static const uint16_t s_holeMask[16];

        // Raise the horizon along this chunk's two far edges, the ones facing the camera.
        // ref: FUN_007cfb10
        void FeedHorizon();

        // The alpha unpackers' scratch: a decompressed row per layer, and the rows a chunk
        // without MCSH or a layer without MCAL read (CMapChunkAlpha.cpp)
        static uint8_t s_alphaRows[4][0x40];
        static uint8_t s_zeroShadowRow[0x40];
        static uint8_t s_zeroAlphaRow[0x80];

        // Static functions
        static void Initialize();
        static void BuildVertexTable();
        static const uint8_t* DecompressAlphaRow(uint8_t* dst, int32_t count, const uint8_t* src);
        static void NextAlphaRows(const uint8_t** shadow, const uint8_t** shadowRow, SMLayerAlpha* layers, uint32_t size, const uint8_t** rows);
        static void NextNibbleRows(uint32_t size, const uint8_t** shadow, const uint8_t** shadowRow, SMLayerAlpha* layers, const uint8_t** rows);
        static void GatherLayerAlphas(const CMapRenderChunk* renderChunk, const CMapChunk* chunk, SMLayerAlpha* out);
        static void PackNibbleRow(uint16_t* dst, int32_t count, const uint8_t** rows, const uint8_t* shadowRow);
        static void UnpackAlphaBits(const CMapRenderChunk* renderChunk, void* dst, uint32_t size, SMLayerAlpha* layer, const uint8_t* shadow, int32_t genFormat, uint32_t bigAlpha);
        static void UnpackAlphaShadowBits(const CMapRenderChunk* renderChunk, void* dst, uint32_t offset, uint32_t pitch, uint32_t size, SMLayerAlpha* layers, uint32_t baseLayer, const uint8_t* shadow, int32_t genFormat, uint32_t bigAlpha);
        static void UnpackShadowBits(uint16_t* dst, uint32_t size, const uint8_t* shadow);
        static void UnpackShadowBitsHalf(uint16_t* dst, uint32_t size, const uint8_t* shadow);

        // Member variables. Reference offsets follow the base object (which ends at +0x24).
        int32_t m_areaChunkX = 0;            // +0x24: index within the area (used & ~1)
        int32_t m_areaChunkY = 0;            // +0x28
        int32_t m_cellY = 0;                 // +0x2c: m_indexY * 8, the chunk's first cell along Y
        int32_t m_cellX = 0;                 // +0x30: m_indexX * 8
        int32_t m_indexY = 0;                // +0x34: map-wide chunk index along Y (the tile's column index, drives the Y coordinate)
        int32_t m_indexX = 0;                // +0x38: map-wide chunk index along X (the tile's row index, drives the X coordinate)
        C3Vector m_center;                   // +0x3c
        float m_radius = 0.0f;               // +0x48
        CAaBox m_bounds;                     // +0x4c .. +0x60
        CAaBox m_liquidBounds;               // +0x64 .. +0x78: of the chunk's liquids; min above max when there are none
        C3Vector m_position;                 // +0x7c: chunk origin; only z is added to heights
        float m_sortDistance = 0.0f;         // +0x88: distance along the camera forward
        CAaBox m_bounds2;                    // +0x8c .. +0xa0: a copy of m_bounds after ComputeBounds
        // The chunk's scattered grass, taken from the WDETAILDOODADINST heap by the
        // scatter builder and given back to it on destroy (FUN_007b3960). Null until the
        // chunk has come near enough to scatter.
        DetailDoodad::CDetailDoodadData* m_detailDoodads = nullptr;     // +0xa4
        CMapRenderChunk* m_renderChunk = nullptr; // +0xa8
        int32_t m_renderChunkReady = 0;      // +0xac
        uint32_t m_areaId = 0;               // +0xb0: the MCNK header's areaId
        TSLink<CMapChunk> m_rowLink;         // +0xb4: the distance row's chunk list (CWorldScene::s_rows)
        TSLink<CMapChunk> m_frameLink;       // +0xbc: CMap::s_frameChunkList, emptied every update, or a row's occluder list
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_entityLinkList;     // +0xc4: owners released on destroy
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_mapObjDefLinkList;  // +0xd0: owners released on destroy
        // Everything standing ON this chunk. LinkEntityToChunks (FUN_007c2040) files an entity
        // or a doodad def here for every chunk its box covers, so a chunk can reach the things
        // resting on it without searching. Named by its writer 2026-09-27; it was m_linkListDc.
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_groundedLinkList;   // +0xdc
        STORM_EXPLICIT_LIST(CMapBaseObjLink, refLink) m_linkListE8;         // +0xe8
        // TODO +0xf4: list of SI2SoundHandle (freed with FUN_007c3330 on destroy)
        STORM_EXPLICIT_LIST(CChunkLiquid, m_chunkLink) m_liquidList;        // +0x100
        uint8_t* m_data = nullptr;           // +0x10c: the 'MCNK' IFF chunk
        SMChunk* m_header = nullptr;         // +0x110: m_data + 8
        uint16_t* m_lowQualityTextureMap = nullptr; // +0x114: &m_header->lowQualityTextureMap
        uint32_t* m_predTex = nullptr;       // +0x118: &m_header->predTex
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
        // A ray (in the chunk's own space) against the four triangles of one of its 8x8 cells,
        // unless the cell is a hole. `t` keeps the nearest positive hit. ref: FUN_007d8730
        bool IntersectCell(uint32_t cellX, uint32_t cellY, const C3Ray& ray, float* t);
        // The terrain triangles of the cells in `cells` (chunk-local rows and columns) that the
        // frustum (already in the chunk's own space) does not reject, appended to the list in
        // world space. ref: FUN_007d8e00
        void GatherFacets(const CiRect& cells, const CWFrustum& frustum, CFacetList& list);
        // The surface height of the first of the chunk's liquid layers that covers the
        // cell point `cells` (map cells, from y then from x). ref: FUN_007c55d0
        bool GetLiquidHeight(const float* cells, float* height);

        CMapChunk();
        ~CMapChunk() override;
        void Load(uint8_t* data, int32_t fixSizes);
        void CreateRefs(CMapArea* area, const uint32_t* refs, uint32_t doodadCount, uint32_t mapObjCount);
        // Replace the chunk's liquid layers with the ones its data now describes, from
        // whichever of the two formats the tile carries. ref: FUN_007c5690
        void CreateLiquid();
        void Destroy();
        void ParseSubChunks(int32_t fixSizes);
        int16_t BuildIndices(uint16_t* indices, int16_t baseVertex);
        void AppendIndices(uint16_t* indices, CGxBatch* batch);
        void BuildVertices(void* buffer, int32_t vertexBase, const C3Vector* offset);
        void FillVerticesWorld(CMapChunkVertex* dst, const C3Vector* offset);
        void FillVerticesWorldColor(CMapChunkVertexColor* dst, const C3Vector* offset);
        void CreateRenderChunk();
        void EnsureRenderChunk();
        int32_t CanPairWith(CMapChunk* other);
        uint32_t AlphaSize() const;
        void UpdateSortDistance();
        void UpdateLiquidVisibility();
        void PrepareRender();
        void FillVerticesLocal(CMapChunkVertex* dst);
        void FillVerticesLocalColor(CMapChunkVertexColor* dst);
        void ComputeBounds();
        void GetBounds(CAaBox* box);
        // The terrain height at a point inside one of the chunk's cells, from the plane of
        // whichever of that cell's four triangles the point lands in. False where the chunk has
        // a hole there. `col` is the cell along y and `row` the cell along x, both already
        // reduced to the chunk. ref: FUN_007ad3b0
        bool HeightAt(const C3Vector& position, uint32_t col, uint32_t row, float* height);
        // Whether every detail doodad kind this chunk's texture layers call for has its
        // model in. Asking starts the loads. ref: FUN_007d05f0
        bool DetailDoodadsReady();
};

#endif
