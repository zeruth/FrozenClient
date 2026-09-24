#ifndef TEMPEST_BOX_C_AABOX_HPP
#define TEMPEST_BOX_C_AABOX_HPP

#include "tempest/Vector.hpp"

class C44Matrix;

class CAaBox {
    public:
    // Member variables
    C3Vector b;
    C3Vector t;
};

// The axis-aligned bounds of `box` after `m` is applied to it. Based at the matrix's translation
// row, then each of the three rows contributes its min and max to each axis -- the standard AABB
// transform, which is tighter than transforming the eight corners and cheaper than both.
CAaBox TransformBox(const CAaBox& box, const C44Matrix& m);

#endif
