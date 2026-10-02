#include "world/CWFrustum.hpp"
#include <cmath>
#include <cstring>

#include <storm/Memory.hpp>
#include <tempest/Intersect.hpp>
#include <tempest/Vector.hpp>

// The records handed out to visible groups, returned at the end of the map object pass. The
// reference keeps this as a WorldScene.cpp file static at 0x00adf6a8.
static STORM_EXPLICIT_LIST(CWFrustum, link) s_freeFrustums;

// ref: FUN_00601650
CWFrustum::CWFrustum() {
    // A (0, 0, 1) normal with distance 0 -- the reference writes 1.0 into the third float of each
    // plane and leaves the rest zero, which is this.
    for (int32_t i = 0; i < 6; i++) {
        this->planes[i].n.x = 0.0f;
        this->planes[i].n.y = 0.0f;
        this->planes[i].n.z = 1.0f;
        this->planes[i].d = 0.0f;
    }

    for (int32_t i = 0; i < 8; i++) {
        this->corners[i].x = 0.0f;
        this->corners[i].y = 0.0f;
        this->corners[i].z = 0.0f;
    }

    memset(this->unknownC0, 0, sizeof(this->unknownC0));
}

// ref: FUN_007983b0
CWFrustum* CWFrustum::Alloc() {
    auto frustum = s_freeFrustums.Head();

    if (!frustum) {
        frustum = STORM_NEW(CWFrustum);

        if (!frustum) {
            return nullptr;
        }

        return frustum;
    }

    s_freeFrustums.UnlinkNode(frustum);

    return frustum;
}

void CWFrustum::Free(CWFrustum* frustum) {
    if (!frustum) {
        return;
    }

    s_freeFrustums.LinkToTail(frustum);
}

// ref: FUN_007ba960
// Flip every plane, turning the frustum inside out: what was in front of each face is now behind
// it, so a test that reported "inside" reports "outside" and the other way round.
//
// The reference negates all twenty-four floats straight through, which is the six planes' normals
// AND their distances -- both halves, or the plane would move rather than turn.
//
// Its callers are the SHADOW MAP's frustum setup, and they all run it in the same place: build the
// corners, SetCorners to derive the planes from them, negate, then carry on. That is the tell for
// what it is for -- those frusta come from GxuXformCreateOrtho, whose handedness is the opposite
// of the one SetCorners assumes, so the planes come out facing the wrong way and this turns them
// back rather than reworking the derivation.
// reference FUN_00983990
void CWFrustum::GetBounds(CAaBox& bounds) const {
    BoundsFromPoints(bounds, this->corners, 8);
}

// reference FUN_007baaa0
void CWFrustum::MirrorCorners() {
    C3Vector swap = this->corners[0];
    this->corners[0] = this->corners[3];
    this->corners[3] = swap;

    swap = this->corners[1];
    this->corners[1] = this->corners[2];
    this->corners[2] = swap;

    swap = this->corners[4];
    this->corners[4] = this->corners[7];
    this->corners[7] = swap;

    swap = this->corners[5];
    this->corners[5] = this->corners[6];
    this->corners[6] = swap;
}

void CWFrustum::NegatePlanes() {
    for (int32_t i = 0; i < 6; i++) {
        this->planes[i].n.x = -this->planes[i].n.x;
        this->planes[i].n.y = -this->planes[i].n.y;
        this->planes[i].n.z = -this->planes[i].n.z;
        this->planes[i].d = -this->planes[i].d;
    }
}

// ref: FUN_00983f40
void CWFrustum::Transform(const C44Matrix& matrix) {
    for (int32_t i = 0; i < 8; i++) {
        C3Vector out;
        TransformPointInPlace(out, this->corners[i], matrix);
    }

    this->ComputePlanes();

    // The two points just past the corners, which the occlusion code keeps there.
    C3Vector out;
    TransformPointInPlace(out, *reinterpret_cast<C3Vector*>(&this->unknownC0[0]), matrix);
    TransformPointInPlace(out, *reinterpret_cast<C3Vector*>(&this->unknownC0[3]), matrix);
}

// ref: FUN_00983ae0
void CWFrustum::Translate(const C3Vector& offset) {
    for (int32_t i = 0; i < 8; i++) {
        this->corners[i].x += offset.x;
        this->corners[i].y += offset.y;
        this->corners[i].z += offset.z;
    }

    for (int32_t i = 0; i < 6; i++) {
        C4Plane& plane = this->planes[i];
        plane.d -= offset.x * plane.n.x + plane.n.y * offset.y + plane.n.z * offset.z;
    }

    auto points = reinterpret_cast<C3Vector*>(&this->unknownC0[0]);

    for (int32_t i = 0; i < 2; i++) {
        points[i].x += offset.x;
        points[i].y += offset.y;
        points[i].z += offset.z;
    }
}

