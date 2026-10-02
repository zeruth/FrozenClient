#ifndef WORLD_MAP_C_MAP_OBJ_GROUP_HPP
#define WORLD_MAP_C_MAP_OBJ_GROUP_HPP

#include "gx/CGxBatch.hpp"
#include "gx/Types.hpp"
#include "world/map/VBBList.hpp"
#include <storm/Array.hpp>
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Plane.hpp>
#include <tempest/Ray.hpp>
#include <tempest/Segment.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CAsyncObject;
class CGxBuf;
class CMapObj;
class CWFrustum;
class CMapObjGroup;
struct SMOMaterial;

// MOBN: one node of the group's axis-aligned BSP over its faces. The name and layout are the
// format's own; the reference walks these 16-byte records in place.
struct CAaBspNode {
    enum {
        Flag_XAxis    = 0x0,
        Flag_YAxis    = 0x1,
        Flag_ZAxis    = 0x2,
        Flag_AxisMask = 0x3,
        Flag_Leaf     = 0x4,
    };

    static constexpr int16_t NoChild = -1;

    uint16_t flags;
    int16_t negChild;
    int16_t posChild;
    uint16_t nFaces;
    uint32_t faceStart;
    float planeDist;
};

// MOPY: a face's flags and material.
struct SMOPoly {
    enum {
        F_UNK_0x01      = 0x1,
        F_NOCAMCOLLIDE  = 0x2,
        F_DETAIL        = 0x4,
        F_COLLISION     = 0x8,
        F_HINT          = 0x10,
        F_RENDER        = 0x20,
        F_UNK_0x40      = 0x40,
        F_COLLIDE_HIT   = 0x80, // set on every face a query visits, cleared when the query ends
    };

    uint8_t flags;
    uint8_t material;
};

// The per-query state a segment query keeps on the caller's stack (reference: 0x5c bytes, built
// by FUN_007c78e0). It carries the group's arrays so the leaf tests need no group pointer, and the
// running nearest hit.
struct CMapObjGroupSegmentQuery {
    uint32_t* overflow;           // |= 1 when the visited-face list fills up (may be null)
    SMOPoly* polys;               // MOPY
    const C3Vector* vertices;     // MOVT
    const uint16_t* indices;      // MOVI
    float* outT;                  // where the hit's segment parameter is written
    float maxT;                   // the caller's cap on *outT (its value on entry)
    C3Segment segment;
    C3Ray ray;                    // segment.start and its unit direction
    float invLength;              // 1 / |segment|
    float bestDist;               // nearest hit so far along ray.dir; starts at maxT * |segment|
    const SMOMaterial* materials; // MOMT, for the textured / untextured face test
    uint32_t materialCount;       // so the lookup can be bounds-checked; see TestFace
    uint32_t unk15;               // the reference copies a global (DAT_00cf08f8) here; unread by the ported callees
    uint16_t skipFlags;           // faces with any of these MOPY flags are ignored; always includes F_COLLIDE_HIT

    void Init(SMOPoly* polys, const C3Vector* vertices, const uint16_t* indices, const C3Segment& segment, float* t, uint16_t skipFlags, const SMOMaterial* materials, uint32_t materialCount);
    bool CachedLeaf(CMapObjGroup* group, const CAaBspNode* node);
    void TestFace(uint16_t face);
};

// The segment query that tracks two nearest hits: the nearest collision face (F_RENDER or
// F_COLLISION) and the nearest rendered face (F_RENDER or F_DETAIL). Reference: 0x60 bytes,
// built by FUN_007c77d0.
struct CMapObjGroupDualSegmentQuery {
    uint32_t* overflow;
    SMOPoly* polys;
    const C3Vector* vertices;
    const uint16_t* indices;
    C3Ray ray;
    C3Segment segment;
    float invLength;
    float maxCollisionT;          // caller caps on the two results, as segment parameters
    float maxRenderT;
    float bestCollisionDist;      // running nearest hits along ray.dir
    float bestRenderDist;
    int32_t collisionFace;        // -1 until a hit lands
    int32_t renderFace;
    uint32_t skipFlags;           // F_COLLIDE_HIT | F_NOCAMCOLLIDE

