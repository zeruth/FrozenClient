#include "world/map/CMapObjGroup.hpp"
#include "gx/Buffer.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CChunkLiquid.hpp"
#include "db/Db.hpp"
#include <storm/Memory.hpp>
#include <tempest/Intersect.hpp>
#include <tempest/Rect.hpp>
#include <cmath>
#include <cstring>

// The WMO group's face queries: a segment, a segment wanting both the nearest collision face and
// the nearest rendered face, and a box, each walked through the group's MOBN tree. The reference
// keeps this state in module globals and the per-query context on the caller's stack; the layout
// here follows it so the ports read like the decompilation.

uint32_t CMapObjGroup::s_hitFlags = 0;
uint32_t CMapObjGroup::s_hitRecordCount = 0;
uint32_t CMapObjGroup::s_hitFacePoolCount = 0;
uint32_t CMapObjGroup::s_hitIndexPoolCount = 0;
uint32_t CMapObjGroup::s_hitPlacementCount = 0;
CMapObjHitRecord CMapObjGroup::s_hitRecords[0x20];
C44Matrix CMapObjGroup::s_hitPlacements[0x20];
uint16_t CMapObjGroup::s_hitFacePool[0x4000];
uint16_t CMapObjGroup::s_hitIndexPool[0xc000];

namespace {

// Every face the running query has marked F_COLLIDE_HIT (DAT_00d25bf8 / DAT_00d2dbf8), so the
// marks can be cleared when it ends, and the faces it actually hit (DAT_00d29bf8 / DAT_00d2dbfc).
uint16_t s_collideHitFaces[0x2000];
uint32_t s_collideHitCount = 0;
uint16_t s_hitFaces[0x2000];
uint32_t s_hitCount = 0;

// The band either side of a split plane inside which a segment endpoint counts as on the plane
constexpr float BSP_PLANE_EPSILON = 0.01f;

// How far outside a triangle a ray may land and still hit it
constexpr float RAY_TRIANGLE_EPSILON = 0.002f;

// Which two axes a face is projected onto for its barycentric colour sample, by the dominant axis
// of its normal (DAT_00d2dc00, filled on first use)
uint32_t s_colorAxisTable[3][2];
bool s_colorAxisTableInit = false;

// ref: FUN_007c71e0
bool TriangleOutsideHull(const C4Plane* hull, const C3Vector& a, const C3Vector& b, const C3Vector& c) {
    uint8_t outcode[3];

    ClassifyPointPlanes6(hull, a, &outcode[0]);
    ClassifyPointPlanes6(hull, b, &outcode[1]);
    ClassifyPointPlanes6(hull, c, &outcode[2]);

    return (outcode[0] & outcode[2] & outcode[1]) != 0;
}

} // namespace

// ref: FUN_007c7710
CMapObjHitRecord* CMapObjGroup::AllocHitRecord(uint32_t indexCount, uint32_t faceCount) {
    uint32_t i = CMapObjGroup::s_hitRecordCount;

    if (CMapObjGroup::s_hitRecordCount + 1 < 0x20
        && CMapObjGroup::s_hitIndexPoolCount + indexCount < 0xc000
        && CMapObjGroup::s_hitFacePoolCount + faceCount < 0x4000) {
        CMapObjGroup::s_hitRecordCount++;

        CMapObjHitRecord* record = &CMapObjGroup::s_hitRecords[i];
        record->placement = nullptr;
        record->vertices = nullptr;
        record->vertexCount = 0;
        record->heights = nullptr;
        record->indices = nullptr;
        record->faces = nullptr;
        record->indexCount = 0;
        record->faceCount = 0;
        record->minIndex = 0;
        record->maxIndex = 0;
        record->object = nullptr;
        record->minIndex = 0xFFFF;

        return record;
    }

    CMapObjGroup::s_hitFlags |= 0x1;

    return nullptr;
}

// A hit record with no pool space reserved: only the record count is checked.
// ref: FUN_007a6140
CMapObjHitRecord* CMapObjGroup::AllocHitRecord() {
    uint32_t i = CMapObjGroup::s_hitRecordCount;

    if (CMapObjGroup::s_hitRecordCount + 1 < 0x20) {
        CMapObjGroup::s_hitRecordCount++;

        CMapObjHitRecord* record = &CMapObjGroup::s_hitRecords[i];
        record->placement = nullptr;
        record->vertices = nullptr;
        record->vertexCount = 0;
        record->heights = nullptr;
        record->indices = nullptr;
        record->faces = nullptr;
        record->indexCount = 0;
        record->faceCount = 0;
        record->minIndex = 0;
        record->maxIndex = 0;
        record->object = nullptr;
        record->minIndex = 0xFFFF;

        return record;
    }

    CMapObjGroup::s_hitFlags |= 0x1;

    return nullptr;
}

// ref: FUN_007a6190
uint16_t* CMapObjGroup::AllocHitIndices(uint32_t count) {
    uint32_t start = CMapObjGroup::s_hitIndexPoolCount;

    if (count + CMapObjGroup::s_hitIndexPoolCount < 0xc000) {
        CMapObjGroup::s_hitIndexPoolCount = count + CMapObjGroup::s_hitIndexPoolCount;

        return &CMapObjGroup::s_hitIndexPool[start];
    }

    CMapObjGroup::s_hitFlags |= 0x1;

    return nullptr;
}

// The placement pool's bump allocator. The reference inlines this at its one call site, the terrain
// hit collector, and shares the overflow flag with the other two pools.
C44Matrix* CMapObjGroup::AllocHitPlacement() {
    uint32_t i = CMapObjGroup::s_hitPlacementCount;

    if (CMapObjGroup::s_hitPlacementCount + 1 < 0x20) {
        CMapObjGroup::s_hitPlacementCount++;

        return &CMapObjGroup::s_hitPlacements[i];
    }

    CMapObjGroup::s_hitFlags |= 0x1;

    return nullptr;
}

// Whether all three corners lie beyond the same face of the box, tested on the sign bits of the
// differences as the reference does.
// ref: FUN_007c7a00
bool TriangleOutsideBox(const CAaBox& box, const C3Vector& a, const C3Vector& b, const C3Vector& c) {
    const float* bmin = &box.b.x;
    const float* bmax = &box.t.x;
    const float* pa = &a.x;
    const float* pb = &b.x;
    const float* pc = &c.x;

    for (int32_t i = 0; i < 3; i++) {
        if ((std::signbit(bmax[i] - pa[i]) && std::signbit(bmax[i] - pb[i]) && std::signbit(bmax[i] - pc[i]))
            || (std::signbit(pa[i] - bmin[i]) && std::signbit(pb[i] - bmin[i]) && std::signbit(pc[i] - bmin[i]))) {
            return true;
        }
    }

    return false;
}

// ref: FUN_007c7610
void CMapObjGroup::QueryEnd(SMOPoly* polys) {
    while (s_collideHitCount != 0) {
        s_collideHitCount--;
        polys[s_collideHitFaces[s_collideHitCount]].flags &= 0x7F;
    }

    s_hitCount = 0;
    s_collideHitCount = 0;
}

CMapObjGroup::~CMapObjGroup() {
    this->FreeQueryData();
}