// ref: FUN_00791640
int32_t FrustumUnitBasis(const CWFrustum& frustum, C44Matrix& out, int32_t translate) {
    const C3Vector& origin = frustum.corners[0];

    out = C44Matrix();

    out.a0 = frustum.corners[3].x - origin.x;
    out.a1 = frustum.corners[3].y - origin.y;
    out.a2 = frustum.corners[3].z - origin.z;
    out.b0 = frustum.corners[1].x - origin.x;
    out.b1 = frustum.corners[1].y - origin.y;
    out.b2 = frustum.corners[1].z - origin.z;
    out.c0 = frustum.corners[4].x - origin.x;
    out.c1 = frustum.corners[4].y - origin.y;
    out.c2 = frustum.corners[4].z - origin.z;

    if (translate) {
        out.d0 = origin.x;
        out.d1 = origin.y;
        out.d2 = origin.z;
    }

    float det = out.Determinant();

    if (2.384185791015625e-07f <= std::fabs(det)) {
        out = out.Inverse(det);

        return 1;
    }

    out = C44Matrix();

    return 0;
}

namespace {

// One point's distance inside each of the cube's six faces, and the sign bits of those
// distances packed from bit 31 down: a set bit is a face the point is outside.
struct UnitClipVertex {
    float d[6];
    uint32_t outcode;
};

// ref: FUN_0078ffa0
void UnitClipClassify(UnitClipVertex& vertex, const C3Vector& point) {
    vertex.d[0] = point.x;
    vertex.d[1] = 1.0f - point.x;
    vertex.d[2] = point.y;
    vertex.d[3] = 1.0f - point.y;
    vertex.d[4] = point.z;
    vertex.d[5] = 1.0f - point.z;

    vertex.outcode = 0;

    for (int32_t i = 0; i < 6; i++) {
        if (std::signbit(vertex.d[i])) {
            vertex.outcode |= 0x80000000u >> i;
        }
    }
}

struct UnitClipPolygon {
    const C3Vector** points;
    const UnitClipVertex** vertices;
    uint32_t count;
};

}

// ref: FUN_00791380
int32_t ClipToUnitCube(const C3Vector* points, uint32_t count, const C3Vector* const** clipped, uint32_t* clippedCount) {
    static const C3Vector* s_pointsA[32];   // DAT_00cdd380
    static const C3Vector* s_pointsB[32];   // DAT_00cdd300
    static C3Vector s_newPoints[32];        // DAT_00cdd400

    if (!count) {
        return 0;
    }

    UnitClipVertex inputs[31];
    const UnitClipVertex* verticesA[32];
    const UnitClipVertex* verticesB[32];

    uint32_t all = 0xffffffffu;
    uint32_t any = 0;

    for (uint32_t i = 0; i < count; i++) {
        UnitClipClassify(inputs[i], points[i]);
        verticesA[i] = &inputs[i];
        all &= inputs[i].outcode;
        any |= inputs[i].outcode;
        s_pointsA[i] = &points[i];
    }

    if (all) {
        return 0;
    }

    if (!any) {
        *clipped = s_pointsA;
        *clippedCount = count;

        return 1;
    }

    UnitClipVertex made[32];
    uint32_t madeCount = 0;
    C3Vector* next = s_newPoints;

    UnitClipPolygon a = { s_pointsA, verticesA, count };
    UnitClipPolygon b = { s_pointsB, verticesB, 0 };
    UnitClipPolygon* src = &a;
    UnitClipPolygon* dst = &b;

    uint32_t mask = 0x80000000u;

    for (int32_t plane = 0; plane < 6; plane++, mask >>= 1) {
        if (!(any & mask)) {
            continue;
        }

        dst->count = 0;

        uint32_t prev = src->count - 1;
        uint32_t prevOut = src->vertices[prev]->outcode & mask;

        for (uint32_t cur = 0; cur < src->count; cur++) {
            uint32_t curOut = src->vertices[cur]->outcode & mask;

            if (curOut != prevOut) {
                float denom = src->vertices[prev]->d[plane] - src->vertices[cur]->d[plane];

                if (denom == 0.0f) {
                    denom = 9.999999747378752e-05f;
                }

                float t = src->vertices[prev]->d[plane] / denom;
                const C3Vector& p0 = *src->points[prev];
                const C3Vector& p1 = *src->points[cur];

                C3Vector* point = next++;
                point->x = (p1.x - p0.x) * t + p0.x;
                point->y = (p1.y - p0.y) * t + p0.y;
                point->z = (p1.z - p0.z) * t + p0.z;

                UnitClipVertex* vertex = &made[madeCount++];
                UnitClipClassify(*vertex, *point);

                dst->points[dst->count] = point;
                dst->vertices[dst->count] = vertex;
                dst->count++;
            }

            if (!curOut) {
                dst->points[dst->count] = src->points[cur];
                dst->vertices[dst->count] = src->vertices[cur];
                dst->count++;
            }

            prevOut = curOut;
            prev = cur;
        }

        if (!dst->count) {
            return 0;
        }

        UnitClipPolygon* swap = src;
        src = dst;
        dst = swap;
    }

    *clipped = src->points;
    *clippedCount = src->count;

    return 1;
}
