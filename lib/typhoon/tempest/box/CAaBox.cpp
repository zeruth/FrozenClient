#include "tempest/box/CAaBox.hpp"
#include "tempest/matrix/C44Matrix.hpp"
#include <cstdint>

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