void CMapObjGroup::FreeQueryData() {
    if (this->m_polys) {
        SMemFree(this->m_polys, __FILE__, __LINE__, 0);
        this->m_polys = nullptr;
    }

    if (this->m_bspNodes) {
        SMemFree(this->m_bspNodes, __FILE__, __LINE__, 0);
        this->m_bspNodes = nullptr;
    }

    if (this->m_bspFaceRefs) {
        SMemFree(this->m_bspFaceRefs, __FILE__, __LINE__, 0);
        this->m_bspFaceRefs = nullptr;
    }

    this->m_bspNodeCount = 0;
    this->m_bspFaceRefCount = 0;
    this->m_faceCount = 0;
}

// ref: FUN_007c78e0
void CMapObjGroupSegmentQuery::Init(SMOPoly* polys, const C3Vector* vertices, const uint16_t* indices, const C3Segment& segment, float* t, uint16_t skipFlags, const SMOMaterial* materials, uint32_t materialCount) {
    this->polys = polys;
    this->vertices = vertices;
    this->indices = indices;
    this->overflow = nullptr;
    this->outT = t;
    this->segment.start = { 0.0f, 0.0f, 0.0f };
    this->segment.end = { 0.0f, 0.0f, 0.0f };
    this->ray.origin = { 0.0f, 0.0f, 0.0f };
    this->ray.dir = { 0.0f, 0.0f, 0.0f };
    this->materials = materials;
    this->materialCount = materialCount;
    this->skipFlags = skipFlags | SMOPoly::F_COLLIDE_HIT;
    // The reference copies the global DAT_00cf08f8 here; nothing ported reads it
    this->unk15 = 0;

    this->ray.origin = segment.start;
    this->ray.dir.x = segment.end.x - segment.start.x;
    this->ray.dir.y = segment.end.y - segment.start.y;
    this->ray.dir.z = segment.end.z - segment.start.z;

    float length = sqrtf(this->ray.dir.x * this->ray.dir.x + this->ray.dir.y * this->ray.dir.y + this->ray.dir.z * this->ray.dir.z);

    this->bestDist = *t * length;
    this->segment = segment;
    this->maxT = *t;

    float invLength = 1.0f / length;
    this->invLength = invLength;
    this->ray.dir.x = this->ray.dir.x * invLength;
    this->ray.dir.y = this->ray.dir.y * invLength;
    this->ray.dir.z = invLength * this->ray.dir.z;
}

// FUN_007c6d50 (see overrides.json: diverged). With the bspcache CVar on, which is its default,
// the reference resolves the leaf through CMap's node cache (FUN_0079b1f0): a world-space copy of
// the leaf's faces with per-vertex outcodes that prefilter the triangle test. The cache is not
// ported, so every leaf takes the uncached loop below; the hits are the same set.
bool CMapObjGroupSegmentQuery::CachedLeaf(CMapObjGroup* group, const CAaBspNode* node) {
    return false;
}

// ref: FUN_007c6c30
void CMapObjGroupSegmentQuery::TestFace(uint16_t face) {
    uint16_t mask = this->skipFlags;

    if (this->polys[face].flags & static_cast<uint8_t>(mask)) {
        return;
    }

    // Query flag 0x200 skips untextured faces, 0x100 textured ones.
    //
    // Diverges from the reference by checking the material table first. The reference indexes it
    // unconditionally, which is safe there but not here: frozen only fills the table when the WMO
    // carries a MOMT chunk big enough to parse, so a malformed or absent one leaves it null and
    // the read would fault. A face with no resolvable material counts as untextured, which is
    // what material 0xFF already means.
    uint8_t material = this->polys[face].material;
    uint16_t kind;

    bool textured = material != 0xFF
        && this->materials
        && material < this->materialCount
        && this->materials[material].texture1 != 0;

    if (!textured) {
        kind = mask & 0x200;
    } else {
        kind = mask & 0x100;
    }

    if (kind != 0) {
        return;
    }

    if (s_collideHitCount < 0x2000) {
        s_collideHitFaces[s_collideHitCount] = face;
        s_collideHitCount++;
        this->polys[face].flags |= SMOPoly::F_COLLIDE_HIT;

        float t = 0.0f;

        if (IntersectRayTriangle(this->ray, this->vertices, &this->indices[face * 3], &t, nullptr, RAY_TRIANGLE_EPSILON)
            && 0.0f <= t
            && t <= this->bestDist) {
            this->bestDist = t;
            s_hitFaces[0] = face;
            s_hitCount = 1;

            *this->outT = t * this->invLength;

            if (*this->outT > this->maxT) {
                *this->outT = this->maxT;
            }
        }
    } else if (this->overflow) {
        *this->overflow |= 0x1;
    }
}

// ref: FUN_007ca180
void CMapObjGroup::SegmentQueryNode(CMapObjGroupSegmentQuery& query, int32_t nodeIdx, const C3Segment& segment, const CAaBox& box) {
    const CAaBspNode* node = &this->m_bspNodes[nodeIdx];

    if (!node) {
        return;
    }

    if (node->flags & CAaBspNode::Flag_Leaf) {
        this->SegmentQueryLeaf(query, node);
        return;
    }

    uint32_t axis = node->flags & CAaBspNode::Flag_AxisMask;

    const float* start = &segment.start.x;
    const float* end = &segment.end.x;
    const float* boxMin = &box.b.x;
    const float* boxMax = &box.t.x;

    // The segment has to reach into this node's slab along the split axis
    if ((-BSP_PLANE_EPSILON <= start[axis] - boxMin[axis] || -BSP_PLANE_EPSILON <= end[axis] - boxMin[axis])
        && (BSP_PLANE_EPSILON <= boxMax[axis] - start[axis] || BSP_PLANE_EPSILON <= boxMax[axis] - end[axis])) {
        CAaBox posBox = box;
        (&posBox.b.x)[axis] = node->planeDist;

        CAaBox negBox = box;
        (&negBox.t.x)[axis] = node->planeDist;

        float d0 = start[axis] - node->planeDist;
        float d1 = end[axis] - node->planeDist;

        if ((-BSP_PLANE_EPSILON <= d0 && d0 <= BSP_PLANE_EPSILON) || (-BSP_PLANE_EPSILON <= d1 && d1 <= BSP_PLANE_EPSILON)) {
            // An endpoint sits on the plane: both sides, whole segment
            if (node->posChild != CAaBspNode::NoChild) {
                this->SegmentQueryNode(query, node->posChild, segment, posBox);
            }
        } else {
            if (BSP_PLANE_EPSILON < d0 && BSP_PLANE_EPSILON < d1) {
                if (node->posChild == CAaBspNode::NoChild) {
                    return;
                }

                this->SegmentQueryNode(query, node->posChild, segment, posBox);
                return;
            }

            if (-BSP_PLANE_EPSILON <= d0 || -BSP_PLANE_EPSILON <= d1) {
                // The segment crosses the plane: split it there and send each half down its side
                C3Vector mid;
                segment.Lerp(mid, d0 / (d0 - d1));

                if (d0 <= 0.0f) {
                    if (node->negChild != CAaBspNode::NoChild) {
                        C3Segment part(segment.start, mid);
                        this->SegmentQueryNode(query, node->negChild, part, negBox);
                    }

                    if (node->posChild == CAaBspNode::NoChild) {
                        return;
                    }

                    C3Segment part(mid, segment.end);
                    this->SegmentQueryNode(query, node->posChild, part, posBox);
                    return;
                }

                if (node->posChild != CAaBspNode::NoChild) {
                    C3Segment part(segment.start, mid);
                    this->SegmentQueryNode(query, node->posChild, part, posBox);
                }

                if (node->negChild == CAaBspNode::NoChild) {
                    return;
                }

                C3Segment part(mid, segment.end);
                this->SegmentQueryNode(query, node->negChild, part, negBox);
                return;
            }
        }

        if (node->negChild != CAaBspNode::NoChild) {
            this->SegmentQueryNode(query, node->negChild, segment, negBox);
        }
    }
}

