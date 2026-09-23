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

#endif