    void Init(SMOPoly* polys, const C3Vector* vertices, const uint16_t* indices, const C3Segment& segment, float maxCollisionT, float maxRenderT);
    bool CachedLeaf(CMapObjGroup* group, const CAaBspNode* node);
    void TestFace(uint16_t face);
    bool Results(float* collisionT, int32_t* collisionFace, float* renderT, int32_t* renderFace);
};

// The box query: every face that reaches into a six-plane hull. Reference: built inline by
// FUN_007cb180.
// The plain-box sibling of CMapObjGroupBoxQuery: the same fields, with the box itself in place of
// the hull, and faces kept when the triangle is not wholly outside it. FUN_007cb7b0.
class CM2Model;

struct CMapObjGroupAaBoxQuery {
    uint32_t* overflow;
    SMOPoly* polys;
    const C3Vector* vertices;
    const uint16_t* indices;
    const CAaBox* box;
    uint16_t skipFlags;

    bool CachedLeaf(CMapObjGroup* group, const CAaBspNode* node);
    void TestFace(uint16_t face);
};

struct CMapObjGroupBoxQuery {
    uint32_t* overflow;
    SMOPoly* polys;
    const C3Vector* vertices;
    const uint16_t* indices;
    const C4Plane* hull;          // six planes, inward facing
    uint16_t skipFlags;

    void TestFace(uint16_t face);
};

// The record a query leaves behind for its caller: the faces it hit, with the index range and
// the placement to bring them to world space (reference: a 0x24-byte entry in a pool of 32).
struct CMapObjHitRecord {
    const C44Matrix* placement;   // the instance's placement matrix
    const C3Vector* vertices;     // MOVT
    uint32_t vertexCount;
    // The receiver's SEPARATE HEIGHT ARRAY, one float per vertex, and the thing that lets a terrain
    // chunk be a receiver at all: the terrain hit collector (FUN_007a6260) points `vertices` at the
    // single shared CMapChunk::s_vertexTable, which carries chunk-local XY only, and puts the
    // chunk's own MCVT here. Non-zero switches both decal stream builders (FUN_007e2fd0,
    // FUN_007e32f0) to their one-vertex-per-index path, where each vertex's z comes from this array
    // instead of from `vertices`. Null for a WMO receiver, whose vertices carry their own z.
    const float* heights;
    uint16_t* indices;            // three per hit face, from the shared index pool
    uint16_t* faces;              // hit face numbers, from the shared face pool
    uint16_t indexCount;
    uint16_t faceCount;
    uint16_t minIndex;
    uint16_t maxIndex;
    void* object;                 // whatever the caller passed as the hit's owner
};

// MOBA: one draw batch of a group, 24 bytes.
struct SMOBatch {
    int16_t bounds[6];          // +0x00: the batch's box in group space, as integers
    uint32_t startIndex;        // +0x0c: into MOVI
    uint16_t count;             // +0x10
    uint16_t minVertex;         // +0x12
    uint16_t maxVertex;         // +0x14
    uint8_t flags;              // +0x16
    uint8_t materialId;         // +0x17
};

static_assert(sizeof(SMOBatch) == 0x18, "SMOBatch is 24 bytes");

// The map object half of a shadow map: each group drawn through its placement, moved camera-
// relative, if its placed box meets the frustum. A group whose batches are all untextured (state
// bit 4) goes in one draw at alpha ref 0; the rest draw batch by batch with their own texture,
// alpha-tested at 224/255 when the material blends by mode 1. ref: FUN_007ab760
void MapObjDrawShadowCasters(CMapObjGroup* const* groups, uint32_t count, const C44Matrix* const* placements,
                             const C44Matrix& toCamera, const CWFrustum& frustum);

// ref: FUN_007c7a00
bool TriangleOutsideBox(const CAaBox& box, const C3Vector& a, const C3Vector& b, const C3Vector& c);

