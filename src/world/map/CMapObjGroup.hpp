#ifndef WORLD_MAP_C_MAP_OBJ_GROUP_HPP
#define WORLD_MAP_C_MAP_OBJ_GROUP_HPP

#include "world/map/CMapObj.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Plane.hpp>
#include <tempest/Ray.hpp>
#include <tempest/Segment.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CMapObjGroup;

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
    uint32_t unused3;
    uint16_t* indices;            // three per hit face, from the shared index pool
    uint16_t* faces;              // hit face numbers, from the shared face pool
    uint16_t indexCount;
    uint16_t faceCount;
    uint16_t minIndex;
    uint16_t maxIndex;
    void* object;                 // whatever the caller passed as the hit's owner
};

class CMapObjGroup {
    public:
        // Static variables: the pool of hit records the queries append to (reference globals
        // DAT_00cd8080.., DAT_00cb7528..). Callers reset the counters before a query and read the
        // records after it.
        static uint32_t s_hitFlags;              // DAT_00cb7528: bit 0 = a pool overflowed
        static uint32_t s_hitRecordCount;        // DAT_00cb752c
        static uint32_t s_hitFacePoolCount;      // DAT_00cb7530
        static uint32_t s_hitIndexPoolCount;     // DAT_00cb7534
        static uint32_t s_unk7538;               // DAT_00cb7538: reset alongside the others by every caller
        static CMapObjHitRecord s_hitRecords[0x20];
        static uint16_t s_hitFacePool[0x4000];   // DAT_00cb7540
        static uint16_t s_hitIndexPool[0xc000];  // DAT_00cbf540

        // Static functions
        static CMapObjHitRecord* AllocHitRecord(uint32_t indexCount, uint32_t faceCount);
        static void QueryEnd(SMOPoly* polys);

        // Member variables. The reference offsets, for the day the whole class is laid out.
        uint32_t m_memHandle = 0;                // +0: CMap::s_mapObjGroupHeap slot (CMap::AllocMapObjGroup)
        uint32_t m_flags = 0;                    // +0x30: MOGP flags
        CAaBspNode* m_bspNodes = nullptr;        // +0x68: MOBN
        uint32_t m_bspNodeCount = 0;
        uint16_t* m_bspFaceRefs = nullptr;       // +0x6c: MOBR
        uint32_t m_bspFaceRefCount = 0;
        CAaBox m_bounds;                         // +0xb0
        SMOPoly* m_polys = nullptr;              // +0xdc: MOPY
        const uint16_t* m_indices = nullptr;     // +0xe0: MOVI (not owned)
        const C3Vector* m_vertices = nullptr;    // +0xe8: MOVT (not owned)
        uint32_t m_vertexCount = 0;              // +0xec
        const CImVector* m_colors = nullptr;     // +0x108: MOCV (not owned)
        uint32_t m_faceCount = 0;                // +0x150
        CMapObj* m_mapObj = nullptr;             // +0x18c
        TSLink<CMapObjGroup> m_link;             // +0x1b4: unlinked by CMap::FreeMapObjGroup

        // Member functions
        ~CMapObjGroup();
        void FreeQueryData();

        bool QuerySegment(const C3Segment& segment, float* t, uint32_t queryFlags, uint16_t skipFlags, void* unused, const C44Matrix* placement, void* object);
        bool QuerySegmentFace(const C3Segment& segment, float* t, uint32_t queryFlags, uint16_t skipFlags, uint32_t* outFace);
        bool QuerySegmentDual(const C3Segment& segment, float* collisionT, int32_t* collisionFace, float* renderT, int32_t* renderFace);
        bool QueryBox(const C4Plane* hull, const C3Vector* corners, uint32_t queryFlags, uint16_t skipFlags, const C44Matrix* placement, void* object);
        bool SampleColorAtFace(const C3Vector& point, uint16_t face, CImVector* outColor, uint8_t* outFlag);

        void SegmentQueryNode(CMapObjGroupSegmentQuery& query, int32_t nodeIdx, const C3Segment& segment, const CAaBox& box);
        void SegmentQueryLeaf(CMapObjGroupSegmentQuery& query, const CAaBspNode* node);
        void DualQueryNode(CMapObjGroupDualSegmentQuery& query, int32_t nodeIdx, const C3Segment& segment, const CAaBox& box);
        void DualQueryLeaf(CMapObjGroupDualSegmentQuery& query, const CAaBspNode* node);
        void BoxQueryNode(CMapObjGroupBoxQuery& query, int32_t nodeIdx, const CAaBox& queryBox, const CAaBox& box);
        void BoxQueryLeaf(CMapObjGroupBoxQuery& query, const CAaBspNode* node);
        void RecordHits(const C44Matrix* placement, void* object, uint32_t flags);
};

#endif