// ref: FUN_007c9a00
void CMapObjGroup::SegmentQueryLeaf(CMapObjGroupSegmentQuery& query, const CAaBspNode* node) {
    if (query.CachedLeaf(this, node)) {
        return;
    }

    uint32_t faceStart = node->faceStart;
    const uint16_t* faceRefs = this->m_bspFaceRefs;

    // MOBN carries the range; clamp it to what MOBR actually holds. The reference trusts the
    // file, which is fine for archive data it shipped, but a short or damaged MOBR would walk off
    // the end here.
    uint32_t count = node->nFaces;

    if (faceStart >= this->m_bspFaceRefCount) {
        return;
    }

    if (faceStart + count > this->m_bspFaceRefCount) {
        count = this->m_bspFaceRefCount - faceStart;
    }

    for (uint32_t i = 0; i < count; i++) {
        query.TestFace(faceRefs[faceStart + i]);
    }
}

// ref: FUN_007c77d0
void CMapObjGroupDualSegmentQuery::Init(SMOPoly* polys, const C3Vector* vertices, const uint16_t* indices, const C3Segment& segment, float maxCollisionT, float maxRenderT) {
    this->polys = polys;
    this->overflow = nullptr;
    this->vertices = vertices;
    this->indices = indices;
    this->ray.origin = { 0.0f, 0.0f, 0.0f };
    this->ray.dir = { 0.0f, 0.0f, 0.0f };
    this->segment.start = { 0.0f, 0.0f, 0.0f };
    this->segment.end = { 0.0f, 0.0f, 0.0f };

    this->ray.origin = segment.start;
    this->ray.dir.x = segment.end.x - segment.start.x;
    this->ray.dir.y = segment.end.y - segment.start.y;
    this->ray.dir.z = segment.end.z - segment.start.z;
    this->segment = segment;

    float length = sqrtf(this->ray.dir.x * this->ray.dir.x + this->ray.dir.y * this->ray.dir.y + this->ray.dir.z * this->ray.dir.z);
    float invLength = 1.0f / length;

    this->invLength = invLength;
    this->ray.dir.x = invLength * this->ray.dir.x;
    this->ray.dir.y = this->ray.dir.y * invLength;
    this->ray.dir.z = invLength * this->ray.dir.z;
    this->maxCollisionT = maxCollisionT;
    this->maxRenderT = maxRenderT;
    this->collisionFace = -1;
    this->renderFace = -1;
    this->skipFlags = SMOPoly::F_COLLIDE_HIT | SMOPoly::F_NOCAMCOLLIDE;
    this->bestCollisionDist = length * maxCollisionT;
    this->bestRenderDist = maxRenderT * length;
}

// FUN_007c6790 (see overrides.json: diverged, as CMapObjGroupSegmentQuery::CachedLeaf)
bool CMapObjGroupDualSegmentQuery::CachedLeaf(CMapObjGroup* group, const CAaBspNode* node) {
    return false;
}

// ref: FUN_007c6600
void CMapObjGroupDualSegmentQuery::TestFace(uint16_t face) {
    uint8_t flags = this->polys[face].flags;

    if ((this->skipFlags & flags) != 0 || s_collideHitCount >= 0x2000) {
        return;
    }

    s_collideHitFaces[s_collideHitCount] = face;
    s_collideHitCount++;
    this->polys[face].flags |= SMOPoly::F_COLLIDE_HIT;

    float t = 0.0f;

    if (!IntersectRayTriangle(this->ray, this->vertices, &this->indices[face * 3], &t, nullptr, RAY_TRIANGLE_EPSILON)) {
        return;
    }

    if ((flags & SMOPoly::F_RENDER) == 0) {
        if (flags & SMOPoly::F_COLLISION) {
            // A pure collision face only competes for the collision slot
            if (t < 0.0f) {
                return;
            }

            if (t > this->bestCollisionDist) {
                return;
            }

            this->collisionFace = face;
            this->bestCollisionDist = t;
            return;
        }

        if ((flags & SMOPoly::F_DETAIL) == 0) {
            return;
        }

        // A detail face only competes for the render slot
        if (t < 0.0f) {
            return;
        }
    } else {
        // A rendered face competes for both
        if (t < 0.0f) {
            return;
        }

        if (t <= this->bestCollisionDist) {
            this->bestCollisionDist = t;
            this->collisionFace = face;
        }
    }

    if (t <= this->bestRenderDist) {
        this->renderFace = face;
        this->bestRenderDist = t;
    }
}

// ref: FUN_007c6710
bool CMapObjGroupDualSegmentQuery::Results(float* collisionT, int32_t* collisionFace, float* renderT, int32_t* renderFace) {
    *collisionFace = -1;
    *renderFace = -1;

    if (this->collisionFace == -1) {
        if (this->renderFace == -1) {
            return false;
        }
    } else if (this->renderFace == -1) {
        goto collision;
    }

    {
        float t = this->bestRenderDist * this->invLength;
        *renderT = t;

        if (this->maxRenderT <= t) {
            t = this->maxRenderT;
        }

        *renderT = t;
        *renderFace = this->renderFace;
    }

collision:
    if (this->collisionFace != -1) {
        float t = this->bestCollisionDist * this->invLength;
        *collisionT = t;

        if (this->maxCollisionT <= t) {
            t = this->maxCollisionT;
        }

        *collisionT = t;
        *collisionFace = this->collisionFace;
    }

    return true;
}

