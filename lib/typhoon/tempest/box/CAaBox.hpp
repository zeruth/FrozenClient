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

// ref: FUN_007fa140
// The box a sphere fits in: the centre less the radius on each axis, and plus it.
void SphereToBox(const CAaSphere& sphere, CAaBox& box);

// 1 unless the box's minimum is strictly below its maximum on all three axes.
// ref: FUN_0070bd20
int32_t AaBoxIsDegenerate(const CAaBox& box);

// 1 when the maximum is strictly below the minimum on all three axes.
// ref: FUN_007bd450
int32_t AaBoxIsInverted(const CAaBox& box);

#endif
