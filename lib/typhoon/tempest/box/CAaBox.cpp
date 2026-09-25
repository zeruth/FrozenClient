#include "tempest/box/CAaBox.hpp"
#include "tempest/matrix/C44Matrix.hpp"
#include <cstdint>

// ref: FUN_006cb900
void CAaBox::SetPoint(const C3Vector& p) {
    this->b.x = p.x;
    this->b.y = p.y;
    this->b.z = p.z;
    this->t.x = p.x;
    this->t.y = p.y;
    this->t.z = p.z;
}

// ref: FUN_005fecb0
void CAaBox::Scale(float s) {
    this->b.x = this->b.x * s;
    this->b.y = this->b.y * s;
    this->b.z = this->b.z * s;
    this->t.x = this->t.x * s;
    this->t.y = this->t.y * s;
    this->t.z = s * this->t.z;
}

// ref: FUN_00984860
CAaBox TransformBox(const CAaBox& box, const C44Matrix& m) {
    // The translation row is where a zero-sized box would land, so both corners start there and
    // the rows below only ever widen them.
    float lo[3] = { m.d0, m.d1, m.d2 };
    float hi[3] = { m.d0, m.d1, m.d2 };

    const float row[3][3] = {
        { m.a0, m.a1, m.a2 },
        { m.b0, m.b1, m.b2 },
        { m.c0, m.c1, m.c2 }
    };

    const float boxLo[3] = { box.b.x, box.b.y, box.b.z };
    const float boxHi[3] = { box.t.x, box.t.y, box.t.z };

    for (uint32_t axis = 0; axis < 3; axis++) {
        for (uint32_t r = 0; r < 3; r++) {
            float a = boxLo[r] * row[r][axis];
            float b = boxHi[r] * row[r][axis];

            // A negative matrix element swaps which end of the box contributes the minimum, which
            // is the only reason this is a comparison rather than two additions.
            if (b <= a) {
                lo[axis] += b;
                hi[axis] += a;
            } else {
                lo[axis] += a;
                hi[axis] += b;
            }
        }
    }

    CAaBox out;

    out.b = { lo[0], lo[1], lo[2] };
    out.t = { hi[0], hi[1], hi[2] };

    return out;
}

// ref: FUN_0070bd20
int32_t AaBoxIsDegenerate(const CAaBox& box) {
    if (box.b.x < box.t.x && box.b.y < box.t.y && box.b.z < box.t.z) {
        return 0;
    }

    return 1;
}