// ref: FUN_007ca600
void CMapObjGroup::DualQueryNode(CMapObjGroupDualSegmentQuery& query, int32_t nodeIdx, const C3Segment& segment, const CAaBox& box) {
    const CAaBspNode* node = &this->m_bspNodes[nodeIdx];

    if (!node) {
        return;
    }

    if (node->flags & CAaBspNode::Flag_Leaf) {
        this->DualQueryLeaf(query, node);
        return;
    }

    uint32_t axis = node->flags & CAaBspNode::Flag_AxisMask;

    const float* start = &segment.start.x;
    const float* end = &segment.end.x;
    const float* boxMin = &box.b.x;
    const float* boxMax = &box.t.x;

    if ((-BSP_PLANE_EPSILON <= start[axis] - boxMin[axis] || -BSP_PLANE_EPSILON <= end[axis] - boxMin[axis])
        && (BSP_PLANE_EPSILON <= boxMax[axis] - start[axis] || BSP_PLANE_EPSILON <= boxMax[axis] - end[axis])) {
        CAaBox posBox = box;
        (&posBox.b.x)[axis] = node->planeDist;

        CAaBox negBox = box;
        (&negBox.t.x)[axis] = node->planeDist;

        float d0 = start[axis] - node->planeDist;
        float d1 = end[axis] - node->planeDist;

        if ((-BSP_PLANE_EPSILON <= d0 && d0 <= BSP_PLANE_EPSILON) || (-BSP_PLANE_EPSILON <= d1 && d1 <= BSP_PLANE_EPSILON)) {
            if (node->posChild != CAaBspNode::NoChild) {
                this->DualQueryNode(query, node->posChild, segment, posBox);
            }
        } else {
            if (BSP_PLANE_EPSILON < d0 && BSP_PLANE_EPSILON < d1) {
                if (node->posChild == CAaBspNode::NoChild) {
                    return;
                }

                this->DualQueryNode(query, node->posChild, segment, posBox);
                return;
            }

            if (-BSP_PLANE_EPSILON <= d0 || -BSP_PLANE_EPSILON <= d1) {
                C3Vector mid;
                segment.Lerp(mid, d0 / (d0 - d1));

                if (d0 <= 0.0f) {
                    if (node->negChild != CAaBspNode::NoChild) {
                        C3Segment part(segment.start, mid);
                        this->DualQueryNode(query, node->negChild, part, negBox);
                    }

                    if (node->posChild == CAaBspNode::NoChild) {
                        return;
                    }

                    C3Segment part(mid, segment.end);
                    this->DualQueryNode(query, node->posChild, part, posBox);
                    return;
                }

                if (node->posChild != CAaBspNode::NoChild) {
                    C3Segment part(segment.start, mid);
                    this->DualQueryNode(query, node->posChild, part, posBox);
                }

                if (node->negChild == CAaBspNode::NoChild) {
                    return;
                }

                C3Segment part(mid, segment.end);
                this->DualQueryNode(query, node->negChild, part, negBox);
                return;
            }
        }

        if (node->negChild != CAaBspNode::NoChild) {
            this->DualQueryNode(query, node->negChild, segment, negBox);
        }
    }
}

// ref: FUN_007c9ab0
void CMapObjGroup::DualQueryLeaf(CMapObjGroupDualSegmentQuery& query, const CAaBspNode* node) {
    if (query.CachedLeaf(this, node)) {
        return;
    }

    uint32_t faceStart = node->faceStart;
    const uint16_t* faceRefs = this->m_bspFaceRefs;

    // MOBN carries the range; clamp it to what MOBR actually holds. The reference trusts the
    // file, which is fine for archive data it shipped, but a short or damaged MOBR would walk off
    // the end here.
    uint32_t count = node->nFaces;

    if (faceStart >= this->m_bspFaceRefCount) {
        return;
    }

    if (faceStart + count > this->m_bspFaceRefCount) {
        count = this->m_bspFaceRefCount - faceStart;
    }

    for (uint32_t i = 0; i < count; i++) {
        query.TestFace(faceRefs[faceStart + i]);
    }
}

// ref: FUN_007c7660
void CMapObjGroupBoxQuery::TestFace(uint16_t face) {
    if (static_cast<uint8_t>(this->skipFlags) & this->polys[face].flags) {
        return;
    }

    if (s_collideHitCount < 0x2000) {
        s_collideHitFaces[s_collideHitCount] = face;
        s_collideHitCount++;
        this->polys[face].flags |= SMOPoly::F_COLLIDE_HIT;

        const uint16_t* tri = &this->indices[face * 3];
        const C3Vector* vertices = this->vertices;

        if (!TriangleOutsideHull(this->hull, vertices[tri[0]], vertices[tri[1]], vertices[tri[2]])) {
            s_hitFaces[s_hitCount] = face;
            s_hitCount++;
        }
    } else if (this->overflow) {
        *this->overflow |= 0x1;
    }
}

// ref: FUN_007ca440
void CMapObjGroup::BoxQueryNode(CMapObjGroupBoxQuery& query, int32_t nodeIdx, const CAaBox& queryBox, const CAaBox& box) {
    const CAaBspNode* node = &this->m_bspNodes[nodeIdx];

    if (node->flags & CAaBspNode::Flag_Leaf) {
        this->BoxQueryLeaf(query, node);
        return;
    }

    uint32_t axis = node->flags & CAaBspNode::Flag_AxisMask;

    const float* queryMin = &queryBox.b.x;
    const float* queryMax = &queryBox.t.x;
    const float* boxMin = &box.b.x;
    const float* boxMax = &box.t.x;

    if (boxMin[axis] <= queryMax[axis] && queryMin[axis] <= boxMax[axis]) {
        CAaBox posBox = box;
        (&posBox.b.x)[axis] = node->planeDist;

        CAaBox negBox = box;
        (&negBox.t.x)[axis] = node->planeDist;

        if (queryMin[axis] <= node->planeDist) {
            if (node->planeDist <= queryMax[axis]) {
                // The query box straddles the plane: both sides, each with the box clipped to it
                if (node->posChild != CAaBspNode::NoChild) {
                    CAaBox clipped = queryBox;
                    (&clipped.b.x)[axis] = node->planeDist;
                    this->BoxQueryNode(query, node->posChild, clipped, posBox);
                }

                if (node->negChild != CAaBspNode::NoChild) {
                    CAaBox clipped = queryBox;
                    (&clipped.t.x)[axis] = node->planeDist;
                    this->BoxQueryNode(query, node->negChild, clipped, negBox);
                }
            } else if (node->negChild != CAaBspNode::NoChild) {
                this->BoxQueryNode(query, node->negChild, queryBox, negBox);
                return;
            }
        } else if (node->posChild != CAaBspNode::NoChild) {
            this->BoxQueryNode(query, node->posChild, queryBox, posBox);
            return;
        }
    }
}

// ref: FUN_007c9a60
void CMapObjGroup::BoxQueryLeaf(CMapObjGroupBoxQuery& query, const CAaBspNode* node) {
    uint32_t faceStart = node->faceStart;
    const uint16_t* faceRefs = this->m_bspFaceRefs;

    // MOBN carries the range; clamp it to what MOBR actually holds. The reference trusts the
    // file, which is fine for archive data it shipped, but a short or damaged MOBR would walk off
    // the end here.
    uint32_t count = node->nFaces;

    if (faceStart >= this->m_bspFaceRefCount) {
        return;
    }

    if (faceStart + count > this->m_bspFaceRefCount) {
        count = this->m_bspFaceRefCount - faceStart;
    }

    for (uint32_t i = 0; i < count; i++) {
        query.TestFace(faceRefs[faceStart + i]);
    }
}

