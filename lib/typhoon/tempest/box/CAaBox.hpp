#ifndef TEMPEST_BOX_C_AABOX_HPP
#define TEMPEST_BOX_C_AABOX_HPP

#include "tempest/Vector.hpp"
#include <cstdint>

class C44Matrix;
class CAaSphere;

class CAaBox {
    public:
    // Member variables
    C3Vector b;
    C3Vector t;

    // Member functions

    // ref: FUN_006cb900
    // Collapse the box onto one point: both corners become p.
    void SetPoint(const C3Vector& p);

    // Grow this box to also contain `other`, and hand the result back as well. Both, because
    // the reference writes the union into `this` AND into its return slot -- its seventeen
    // callers use it either way. Tagged on the definition in the .cpp.
    CAaBox GrowToInclude(const CAaBox& other);

    // ref: FUN_005fecb0
    // Both corners scaled about the origin.
    void Scale(float s);

    // ref: FUN_004f5de0
    // The midpoint of the two corners.
    void GetCenter(C3Vector& center) const;

    // ref: FUN_0075b5b0
    // 1 when p lies in the box, faces included.
    int32_t IsPointInside(const C3Vector& p) const;

    // ref: FUN_0078f370
    // 1 when the two boxes overlap on all three axes; touching faces count.
    int32_t Intersects(const CAaBox& other) const;
};

// The axis-aligned bounds of `box` after `m` is applied to it. Based at the matrix's translation
// row, then each of the three rows contributes its min and max to each axis -- the standard AABB
// transform, which is tighter than transforming the eight corners and cheaper than both.
CAaBox TransformBox(const CAaBox& box, const C44Matrix& m);

class C33Matrix;

// The same transform through a 3x3, with no translation: `out` starts at the origin and each axis
// gathers the smaller and larger product of every input axis.
void TransformBoxExtents(const C33Matrix& m, const CAaBox& box, CAaBox& out);

// The smallest box containing both: min of the minima, max of the maxima.
// ref: FUN_007150d0
CAaBox AaBoxUnion(const CAaBox& a, const CAaBox& b);

// ref: FUN_007fa140
// The box a sphere fits in: the centre less the radius on each axis, and plus it.
void SphereToBox(const CAaSphere& sphere, CAaBox& box);

// 1 unless the box's minimum is strictly below its maximum on all three axes.
// ref: FUN_0070bd20
int32_t AaBoxIsDegenerate(const CAaBox& box);

// ref: FUN_009855f0
// The box holding every sphere of the array; all zero for none.
void SpheresToBox(const CAaSphere* spheres, uint32_t count, CAaBox& box);

// ref: FUN_00985750
// A sphere holding every sphere of the array: centred on their bounding box, wide enough for the
// farthest. A degenerate box gives a zero radius at its minimum corner.
void SphereBoundSpheres(CAaSphere& out, const CAaSphere* spheres, uint32_t count);

// 1 when the maximum is strictly below the minimum on all three axes.
// ref: FUN_007bd450
int32_t AaBoxIsInverted(const CAaBox& box);

#endif