// The world box query: everything under an axis-aligned box that can receive a projected decal,
// appended to the shared hit-record pool. The decal module resets the pool, calls this, and then
// re-draws each record's triangles (src/world/Shadow.cpp).
//
// `queryMask` selects which classes of geometry the walk visits, and the bits are not independent:
//   & 0x300f0  the loaded map-object instances are walked
//   & 0x30100  the terrain chunks are walked
//   & 0x100    within a chunk, its terrain triangles are collected
//   & 0x30000  within a chunk or a group, its doodads are collected as well
// The blob shadow passes 0x220122, so it collects terrain triangles and WMO faces but not doodads
// of either kind.
//
// ref: FUN_007a6af0
bool MapQueryBox(const CAaBox& box, void* object, uint32_t queryMask);

// Which z face of the box a vertex lies outside of, as outcode bits 0x04 and 0x20. Shared by the
// chunk and group liquid collectors. ref: FUN_007c7790 (CChunkLiquid.cpp)
uint8_t ClassifyCornerZ(const CAaBox& box, const C3Vector& v);

// The map-object half of MapQueryBox: every placed building the box reaches, in its own space.
// ref: FUN_007a6940
bool MapQueryBoxMapObjs(const CAaBox& box, void* object, uint32_t queryMask);

// Up to `maxModels` doodad models inside placed buildings whose bounds meet the box, for the
// decal's M2 receivers. Returns how many. ref: FUN_007a2aa0
uint32_t MapQueryBoxModels(CM2Model** models, uint32_t maxModels, const CAaBox& box, uint32_t queryMask);

// The terrain half on its own, reached through the dispatcher above: turn the box into a range of
// chunks and collect each one.
// ref: FUN_007a6830
bool MapQueryBoxTerrain(const CAaBox& box, void* object, uint32_t queryMask);

class CMapObjGroup {
    public:
        // Static variables: the pool of hit records the queries append to (reference globals
        // DAT_00cd8080.., DAT_00cb7528..). Callers reset the counters before a query and read the
        // records after it.
        static uint32_t s_hitFlags;              // DAT_00cb7528: bit 0 = a pool overflowed
        static uint32_t s_hitRecordCount;        // DAT_00cb752c
        static uint32_t s_hitFacePoolCount;      // DAT_00cb7530
        static uint32_t s_hitIndexPoolCount;     // DAT_00cb7534
        // DAT_00cb7538: the cursor into the placement pool below, which the terrain hit collector
        // needs because a terrain chunk has no instance matrix of its own to point a record at.
        // Every query resets it.
        static uint32_t s_hitPlacementCount;
        static CMapObjHitRecord s_hitRecords[0x20];
        // DAT_00cd7880: 32 placement matrices, immediately below the record pool in the reference
        // and the same count, one per record.
        static C44Matrix s_hitPlacements[0x20];
        static uint16_t s_hitFacePool[0x4000];   // DAT_00cb7540
        static uint16_t s_hitIndexPool[0xc000];  // DAT_00cbf540

        // Static functions
        static CMapObjHitRecord* AllocHitRecord(uint32_t indexCount, uint32_t faceCount);
        // ref: FUN_007a6140
        static CMapObjHitRecord* AllocHitRecord();
        // ref: FUN_007a6190
        static uint16_t* AllocHitIndices(uint32_t count);
        // The placement pool's bump allocator, inlined at its one call site in the reference
        // (FUN_007a6260) the way AllocHitRecord is not.
        static C44Matrix* AllocHitPlacement();
        static void QueryEnd(SMOPoly* polys);

        // Member variables, with the reference's offsets.
        uint32_t m_memHandle = 0;                // +0x00: CMap::s_mapObjGroupHeap slot

        // The buffers the group draws from, built on first draw and dropped again once it has
        // gone five seconds without one
        VBBList::Block* m_vertexBuf = nullptr;   // +0x04: position, normal, colour, uv
        VBBList::Block* m_colorBuf = nullptr;    // +0x08: a second colour set, outdoor groups only
        VBBList::Block* m_indexBuf = nullptr;    // +0x0c