// ref: FUN_007c7ae0
void CMapObjGroup::RecordHits(const C44Matrix* placement, void* object, uint32_t flags) {
    // With CWorld enable 0x200000 set, the reference first draws every visited face (0x7fff0000)
    // and every hit face (0x7f00ff00) as debug triangles through FUN_007a4c10. That debug drawer
    // is not ported.

    CMapObjGroup::s_hitFlags |= flags;

    if (s_hitCount != 0 && s_hitCount < 0x2001) {
        CMapObjHitRecord* record = CMapObjGroup::AllocHitRecord(s_hitCount * 3, s_hitCount);

        if (record) {
            record->object = object;

            uint16_t* indices;
            uint32_t indexEnd = s_hitCount * 3 + CMapObjGroup::s_hitIndexPoolCount;

            if (indexEnd < 0xc000) {
                indices = &CMapObjGroup::s_hitIndexPool[CMapObjGroup::s_hitIndexPoolCount];
                CMapObjGroup::s_hitIndexPoolCount = indexEnd;
            } else {
                CMapObjGroup::s_hitFlags |= 0x1;
                indices = nullptr;
            }

            uint16_t* faces;

            if (CMapObjGroup::s_hitFacePoolCount + s_hitCount < 0x4000) {
                faces = &CMapObjGroup::s_hitFacePool[CMapObjGroup::s_hitFacePoolCount];
                CMapObjGroup::s_hitFacePoolCount += s_hitCount;
            } else {
                CMapObjGroup::s_hitFlags |= 0x1;
                faces = nullptr;
            }

            record->placement = placement;
            record->vertices = this->m_vertices;
            record->vertexCount = this->m_vertexCount;
            record->faceCount = static_cast<uint16_t>(s_hitCount);
            record->indexCount = static_cast<uint16_t>(s_hitCount * 3);

            for (uint32_t i = 0; i < s_hitCount; i++) {
                uint16_t face = s_hitFaces[i];
                faces[i] = face;

                for (uint32_t k = 0; k < 3; k++) {
                    uint16_t v = this->m_indices[face * 3 + k];
                    indices[i * 3 + k] = v;

                    uint16_t mn = record->minIndex;
                    if (v < mn) {
                        mn = v;
                    }
                    record->minIndex = mn;

                    if (v <= record->maxIndex) {
                        v = record->maxIndex;
                    }
                    record->maxIndex = v;
                }
            }

            record->indices = indices;
            record->faces = faces;
        }
    }
}

// ref: FUN_007cb0c0
bool CMapObjGroup::QuerySegment(const C3Segment& segment, float* t, uint32_t queryFlags, uint16_t skipFlags, void* unused, const C44Matrix* placement, void* object) {
    // A group with no BSP has nothing to walk. The reference always has one; frozen only builds
    // it when MOPY, MOBN and MOBR all parsed, so entering with a null tree is possible here and
    // would fault on the root node.
    if (!this->m_bspNodes || !this->m_bspNodeCount) {
        return false;
    }

    uint32_t recordsBefore = CMapObjGroup::s_hitRecordCount;

    CMapObjGroupSegmentQuery query;
    query.Init(this->m_polys, this->m_vertices, this->m_indices, segment, t, skipFlags, this->m_mapObj->m_materials, this->m_mapObj->m_materialCount);

    this->SegmentQueryNode(query, 0, segment, this->m_bounds);
    this->RecordHits(placement, object, 0);

    if ((queryFlags & 0x30000) && (this->m_flags & 0x1000)) {
        // TODO FUN_007c9dd0: the group's LIQUID, not its doodads -- this said doodads, and the
        // gate above disproves it: MOGP 0x1000 is has-water (has-doodads is 0x800), and
        // FUN_007c9dd0 reads m_liquidXTiles/YTiles, m_liquidPos and m_liquidMinZ/MaxZ. It is the
        // segment form of the point query ported as CMapObjGroup::GetLiquidAt (FUN_007c8360),
        // so a segment query cannot yet hit an indoor water surface.
    }

    bool hit = CMapObjGroup::s_hitRecordCount != recordsBefore;

    CMapObjGroup::QueryEnd(this->m_polys);

    return hit;
}

// ref: FUN_007cb2f0
bool CMapObjGroup::QuerySegmentFace(const C3Segment& segment, float* t, uint32_t queryFlags, uint16_t skipFlags, uint32_t* outFace) {
    // A group with no BSP has nothing to walk. The reference always has one; frozen only builds
    // it when MOPY, MOBN and MOBR all parsed, so entering with a null tree is possible here and
    // would fault on the root node.
    if (!this->m_bspNodes || !this->m_bspNodeCount) {
        return false;
    }

    CMapObjGroupSegmentQuery query;
    query.Init(this->m_polys, this->m_vertices, this->m_indices, segment, t, skipFlags, this->m_mapObj->m_materials, this->m_mapObj->m_materialCount);

    this->SegmentQueryNode(query, 0, segment, this->m_bounds);

    bool hit = s_hitCount != 0;

    if (hit) {
        *outFace = s_hitFaces[0];
    }

    if ((queryFlags & 0x30000) && (this->m_flags & 0x1000)) {
        // TODO FUN_007c9dd0: the group's LIQUID (see the note above); a hit reports face 0
    }

    CMapObjGroup::QueryEnd(this->m_polys);

    return hit;
}

// ref: FUN_007cb260
bool CMapObjGroup::QuerySegmentDual(const C3Segment& segment, float* collisionT, int32_t* collisionFace, float* renderT, int32_t* renderFace) {
    // A group with no BSP has nothing to walk. The reference always has one; frozen only builds
    // it when MOPY, MOBN and MOBR all parsed, so entering with a null tree is possible here and
    // would fault on the root node.
    if (!this->m_bspNodes || !this->m_bspNodeCount) {
        return false;
    }

    CMapObjGroupDualSegmentQuery query;
    query.Init(this->m_polys, this->m_vertices, this->m_indices, segment, *collisionT, *renderT);

    this->DualQueryNode(query, 0, segment, this->m_bounds);

    bool hit = query.Results(collisionT, collisionFace, renderT, renderFace);

    CMapObjGroup::QueryEnd(this->m_polys);

    return hit;
}

// ref: FUN_007cb180
bool CMapObjGroup::QueryBox(const C4Plane* hull, const C3Vector* corners, uint32_t queryFlags, uint16_t skipFlags, const C44Matrix* placement, void* object) {
    // A group with no BSP has nothing to walk. The reference always has one; frozen only builds
    // it when MOPY, MOBN and MOBR all parsed, so entering with a null tree is possible here and
    // would fault on the root node.
    if (!this->m_bspNodes || !this->m_bspNodeCount) {
        return false;
    }

    uint32_t recordsBefore = CMapObjGroup::s_hitRecordCount;

    if (queryFlags & 0xF0) {
        uint32_t overflow = 0;

        CMapObjGroupBoxQuery query;
        query.vertices = this->m_vertices;
        query.polys = this->m_polys;
        query.overflow = &overflow;
        query.indices = this->m_indices;
        query.skipFlags = skipFlags | SMOPoly::F_COLLIDE_HIT;
        query.hull = hull;

        CAaBox queryBox;
        BoundsFromPoints(queryBox, corners, 8);

        this->BoxQueryNode(query, 0, queryBox, this->m_bounds);
        this->RecordHits(placement, object, overflow);
        CMapObjGroup::QueryEnd(this->m_polys);
    }

    if (queryFlags & 0x30000) {
        // TODO FUN_007cab70: the group's doodads against the box
    }

    return CMapObjGroup::s_hitRecordCount != recordsBefore;
}

