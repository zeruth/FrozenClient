#ifndef TEMPEST_INTERSECT_HPP
#define TEMPEST_INTERSECT_HPP

#include "tempest/Box.hpp"
#include "tempest/Plane.hpp"
#include "tempest/Ray.hpp"
#include "tempest/Vector.hpp"
#include <cstdint>

// Ray against the indexed triangle tri[0..2] of vertices (Moller-Trumbore). t is the distance
// along ray.dir, uv the barycentric pair; epsilon widens the triangle by that much in barycentric
// space.
bool IntersectRayTriangle(const C3Ray& ray, const C3Vector* vertices, const uint16_t* tri, float* t, C2Vector* uv, float epsilon);

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

#endif
