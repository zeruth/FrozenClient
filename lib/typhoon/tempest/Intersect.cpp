#include "tempest/Intersect.hpp"
#include <cmath>

// ref: FUN_00983490
bool IntersectRayTriangle(const C3Ray& ray, const C3Vector* vertices, const uint16_t* tri, float* t, C2Vector* uv, float epsilon) {
    const C3Vector& p0 = vertices[tri[0]];
    const C3Vector& p1 = vertices[tri[1]];

    float e1x = p1.x - p0.x;
    float e1y = p1.y - p0.y;
    float e1z = p1.z - p0.z;

    const C3Vector& p2 = vertices[tri[2]];

    float e2x = p2.x - p0.x;
    float e2y = p2.y - p0.y;
    float e2z = p2.z - p0.z;

    float px = ray.dir.y * e2z - ray.dir.z * e2y;
    float py = ray.dir.z * e2x - ray.dir.x * e2z;
    float pz = ray.dir.x * e2y - ray.dir.y * e2x;

    float det = px * e1x + py * e1y + pz * e1z;

    if (-1.0e-6f < det && det < 1.0e-6f) {
        return false;
    }

    float invDet = 1.0f / det;

    float tx = ray.origin.x - p0.x;
    float ty = ray.origin.y - p0.y;
    float tz = ray.origin.z - p0.z;

    float u = (tx * px + tz * pz + ty * py) * invDet;

    if (u < -epsilon) {
        return false;
    }

    if (u > epsilon + 1.0f) {
        return false;
    }

    float qx = e1z * ty - tz * e1y;
    float qy = tz * e1x - e1z * tx;
    float qz = tx * e1y - ty * e1x;

    float v = (ray.dir.x * qx + ray.dir.y * qy + ray.dir.z * qz) * invDet;

    if (v < -epsilon) {
        return false;
    }

    if (v + u > epsilon + 1.0f) {
        return false;
    }

    if (t) {
        *t = (qx * e2x + qy * e2y + qz * e2z) * invDet;
    }

    if (uv) {
        uv->x = u;
        uv->y = v;
    }

    return true;
}

// Transcribed from its own decompilation rather than folded into the one above: it reads the same
// three constants (-1e-6 at 0x00aa2e70, 1e-6 at 0x00a32b48, 1.0 at 0x009e1130) in the same order.
//
// ref: FUN_009836b0
bool IntersectRayTriangle(const C3Ray& ray, const C3Vector* vertices, const int32_t* tri, float* t, C2Vector* uv, float epsilon) {
    const C3Vector& p0 = vertices[tri[0]];
    const C3Vector& p1 = vertices[tri[1]];

    float e1x = p1.x - p0.x;
    float e1y = p1.y - p0.y;
    float e1z = p1.z - p0.z;

    const C3Vector& p2 = vertices[tri[2]];

    float e2x = p2.x - p0.x;
    float e2y = p2.y - p0.y;
    float e2z = p2.z - p0.z;

    float px = ray.dir.y * e2z - ray.dir.z * e2y;
    float py = ray.dir.z * e2x - ray.dir.x * e2z;
    float pz = ray.dir.x * e2y - ray.dir.y * e2x;

    float det = px * e1x + py * e1y + pz * e1z;

    if (-1.0e-6f < det && det < 1.0e-6f) {
        return false;
    }

    float invDet = 1.0f / det;

    float tx = ray.origin.x - p0.x;
    float ty = ray.origin.y - p0.y;
    float tz = ray.origin.z - p0.z;

    float u = (tx * px + tz * pz + ty * py) * invDet;

    if (u < -epsilon) {
        return false;
    }

    if (!(u <= epsilon + 1.0f)) {
        return false;
    }

    float qx = e1z * ty - tz * e1y;
    float qy = tz * e1x - e1z * tx;
    float qz = tx * e1y - ty * e1x;

    float v = (ray.dir.x * qx + ray.dir.y * qy + ray.dir.z * qz) * invDet;

    if (v < -epsilon) {
        return false;
    }

    if (!(v + u <= epsilon + 1.0f)) {
        return false;
    }

    if (t) {
        *t = (qx * e2x + qy * e2y + qz * e2z) * invDet;
    }

    if (uv) {
        uv->x = u;
        uv->y = v;
    }

    return true;
}

