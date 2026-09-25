#ifndef TEMPEST_BOX_C_AABOX_HPP
#define TEMPEST_BOX_C_AABOX_HPP

#include "tempest/Vector.hpp"
#include <cstdint>

class C44Matrix;

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
};

// The axis-aligned bounds of `box` after `m` is applied to it. Based at the matrix's translation
// row, then each of the three rows contributes its min and max to each axis -- the standard AABB
// transform, which is tighter than transforming the eight corners and cheaper than both.
CAaBox TransformBox(const CAaBox& box, const C44Matrix& m);

// 1 unless the box's minimum is strictly below its maximum on all three axes.
// ref: FUN_0070bd20
int32_t AaBoxIsDegenerate(const CAaBox& box);

#endif