        // +0x10 and +0x14: a SECOND buffer pair, for the group's liquid surface only, parallel to
        // the three above and allocated out of the same VBBList. Identified from FUN_007cbdc0, which
        // fills whichever of the two is still null and then clears the CGxBuf's ready flag through
        // the block's buf at +0x18 -- an offset frozen's VBBList::Block already carries.
        VBBList::Block* m_liquidVertexBuf = nullptr;
        VBBList::Block* m_liquidIndexBuf = nullptr;
        // +0x20: the batch the liquid surface was last built into. CMeshGeomFactory::Build copies it
        // out on a cache hit and fills it on a rebuild, reaching it through the holder
        // AcquireLiquidBuffers hands back -- which is what that holder actually points at.
        CGxBatch m_liquidBatch = {};
        float m_bufferIdleTime = 0.0f;           // +0x18

        // The liquid surface's vertex positions, built from MLIQ. The reference keeps these in a
        // pooled VertArray reached through a pointer at +0x1c; frozen owns the array (diverged).
        TSGrowableArray<C3Vector> m_liquidVertices;

        uint32_t m_flags = 0;                    // +0x30: MOGP flags
        CAaBox m_mogpBounds;                     // +0x34: the group's own box, from MOGP
        float m_nearestDistanceSq = 0.0f;        // +0x4c: to the streaming target
        uint32_t m_portalStart = 0;              // +0x50: into the root's MOPR
        uint32_t m_portalCount = 0;              // +0x54
        uint32_t m_fogIds = 0;                   // +0x58: four MFOG indices, one byte each
        uint16_t m_batchCountA = 0;              // +0x5c: the trans, interior and exterior batch
        uint16_t m_batchCountB = 0;              // +0x5e   counts; the three runs partition MOBA
        uint16_t m_batchCountC = 0;              // +0x60   in that order

        // The BSP over the group's faces (the reference gives it its own object at +0x64)
        CAaBspNode* m_bspNodes = nullptr;        // +0x68: MOBN
        uint32_t m_bspNodeCount = 0;             // +0x70
        uint16_t* m_bspFaceRefs = nullptr;       // +0x6c: MOBR
        uint32_t m_bspFaceRefCount = 0;          // +0x74
        CAaBox m_bounds;                         // +0xb0: the BSP's own copy of the group box

        const char* m_name = nullptr;            // +0xd8: into the root's MOGN

        // The group file's chunks, pointed straight into m_fileBuffer (nothing here is owned)
        SMOPoly* m_polys = nullptr;              // +0xdc: MOPY
        const uint16_t* m_indices = nullptr;     // +0xe0: MOVI
        const uint16_t* m_triangleStrips = nullptr; // +0xe4: MORI
        const C3Vector* m_vertices = nullptr;    // +0xe8: MOVT
        const C3Vector* m_normals = nullptr;     // +0xec: MONR
        const C2Vector* m_texCoords = nullptr;   // +0xf0: MOTV
        const C2Vector* m_texCoords2 = nullptr;  // +0xf4: the second MOTV
        SMOBatch* m_batches = nullptr;           // +0xf8: MOBA
        const uint8_t* m_morb = nullptr;         // +0xfc: MORB, the ranges MORI's strips go with
        const uint16_t* m_lightRefs = nullptr;   // +0x100: MOLR
        const uint16_t* m_doodadRefs = nullptr;  // +0x104: MODR
        CImVector* m_colors = nullptr;           // +0x108: MOCV
        const CImVector* m_colors2 = nullptr;    // +0x10c: the second MOCV

        // MLIQ: a grid of liquid heights over the group, with a flag byte per tile
        // +0x110: how many surfaces have taken the liquid buffer pair. FUN_007cbdc0 bumps it and
        // nothing read so far lowers it, so its only established use is as a claim count.
        uint32_t m_liquidBufferUsers = 0;
        // What the liquid buffer pair was actually made for, so a later build at a different vertex
        // format or a bigger grid reallocates instead of writing at the wrong stride.
        uint32_t m_liquidBufStride = 0;
        uint32_t m_liquidBufVertices = 0;
        uint32_t m_liquidBufIndices = 0;