// ref: FUN_007c7fe0
bool CMapObjGroup::SampleColorAtFace(const C3Vector& point, uint16_t face, CImVector* outColor, uint8_t* outFlag) {
    if (face >= this->m_faceCount) {
        *outColor = this->m_mapObj->m_ambientColor;
        *outFlag = 0;
        return true;
    }

    if (!s_colorAxisTableInit) {
        s_colorAxisTableInit = true;
        s_colorAxisTable[0][0] = 1;
        s_colorAxisTable[0][1] = 2;
        s_colorAxisTable[1][0] = 2;
        s_colorAxisTable[1][1] = 0;
        s_colorAxisTable[2][0] = 0;
        s_colorAxisTable[2][1] = 1;
    }

    const uint16_t* tri = &this->m_indices[face * 3];
    const C3Vector* v0 = &this->m_vertices[tri[0]];
    const C3Vector* v1 = &this->m_vertices[tri[1]];
    const C3Vector* v2 = &this->m_vertices[tri[2]];

    // Project onto the two axes across the face's normal
    C3Vector e1 = { v1->x - v0->x, v1->y - v0->y, v1->z - v0->z };
    C3Vector e2 = { v2->x - v0->x, v2->y - v0->y, v2->z - v0->z };
    C3Vector normal = {
        e1.y * e2.z - e1.z * e2.y,
        e1.z * e2.x - e1.x * e2.z,
        e1.x * e2.y - e1.y * e2.x,
    };

    uint32_t dominant = DominantAxis(normal);
    uint32_t a = s_colorAxisTable[dominant][0];
    uint32_t b = s_colorAxisTable[dominant][1];

    float a0 = (&v0->x)[a];
    float b0 = (&v0->x)[b];
    float e1a = (&v1->x)[a] - a0;
    float e1b = (&v1->x)[b] - b0;
    float e2a = (&v2->x)[a] - a0;
    float e2b = (&v2->x)[b] - b0;
    float pa = (&point.x)[a] - a0;
    float pb = (&point.x)[b] - b0;

    float inv = 1.0f / (e2b * e1a - e1b * e2a);

    // Fixed-point barycentric weights out of 256, then any negative weight is folded back into
    // the other two so a point just off the face still samples a sensible colour
    int32_t w1 = static_cast<int32_t>(nearbyintf((e2b * pa - pb * e2a) * inv * 256.0f - 0.5f));
    int32_t w2 = static_cast<int32_t>(nearbyintf((pb * e1a - e1b * pa) * inv * 256.0f - 0.5f));
    int32_t w0 = (256 - w2) - w1;

    if (w1 < 0) {
        int32_t d = (w2 * w1) / (w2 + w0);
        w2 = w2 + d;
        w0 = w0 + (w1 - d);
        w1 = 0;
    }

    if (w2 < 0) {
        int32_t d = (w2 * w1) / (w0 + w1);
        w1 = w1 + d;
        w0 = w0 + (w2 - d);
        w2 = 0;
    }

    if (w0 < 0) {
        int32_t d = (w0 * w1) / (w2 + w1);
        w2 = w2 + (w0 - d);
        w1 = w1 + d;
        w0 = 0;
    }

    const uint8_t* c0 = reinterpret_cast<const uint8_t*>(&this->m_colors[tri[0]]);
    const uint8_t* c1 = reinterpret_cast<const uint8_t*>(&this->m_colors[tri[1]]);
    const uint8_t* c2 = reinterpret_cast<const uint8_t*>(&this->m_colors[tri[2]]);
    uint8_t* out = reinterpret_cast<uint8_t*>(outColor);

    out[3] = static_cast<uint8_t>((c1[3] * w1 + c0[3] * w0 + c2[3] * w2) >> 8);
    out[2] = static_cast<uint8_t>((c1[2] * w1 + c0[2] * w0 + c2[2] * w2) >> 8);
    out[1] = static_cast<uint8_t>((c1[1] * w1 + c0[1] * w0 + c2[1] * w2) >> 8);
    out[0] = static_cast<uint8_t>((c1[0] * w1 + c0[0] * w0 + c2[0] * w2) >> 8);

    // MOCV is stored at half intensity; a unified-render-path WMO (MOHD flag 0x2) keeps its
    // ambient out of the vertex colours, so it is added back here
    const CMapObj* mapObj = this->m_mapObj;
    const uint8_t* ambient = reinterpret_cast<const uint8_t*>(&mapObj->m_ambientColor);

    uint32_t r2 = out[2] * 2;
    uint32_t r1 = out[1] * 2;
    uint32_t r0 = out[0] * 2;

    if (mapObj->m_mohd->flags & 0x2) {
        r2 += ambient[2];
        r1 += ambient[1];
        r0 += ambient[0];
    }

    if (r2 > 0xFE) {
        r2 = 0xFF;
    }
    out[2] = static_cast<uint8_t>(r2);

    if (r1 > 0xFE) {
        r1 = 0xFF;
    }
    out[1] = static_cast<uint8_t>(r1);

    if (r0 > 0xFE) {
        r0 = 0xFF;
    }
    out[0] = static_cast<uint8_t>(r0);

    *outFlag = this->m_polys[face].flags & 0x1;

    return true;
}

// ================================================================================================
// The world box query: what under an axis-aligned box can receive a projected decal.
//
// The reference lays these next to the hit-record allocators above (007a6140 .. 007a6af0), which is
// why they live here rather than with CMap, and passes the box and the mask between them in
// callee-saved registers; they are arguments here.
// ================================================================================================