// ref: FUN_00983d70
void ClassifyPointPlanes6(const C4Plane* planes, const C3Vector& point, uint8_t* outcode) {
    *outcode = 0;

    // The reference's threshold constant: a point has to sit this far inside a plane to count as
    // in front of it.
    const float threshold = -0.019444443f;

    if (planes[0].n.x * point.x + planes[0].n.y * point.y + planes[0].n.z * point.z + planes[0].d < threshold) {
        *outcode = 0x1;
    }

    if (planes[1].n.x * point.x + planes[1].n.y * point.y + planes[1].n.z * point.z + planes[1].d < threshold) {
        *outcode |= 0x2;
    }

    if (planes[2].n.x * point.x + planes[2].n.y * point.y + planes[2].n.z * point.z + planes[2].d < threshold) {
        *outcode |= 0x4;
    }

    if (planes[3].n.x * point.x + planes[3].n.y * point.y + planes[3].n.z * point.z + planes[3].d < threshold) {
        *outcode |= 0x8;
    }

    if (planes[4].n.x * point.x + planes[4].n.y * point.y + planes[4].n.z * point.z + planes[4].d < threshold) {
        *outcode |= 0x10;
    }

    if (planes[5].n.x * point.x + planes[5].n.y * point.y + planes[5].n.z * point.z + planes[5].d < threshold) {
        *outcode |= 0x20;
    }
}

// ref: FUN_00984930
void BoundsFromPoints(CAaBox& out, const C3Vector* points, uint32_t n) {
    if (n == 0) {
        out.b = { 0.0f, 0.0f, 0.0f };
        out.t = { 0.0f, 0.0f, 0.0f };
        return;
    }

    // The reference unrolls this loop four points at a time; the result is the same.
    C3Vector mn = points[0];
    C3Vector mx = points[0];

    for (uint32_t i = 1; i < n; i++) {
        const C3Vector& p = points[i];

        if (p.x < mn.x) mn.x = p.x;
        if (p.y < mn.y) mn.y = p.y;
        if (p.z < mn.z) mn.z = p.z;
        if (mx.x < p.x) mx.x = p.x;
        if (mx.y < p.y) mx.y = p.y;
        if (mx.z < p.z) mx.z = p.z;
    }

    out.b = mn;
    out.t = mx;
}

namespace {

// The corner of the box that maximises n.p, taken per component: the reference indexes a two
// entry table with the sign bit of each normal component, so a positive component reads the box
// maximum and a negative one the minimum.
float MaxCornerDot(const C4Plane& plane, const CAaBox& box) {
    const float* t = &box.t.x;
    const float* b = &box.b.x;

    float x = (plane.n.x >= 0.0f ? t : b)[0];
    float y = (plane.n.y >= 0.0f ? t : b)[1];
    float z = (plane.n.z >= 0.0f ? t : b)[2];

    return plane.n.x * x + plane.n.y * y + plane.n.z * z + plane.d;
}

} // namespace

// ref: FUN_009839e0
uint32_t AaBoxVsPlanes6(const C4Plane* planes, const CAaBox& box) {
    for (uint32_t i = 0; i < 6; i++) {
        if (MaxCornerDot(planes[i], box) < -0.019444443f) {
            return 0;
        }
    }

    return 3;
}

// ref: FUN_00983a60
uint32_t AaBoxBehindPlanes6(const C4Plane* planes, const CAaBox& box) {
    for (uint32_t i = 0; i < 6; i++) {
        if (MaxCornerDot(planes[i], box) > 0.019444443f) {
            return 0;
        }
    }

    return 3;
}

// ref: FUN_009829b0
uint32_t DominantAxis(const C3Vector& v) {
    if (fabsf(v.x) <= fabsf(v.y)) {
        if (fabsf(v.z) < fabsf(v.y)) {
            return 1;
        }
    } else if (fabsf(v.z) < fabsf(v.x)) {
        return 0;
    }

    return 2;
}