        uint32_t m_liquidXVerts = 0;             // +0x114
        uint32_t m_liquidYVerts = 0;             // +0x118
        uint32_t m_liquidXTiles = 0;             // +0x11c
        uint32_t m_liquidYTiles = 0;             // +0x120
        C3Vector m_liquidPos;                    // +0x124: the grid's corner, group-local
        uint16_t m_liquidMaterial = 0;           // +0x130
        const uint8_t* m_liquidVerts = nullptr;  // +0x134: eight bytes each, the height at +4
        const uint8_t* m_liquidTiles = nullptr;  // +0x138: one byte each
        float m_liquidMinZ = 0.0f;               // +0x13c
        float m_liquidMaxZ = 0.0f;               // +0x140
        uint32_t m_liquidType = 0;               // +0x144: the LiquidType row, after the fixup
        // +0x148: how many liquid tiles actually render, cached. LiquidTileCount() fills it and is
        // the only writer; a value below 1 means "not computed yet", which is also why a group whose
        // tiles all turn out to be hidden recomputes the zero every time it is asked. The mesh
        // builder for map-object water needs it -- its vertex count is
        // m_liquidXVerts * m_liquidYVerts + m_liquidTileCount * 6.
        int32_t m_liquidTileCount = 0;

        uint32_t m_faceCount = 0;                // +0x150: MOPY
        uint32_t m_indexCount = 0;               // +0x154: MOVI
        uint32_t m_triangleStripCount = 0;       // +0x158: MORI
        uint32_t m_vertexCount = 0;              // +0x15c: MOVT
        uint32_t m_normalCount = 0;              // +0x160: MONR
        uint32_t m_texCoordCount = 0;            // +0x164: MOTV
        uint32_t m_texCoord2Count = 0;           // +0x168
        uint32_t m_batchCount = 0;               // +0x16c: MOBA
        uint32_t m_lightRefCount = 0;            // +0x170
        uint32_t m_doodadRefCount = 0;           // +0x174
        uint32_t m_colorCount = 0;               // +0x178: MOCV
        uint32_t m_color2Count = 0;              // +0x17c
        uint32_t m_groupID = 0;                  // +0x180: the group's WMOGroupID

        uint8_t* m_fileBuffer = nullptr;         // +0x184: the whole group file, owned
        uint32_t m_fileSize = 0;                 // +0x188
        CMapObj* m_mapObj = nullptr;             // +0x18c: the root
        CAsyncObject* m_asyncObject = nullptr;   // +0x194: the read in flight
        uint32_t m_state = 0;                    // +0x198: bit 0 loaded, bit 1 the root attenuates
                                                 //   vertex colour, bit 2 every batch is
                                                 //   untextured, bit 3 a batch blends by mode 6
        uint32_t m_unk190 = 0;                   // +0x190: cleared every def update
        uint32_t m_minIndex = 0;                 // +0x19c: over the batches
        uint32_t m_maxIndex = 0;                 // +0x1a0
        uint16_t m_minVertex = 0;                // +0x1a4
        uint16_t m_maxVertex = 0;                // +0x1a6
        TSLink<CMapObjGroup> m_link;             // +0x1b4: unlinked by CMap::FreeMapObjGroup

        // Member functions

        // Count the liquid tiles that render, into m_liquidTileCount. Cheap after the first call.
        // ref: FUN_00431f30
        // The LiquidType.dbc id this group's water uses.
        uint32_t GetLiquidType();

        void LiquidTileCount();

        // ref: FUN_007cbdc0
        // Make sure the liquid buffer pair exists, sized for this surface, and hand back the holder
        // the geometry factory caches.
        void AcquireLiquidBuffers(EGxVertexBufferFormat format, uint32_t vertexCount,
                                 uint32_t indexCount, void** holderOut);
        ~CMapObjGroup();
        void FreeQueryData();

        bool QuerySegment(const C3Segment& segment, float* t, uint32_t queryFlags, uint16_t skipFlags, void* unused, const C44Matrix* placement, void* object);
        bool QuerySegmentFace(const C3Segment& segment, float* t, uint32_t queryFlags, uint16_t skipFlags, uint32_t* outFace);
        bool QuerySegmentDual(const C3Segment& segment, float* collisionT, int32_t* collisionFace, float* renderT, int32_t* renderFace);
        bool QueryBox(const CWFrustum& frustum, uint32_t queryFlags, uint16_t skipFlags, const C44Matrix* placement, void* object);
        bool SampleColorAtFace(const C3Vector& point, uint16_t face, CImVector* outColor, uint8_t* outFlag);

