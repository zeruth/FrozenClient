#ifndef TEMPEST_INTERSECT_HPP
#define TEMPEST_INTERSECT_HPP

#include "tempest/Box.hpp"
#include "tempest/Plane.hpp"
#include "tempest/Ray.hpp"
#include "tempest/Segment.hpp"
#include "tempest/Vector.hpp"
#include <cstdint>

// Ray against the indexed triangle tri[0..2] of vertices (Moller-Trumbore). t is the distance
// along ray.dir, uv the barycentric pair; epsilon widens the triangle by that much in barycentric
// space.
bool IntersectRayTriangle(const C3Ray& ray, const C3Vector* vertices, const uint16_t* tri, float* t, C2Vector* uv, float epsilon);

// The same test over 32-bit indices: a separate function in the reference, same arithmetic.
bool IntersectRayTriangle(const C3Ray& ray, const C3Vector* vertices, const int32_t* tri, float* t, C2Vector* uv, float epsilon);

// Outcode of a point against six planes: bit i is set when the point lies behind plane i.
void ClassifyPointPlanes6(const C4Plane* planes, const C3Vector& point, uint8_t* outcode);

// The axis-aligned bounds of n points (zero when n is 0).
void BoundsFromPoints(CAaBox& out, const C3Vector* points, uint32_t n);

// Index (0, 1, 2) of the component with the largest magnitude; ties go to the later axis.
uint32_t DominantAxis(const C3Vector& v);

// A box against a six-plane hull, both returning 3 (accept) or 0 (reject). Each plane is tested
// against the box corner that maximises n.p + d, so one dot product decides the whole box.
//
// AaBoxVsPlanes6 rejects as soon as one plane has the entire box behind it: the frustum test the
// map and terrain culling use. AaBoxBehindPlanes6 is the opposite question, rejecting unless the
// box is behind every plane. Both carry the reference's 0.0194 slack.
uint32_t AaBoxVsPlanes6(const C4Plane* planes, const CAaBox& box);
uint32_t AaBoxBehindPlanes6(const C4Plane* planes, const CAaBox& box);

// A ray from `origin` toward `target`. With `normalize` set the direction comes out unit
// length, so a distance along it is a real distance. ref: FUN_00985200
void RayFromPoints(C3Ray& out, const C3Vector& origin, const C3Vector& target, bool normalize);

// Ray against an infinite plane. Writes how far along the ray the hit is to `t` and the hit
// itself to `point`, either of which may be null. A ray running along the plane counts as a
// hit at its own origin when it is within `epsilon` of the plane, and misses otherwise.
// ref: FUN_00982fb0
bool IntersectRayPlane(const C3Ray& ray, const C4Plane& plane, float* t, C3Vector* point, float epsilon);

// Whether a point lies inside a planar polygon, judged after dropping the axis the polygon
// faces most squarely along (pass DominantAxis of its normal). Crossing-number test.
// ref: FUN_009830d0
bool PointInPolygon(const C3Vector& point, const C3Vector* vertices, int32_t count, uint32_t dominantAxis);

// Squared distance from a point to a segment, and where along the segment the nearest point
// is (0 at the start, 1 at the end); `t` may be null. ref: FUN_00984ce0
float DistancePointSegmentSq(const C3Segment& segment, const C3Vector& point, float* t);

// Distance from a point to the nearest edge of a polygon. ref: FUN_00984db0
float DistancePointPolygon(const C3Vector& point, const C3Vector* vertices, int32_t count);

#endif
