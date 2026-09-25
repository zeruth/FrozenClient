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

// ref: FUN_00985200
void RayFromPoints(C3Ray& out, const C3Vector& origin, const C3Vector& target, bool normalize) {
    C3Vector dir = {
        target.x - origin.x,
        target.y - origin.y,
        target.z - origin.z
    };

    if (normalize) {
        float inv = 1.0f / sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

        dir.x *= inv;
        dir.y *= inv;
        dir.z *= inv;
    }

    out.origin = origin;
    out.dir = dir;
}

// ref: FUN_00982fb0
bool IntersectRayPlane(const C3Ray& ray, const C4Plane& plane, float* t, C3Vector* point, float epsilon) {
    float along = plane.n.x * ray.dir.x + plane.n.y * ray.dir.y + plane.n.z * ray.dir.z;

    // A ray running along the plane: it either lies in it, or never meets it.
    if (fabsf(along) < 1.0000000116860974e-07f) {
        float distance = plane.n.x * ray.origin.x + plane.n.y * ray.origin.y + plane.n.z * ray.origin.z + plane.d;

        if (epsilon <= fabsf(distance)) {
            return false;
        }

        if (t) {
            *t = 0.0f;
        }

        if (point) {
            *point = ray.origin;
        }

        return true;
    }

    if (!t && !point) {
        return true;
    }

    float distance = plane.n.x * ray.origin.x + plane.n.y * ray.origin.y + plane.n.z * ray.origin.z + plane.d;
    float hit = epsilon <= fabsf(distance) ? -(distance / along) : 0.0f;

    if (t) {
        *t = hit;
    }

    if (point) {
        point->x = ray.origin.x + hit * ray.dir.x;
        point->y = ray.origin.y + hit * ray.dir.y;
        point->z = ray.origin.z + hit * ray.dir.z;
    }

    return true;
}

// The two components a polygon is judged on once the axis it faces along is dropped
// (DAT_00b2d6f4 and DAT_00b2d6f8, interleaved in the reference).
static const uint32_t s_projectAxisA[3] = { 2, 0, 1 };
static const uint32_t s_projectAxisB[3] = { 1, 2, 0 };

// ref: FUN_009830d0
// The reference unrolls this four edges at a time; the arithmetic per edge is the same.
bool PointInPolygon(const C3Vector& point, const C3Vector* vertices, int32_t count, uint32_t dominantAxis) {
    if (count < 3) {
        return false;
    }

    uint32_t a = s_projectAxisA[dominantAxis];
    uint32_t b = s_projectAxisB[dominantAxis];

    auto component = [](const C3Vector& v, uint32_t i) {
        return reinterpret_cast<const float*>(&v)[i];
    };

    bool inside = false;
    int32_t j = count - 1;

    for (int32_t i = 0; i < count; j = i, i++) {
        bool aboveI = component(point, a) < component(vertices[i], a);
        bool aboveJ = component(point, a) < component(vertices[j], a);

        if (aboveI == aboveJ) {
            continue;
        }

        float lhs = (component(vertices[i], a) - component(point, a))
                  * (component(vertices[j], b) - component(vertices[i], b));
        float rhs = (component(vertices[j], a) - component(vertices[i], a))
                  * (component(vertices[i], b) - component(point, b));

        if ((rhs < lhs) == aboveI) {
            inside = !inside;
        }
    }

    return inside;
}

// ref: FUN_00984ce0
float DistancePointSegmentSq(const C3Segment& segment, const C3Vector& point, float* t) {
    C3Vector toPoint = {
        point.x - segment.start.x,
        point.y - segment.start.y,
        point.z - segment.start.z
    };

    C3Vector along = {
        segment.end.x - segment.start.x,
        segment.end.y - segment.start.y,
        segment.end.z - segment.start.z
    };

    float projected = along.x * toPoint.x + along.y * toPoint.y + along.z * toPoint.z;
    float where = 0.0f;

    if (projected > 0.0f) {
        float lengthSq = along.x * along.x + along.y * along.y + along.z * along.z;

        if (projected <= lengthSq) {
            where = projected / lengthSq;

            along.x *= where;
            along.y *= where;
            along.z *= where;
        } else {
            where = 1.0f;
        }

        toPoint.x -= along.x;
        toPoint.y -= along.y;
        toPoint.z -= along.z;
    }

    if (t) {
        *t = where;
    }

    return toPoint.x * toPoint.x + toPoint.y * toPoint.y + toPoint.z * toPoint.z;
}