namespace {

// How many of a chunk's eight-by-eight cells fit in a yard, and the half a cell the rounding wants
// (DAT_00a3fdb4 and DAT_00adfe3c: this module's own copies of the pair CMap's point queries use).
constexpr float CELLS_PER_YARD = 0.239999995f;
constexpr float CELL_ROUND_BIAS = 0.5f;
constexpr float MAP_HALF_EXTENT = 17066.666f;

// The slack the corner classification allows, DAT_00a3fdb8: a vertex this far outside a face of the
// box still counts as inside it, so a triangle exactly level with the box's floor is kept.
constexpr float CORNER_EPSILON = 0.0194444433f;

// A cell of a chunk is a four-triangle fan about an inner vertex. These are the five vertices as
// offsets from the cell's first outer vertex, and the four triangles as triples of the same offsets
// (DAT_00a40618 and the table immediately before it at 0x00a405e8). 0 and 1 are the near outer pair,
// 17 and 18 the far one -- the row stride is 17 because MCVT interleaves an eight-vertex inner row
// after every nine-vertex outer row -- and 9 is the inner vertex.
constexpr int32_t CELL_VERTEX_OFFSETS[5] = { 0, 9, 17, 1, 18 };
constexpr int32_t CELL_TRIANGLES[4][3] = { { 17, 9, 0 }, { 9, 1, 0 }, { 9, 17, 18 }, { 9, 18, 1 } };

// One bit per face of the box the vertex lies outside of: x, y, z below the minimum in bits 0..2,
// then above the maximum in bits 3..5. Three corners sharing a bit put their whole triangle outside.
// The reference reads the sign bit of each difference straight out of the float.
// ref: FUN_007a61d0
uint8_t ClassifyCorner(const CAaBox& box, const float* v) {
    uint8_t code = 0;

    code |= std::signbit(v[0] - box.b.x + CORNER_EPSILON) ? 0x01 : 0;
    code |= std::signbit(v[1] - box.b.y + CORNER_EPSILON) ? 0x02 : 0;
    code |= std::signbit(v[2] - box.b.z + CORNER_EPSILON) ? 0x04 : 0;
    code |= std::signbit(box.t.x - v[0] + CORNER_EPSILON) ? 0x08 : 0;
    code |= std::signbit(box.t.y - v[1] + CORNER_EPSILON) ? 0x10 : 0;
    code |= std::signbit(box.t.z - v[2] + CORNER_EPSILON) ? 0x20 : 0;

    return code;
}

// The terrain hit collector: every triangle of the chunk's cells in `cellRect` that the box reaches,
// appended to ONE hit record shared by the whole chunk. This is what puts a blob shadow on the
// ground.
//
// A terrain receiver differs from a WMO one in the two ways the record has to carry:
//
// * Its vertices are the single shared CMapChunk::s_vertexTable, which holds chunk-local XY for all
//   145 vertices and is the same table for every chunk on the map, so the z has to come from
//   somewhere else. That is what the record's `heights` field is for, and both decal stream builders
//   switch to their one-vertex-per-index path when it is set.
// * It has no placement matrix of its own, so one is taken from the pool beside the record pool and
//   filled with a translation to the chunk's origin.
//
// The shared table's z IS patched in place from the chunk's heights before a corner is classified.
// It is scratch: nothing else on the map reads z from that table, and a receiver's own z reaches the
// vertex stream through `heights` rather than through the table.
//
// ref: FUN_007a6260
bool TerrainCollectHits(CMapChunk* chunk, const CiRect& cellRect, const CAaBox& box, void* object) {
    // The reference reserves twelve indices per cell of the whole rect, four triangles' worth, up
    // front rather than per triangle.
    uint32_t cellCount = static_cast<uint32_t>((cellRect.maxY - cellRect.minY) + 1)
                       * static_cast<uint32_t>((cellRect.maxX - cellRect.minX) + 1);

    bool collected = false;
    CMapObjHitRecord* record = nullptr;
    uint16_t* indices = nullptr;

    for (int32_t row = cellRect.minY; row <= cellRect.maxY; row++) {
        for (int32_t col = cellRect.minX; col <= cellRect.maxX; col++) {
            if (CMapChunk::s_holeMask[(col >> 1) + (row >> 1) * 4] & chunk->m_header->holes) {
                continue;
            }

            int32_t base = row * 0x11 + col;

            // Indexed by the vertex offset, so 19 entries used of the reference's 20-byte slot.
            uint8_t corner[20] = { 0 };

            for (int32_t i = 0; i < 5; i++) {
                int32_t offset = CELL_VERTEX_OFFSETS[i];
                CMapChunk::s_vertexTable[base + offset][2] = chunk->m_heights[base + offset];
                corner[offset] = ClassifyCorner(box, CMapChunk::s_vertexTable[base + offset]);
            }

            for (int32_t t = 0; t < 4; t++) {
                const int32_t* tri = CELL_TRIANGLES[t];

                if ((corner[tri[0]] & corner[tri[1]] & corner[tri[2]]) != 0) {
                    continue;
                }

                // One record for the chunk, allocated on its first hit. The flag is set whether or
                // not the pools had room, so a chunk that overflows them is not retried.
                if (!collected) {
                    collected = true;
                    record = CMapObjGroup::AllocHitRecord();

                    if (record) {
                        record->object = chunk;

                        // The reference fills the placement without checking the pointer, which
                        // would fault if the placement pool overflowed. It cannot: the two pools
                        // hold 32 each, a record takes at most one placement, and both counters
                        // reset together.
                        C44Matrix* placement = CMapObjGroup::AllocHitPlacement();

                        if (placement) {
                            placement->Identity();
                            placement->d0 = chunk->m_position.x;
                            placement->d1 = chunk->m_position.y;
                            placement->d2 = chunk->m_position.z;
                        }

                        record->placement = placement;
                        record->vertices = reinterpret_cast<const C3Vector*>(CMapChunk::s_vertexTable);
                        record->vertexCount = 0;
                        record->heights = chunk->m_heights;

                        indices = CMapObjGroup::AllocHitIndices(cellCount * 12);
                        record->indices = indices;
                    }
                }

                if (!record || !record->indices || !record->vertices) {
                    continue;
                }

                for (int32_t k = 0; k < 3; k++) {
                    uint16_t index = static_cast<uint16_t>(tri[k] + base);

                    *indices++ = index;

                    if (index < record->minIndex) {
                        record->minIndex = index;
                    }

                    if (index > record->maxIndex) {
                        record->maxIndex = index;
                    }
                }

                record->indexCount += 3;
                record->faceCount += 1;
            }
        }
    }

    return collected;
}

// One chunk of the terrain walk: find it, bring the box into its space, clip the cell rect to its
// own eight-by-eight, and collect.
// ref: FUN_007a6630
bool MapQueryBoxChunk(int32_t chunkCol, int32_t chunkRow, const CiRect& cellRect, const CAaBox& box,
                      void* object, uint32_t queryMask) {
    CMapArea* area = CMap::s_areaGrid[((chunkRow >> 4) & 0x3f) * 64 + ((chunkCol >> 4) & 0x3f)];

    if (!area || area->m_asyncObject) {
        return false;
    }

    CMapChunk* chunk = area->m_chunks[(chunkRow & 0xf) * 16 + (chunkCol & 0xf)];

    if (!chunk) {
        return false;
    }

    CAaBox local;
    local.b.x = box.b.x - chunk->m_position.x;
    local.b.y = box.b.y - chunk->m_position.y;
    local.b.z = box.b.z - chunk->m_position.z;
    local.t.x = box.t.x - chunk->m_position.x;
    local.t.y = box.t.y - chunk->m_position.y;
    local.t.z = box.t.z - chunk->m_position.z;

    // The rect the caller built spans the whole box in map-wide cells; rebase it onto this chunk's
    // first cell and clip it to the eight it has. The reference keeps the 0,0,7,7 rect in a lazily
    // initialised static (DAT_00cfbe94), which is a constant here.
    static const CiRect CHUNK_CELLS = { 0, 0, 7, 7 };

    CiRect cells;
    cells.minY = cellRect.minY - chunkRow * 8;
    cells.minX = cellRect.minX - chunkCol * 8;
    cells.maxY = cellRect.maxY - chunkRow * 8;
    cells.maxX = cellRect.maxX - chunkCol * 8;
    cells = CiRect::Intersection(cells, CHUNK_CELLS);

    bool hit = false;

    if (queryMask & 0x100) {
        hit = TerrainCollectHits(chunk, cells, local, object);
    }

    // The chunk's LIQUID LAYERS, so a decal lands on water too. The list the reference walks is at
    // +0x100 / +0x108, which is m_liquidList, and the per-entry lookup proves it: it reads the
    // entry's +0x04 as a LiquidType.dbc id and tests bit 2 of that row's flags.
    //
    // The flag filter applies only when bit 0x10000 is set WITHOUT 0x20000. The blob's mask is
    // 0x220122, whose 0x30000 half is 0x20000, so the filter is off and every layer is offered.
    if (queryMask & 0x30000) {
        bool filterByFlag = (queryMask & 0x10000) != 0 && (queryMask & 0x20000) == 0;

        for (auto liquid = chunk->m_liquidList.Head(); liquid; liquid = chunk->m_liquidList.Next(liquid)) {
            if (filterByFlag) {
                // The reference reads the row's flags without checking that the id was in range, so
                // an id outside LiquidType.dbc would fault; frozen skips the layer instead.
                auto row = g_liquidTypeDB.GetRecord(static_cast<int32_t>(liquid->m_liquidType));

                if (!row || !(row->m_flags & 0x4)) {
                    continue;
                }
            }

            hit |= liquid->QueryBox(object, local, cells);
        }
    }

    return hit;
}

} // namespace