        void SegmentQueryNode(CMapObjGroupSegmentQuery& query, int32_t nodeIdx, const C3Segment& segment, const CAaBox& box);
        void SegmentQueryLeaf(CMapObjGroupSegmentQuery& query, const CAaBspNode* node);
        void DualQueryNode(CMapObjGroupDualSegmentQuery& query, int32_t nodeIdx, const C3Segment& segment, const CAaBox& box);
        void DualQueryLeaf(CMapObjGroupDualSegmentQuery& query, const CAaBspNode* node);
        void BoxQueryNode(CMapObjGroupBoxQuery& query, int32_t nodeIdx, const CAaBox& queryBox, const CAaBox& box);
        void BoxQueryLeaf(CMapObjGroupBoxQuery& query, const CAaBspNode* node);
        bool QueryAaBox(const CAaBox& box, uint32_t queryFlags, uint16_t skipFlags, const C44Matrix* placement, void* object);
        bool QueryLiquidBox(const CAaBox& box, uint32_t queryFlags, const C44Matrix* placement, void* object);
        // QueryLiquidBox against a frustum: the grid range from the frustum moved into the
        // grid's own space, each vertex classified against the six planes. ref: FUN_007cab70
        bool QueryLiquidHull(const CWFrustum& frustum, uint32_t queryFlags, const C44Matrix* placement, void* object);
        void AaBoxQueryNode(CMapObjGroupAaBoxQuery& query, int32_t nodeIdx, const CAaBox& queryBox, const CAaBox& box);
        void AaBoxQueryLeaf(CMapObjGroupAaBoxQuery& query, const CAaBspNode* node);
        void RecordHits(const C44Matrix* placement, void* object, uint32_t flags);

        // Loading the group file
        static void Read(CMapObj* mapObj, uint32_t groupIndex, int32_t sync);
        static void ReadCallback(void* arg);
        static uint32_t ResolveLiquidType(uint32_t flags, uint32_t liquid);
        void ReadComplete();
        void ParseChunks(const uint8_t* cursor);
        void ParseOptionalChunks(const uint8_t* cursor);
        void SetBsp(CAaBspNode* nodes, uint32_t nodeCount, uint16_t* faceRefs, uint32_t faceRefCount, const CAaBox& bounds);
        void FixVertexColors();
        void LoadMaterialTextures(uint32_t materialId);
        // The liquid surface at a point, in this GROUP's own space: the MLIQ height grid sampled
        // bilinearly, gated on the tile under the point actually carrying liquid. Answers only when
        // the point is BELOW the surface, which is what makes it a submersion test rather than a
        // height probe. ref: FUN_007c8360
        bool GetLiquidAt(const C3Vector& localPos, uint32_t* outType, float* outHeight);

        uint8_t FirstLiquidTileType() const;
        void BuildLiquidVertices();
        void BuildAntiPortals();
        void FreeBuffers();

        // The vertex format this group's geometry needs: two colour sets and two texture
        // coordinate sets when it carries them, one of each otherwise.
        EGxVertexBufferFormat VertexFormat() const;

        // Make whichever of the three buffers this group still lacks. ref: FUN_007cbcb0
        void CreateBuffers();
        // ref: FUN_007c9cb0
        void BindVertexStream();
        // ref: FUN_007c9d80
        void BindIndexStream();
        // ref: FUN_007c8560
        void FillVertexBuffer(CGxBuf* buf, EGxVertexBufferFormat format);
        // ref: FUN_007c8b90
        void FillIndexBuffer(CGxBuf* buf);

        // ref: FUN_007a7630
        static bool BatchOutsideFrustum(const SMOBatch* batch);

        // The draw a group with no vertex colours takes: one lighting mode for every
        // batch. ref: FUN_007ac6a0
        void DrawBatches(int32_t record);
        // ref: FUN_007ac9f0
        void DrawBatchesSplit(int32_t record);
        // ref: FUN_007a9380
        void DrawBatchesOutdoor(int32_t record);

        // ref: FUN_007d78c0
        void BakePortalLight();
};

#endif