// ref: FUN_00984db0
float DistancePointPolygon(const C3Vector& point, const C3Vector* vertices, int32_t count) {
    float nearest = 3.4028234663852886e+38f;

    if (count <= 0) {
        return nearest;
    }

    int32_t previous = count - 1;

    for (int32_t i = 0; i < count; previous = i, i++) {
        C3Segment edge;
        edge.start = vertices[previous];
        edge.end = vertices[i];

        float distance = sqrtf(DistancePointSegmentSq(edge, point, nullptr));

        if (distance < nearest) {
            nearest = distance;
        }
    }

    return nearest;
}

// ref: FUN_00482870
bool NearlyEqual(float a, float b, float epsilon) {
    return fabsf(a - b) < epsilon;
}

// ref: FUN_007a72a0
uint32_t ClipPolygonToPlanes(const C4Plane* planes, uint32_t planeCount,
                             const C3Vector* vertices, uint32_t count, C3Vector* out) {
    if (!count || count > CLIP_POLYGON_MAX) {
        return 0;
    }

    // Two buffers the clip ping-pongs between, one plane at a time.
    C3Vector buffers[2][CLIP_POLYGON_MAX];
    uint8_t side[CLIP_POLYGON_MAX + 1];
    float distance[CLIP_POLYGON_MAX + 1];

    uint32_t current = 0;

    for (uint32_t i = 0; i < count; i++) {
        buffers[0][i] = vertices[i];
    }

    uint32_t counts[2] = { count, 0 };

    for (uint32_t p = 0; p < planeCount; p++) {
        const C3Vector* in = buffers[current];
        uint32_t inCount = counts[current];
        uint32_t next = (current + 1) & 1;
        C3Vector* dst = buffers[next];

        if (!inCount) {
            return 0;
        }

        const C4Plane& plane = planes[p];

        for (uint32_t i = 0; i < inCount; i++) {
            float d = in[i].x * plane.n.x + in[i].y * plane.n.y + in[i].z * plane.n.z + plane.d;

            distance[i] = d;

            if (d > 0.0001f) {
                side[i] = 1;
            } else if (d < -0.0001f) {
                side[i] = 2;
            } else {
                side[i] = 0;
            }
        }

        // The wrap-around entry, so the last edge is treated like any other.
        side[inCount] = side[0];
        distance[inCount] = distance[0];

        uint32_t outCount = 0;

        for (uint32_t i = 0; i < inCount; i++) {
            if (!side[i]) {
                // Lying in the plane: kept, and its edges are never cut.
                dst[outCount++] = in[i];

                continue;
            }

            if (side[i] == 1) {
                dst[outCount++] = in[i];
            }

            if (side[i + 1] && side[i + 1] != side[i]) {
                uint32_t j = (i + 1) % inCount;
                float t = distance[i] / (distance[i] - distance[j]);

                dst[outCount].x = (in[j].x - in[i].x) * t + in[i].x;
                dst[outCount].y = (in[j].y - in[i].y) * t + in[i].y;
                dst[outCount].z = (in[j].z - in[i].z) * t + in[i].z;

                outCount++;
            }
        }

        if (!outCount) {
            return 0;
        }

        counts[next] = outCount;
        current = next;
    }

    uint32_t result = counts[current];

    for (uint32_t i = 0; i < result; i++) {
        out[i] = buffers[current][i];
    }

    return result;
}