// ref: FUN_007a6830
bool MapQueryBoxTerrain(const CAaBox& box, void* object, uint32_t queryMask) {
    // The map's cell addressing is transposed and inverted: the row comes from x and the column
    // from y, and both count down from the map's positive corner, so the box's MAXIMUM gives the
    // low end of each range. CiRect's Y slot carries the row and its X slot the column, which is
    // the same pairing CWorldScene::s_frustumChunkRect uses.
    CiRect cellRect;
    cellRect.minY = static_cast<int32_t>(roundf(-(box.t.x - MAP_HALF_EXTENT) * CELLS_PER_YARD - CELL_ROUND_BIAS));
    cellRect.minX = static_cast<int32_t>(roundf(-(box.t.y - MAP_HALF_EXTENT) * CELLS_PER_YARD - CELL_ROUND_BIAS));
    cellRect.maxY = static_cast<int32_t>(roundf(-(box.b.x - MAP_HALF_EXTENT) * CELLS_PER_YARD - CELL_ROUND_BIAS));
    cellRect.maxX = static_cast<int32_t>(roundf(-(box.b.y - MAP_HALF_EXTENT) * CELLS_PER_YARD - CELL_ROUND_BIAS));

    bool hit = false;

    for (int32_t chunkRow = cellRect.minY >> 3; chunkRow <= (cellRect.maxY >> 3); chunkRow++) {
        for (int32_t chunkCol = cellRect.minX >> 3; chunkCol <= (cellRect.maxX >> 3); chunkCol++) {
            hit |= MapQueryBoxChunk(chunkCol, chunkRow, cellRect, box, object, queryMask);
        }
    }

    return hit;
}

// ref: FUN_007a6af0
bool MapQueryBox(const CAaBox& box, void* object, uint32_t queryMask) {
    bool hit = false;

    if (queryMask & 0x300f0) {
        // TODO FUN_007a6940, 419 bytes: the MAP-OBJECT half. It walks the loaded instance list
        // (DAT_00d25438 / DAT_00d25440, which frozen does not carry yet), brings the box into each
        // instance's space through its matrix at +0xb0, tests the instance bounds, then walks its
        // groups (FUN_007aef00, 252 bytes) and each group's BSP against the plain box
        // (FUN_007cb7b0, 183 bytes, the sibling of CMapObjGroup::QueryBox that takes a box where
        // QueryBox takes a plane hull, calling the box node walk FUN_007ca920 and then
        // CMapObjGroup::RecordHits, both of which frozen already has). Until it lands a blob does
        // not fall on a building floor through this path; BlobShadowDrawWmo covers that case its own
        // way. docs/ref/parity-shadows.md maps the chain function by function.
    }

    if (queryMask & 0x30100) {
        // Both halves run: the reference ORs their results rather than short-circuiting.
        hit |= MapQueryBoxTerrain(box, object, queryMask);
    }

    return hit;
}

// ref: FUN_007c8bf0
// How many of the group's liquid tiles actually render. The name comes from the identification in
// docs/ref/parity-liquid.md; note that it RETURNS NOTHING -- it computes into m_liquidTileCount and
// callers read that, which is the reference's own shape.
//
// A TILE RENDERS when its flag byte has bit 0x80 set and its low nibble is not 0xF. The reference
// spells the second half as a SIGNED byte compare against zero, which is the same test for bit 0x80
// and is worth keeping in mind when reading the disassembly.
//
// The cache is a plain int and "below 1" is the not-computed marker, so a group whose tiles are all
// hidden recomputes its zero on every call. That is the reference's behaviour and it is harmless:
// the walk is at most m_liquidXTiles * m_liquidYTiles byte tests.
//
// No null check on m_liquidTiles, matching the reference, and it is safe for the same reason there:
// both tile counts and the tile pointer come out of the same MLIQ chunk, so a group without MLIQ has
// zero for both extents and neither loop body runs.
//
// Ported for the map-object water mesh, which sizes its vertex buffer as
// m_liquidXVerts * m_liquidYVerts + m_liquidTileCount * 6. Nothing calls it yet -- that builder,
// Liquid::CMeshGeomFactory, is the remaining liquid gap -- so this lands as its documented
// prerequisite rather than as a behaviour change.
void CMapObjGroup::LiquidTileCount() {
    if (this->m_liquidTileCount >= 1) {
        return;
    }

    int32_t count = 0;

    const uint8_t* tile = this->m_liquidTiles;

    for (int32_t y = static_cast<int32_t>(this->m_liquidYTiles); y > 0; y--) {
        for (int32_t x = static_cast<int32_t>(this->m_liquidXTiles); x > 0; x--) {
            if ((*tile & 0xF) != 0xF && (*tile & 0x80)) {
                count++;
            }

            tile++;
        }
    }

    this->m_liquidTileCount = count;
}

// ref: FUN_007cbdc0
// Make sure the group's liquid buffer pair exists and is big enough, then hand the geometry factory
// the holder it caches.
//
// EACH SLOT IS FILLED ONLY IF STILL NULL, so a second surface over the same group reuses the pair
// rather than reallocating -- which is what makes the claim count meaningful and is why the factory
// caches a holder at all rather than owning buffers itself.
//
// Clearing the CGxBuf's first ready flag is what marks a freshly taken buffer as not yet holding
// geometry. CMeshGeomFactory::Build tests that flag and its neighbour on BOTH buffers, and skips the
// whole rebuild when all four are set -- so this clear is what forces the first build to actually
// run. Frozen leaves the two flags as CGxBuf's unk1C and unk1D rather than renaming them, because
// what sets them is not yet ported and a name would be a guess.
//
// DIVERGENCE on the holder. The reference hands back an interior pointer, group + 0x20, and frozen's
// layout already diverges in exactly that region -- the reference keeps the liquid vertex positions
// in a pooled VertArray behind a pointer at +0x1c where frozen owns a TSGrowableArray. So an
// interior offset there would name nothing. The group pointer carries the same information for every
// use ported so far, which is as a cache key the factory compares and passes back.
void CMapObjGroup::AcquireLiquidBuffers(EGxVertexBufferFormat format, uint32_t vertexCount,
                                        uint32_t indexCount, void** holderOut) {
    if (!this->m_liquidVertexBuf) {
        uint32_t stride = GxVertexBufferFormatSize(format);

        VBBList::s_vertexList.Alloc(&this->m_liquidVertexBuf, stride, vertexCount);

        if (this->m_liquidVertexBuf && this->m_liquidVertexBuf->buf) {
            this->m_liquidVertexBuf->buf->unk1C = 0;
        }
    }

    if (!this->m_liquidIndexBuf) {
        VBBList::s_indexList.Alloc(&this->m_liquidIndexBuf, 2, indexCount);

        if (this->m_liquidIndexBuf && this->m_liquidIndexBuf->buf) {
            this->m_liquidIndexBuf->buf->unk1C = 0;
        }
    }

    *holderOut = this;

    this->m_liquidBufferUsers++;
}
