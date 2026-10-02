#include "tempest/matrix/C34Matrix.hpp"
#include <cmath>

// ref: FUN_00984310
// l then r: the rotations multiplied, and l's translation carried through r and offset by r's.
C34Matrix operator*(const C34Matrix& l, const C34Matrix& r) {
    C34Matrix out;

    out.a0 = l.a0 * r.a0 + r.c0 * l.a2 + l.a1 * r.b0;
    out.a1 = l.a1 * r.b1 + l.a0 * r.a1 + r.c1 * l.a2;
    out.a2 = r.c2 * l.a2 + r.a2 * l.a0 + l.a1 * r.b2;
    out.b0 = r.b0 * l.b1 + r.c0 * l.b2 + l.b0 * r.a0;
    out.b1 = r.b1 * l.b1 + r.c1 * l.b2 + l.b0 * r.a1;
    out.b2 = l.b0 * r.a2 + l.b1 * r.b2 + l.b2 * r.c2;
    out.c0 = l.c0 * r.a0 + l.c1 * r.b0 + r.c0 * l.c2;
    out.c1 = r.b1 * l.c1 + r.c1 * l.c2 + l.c0 * r.a1;
    out.c2 = l.c0 * r.a2 + l.c1 * r.b2 + l.c2 * r.c2;
    out.d0 = r.a0 * l.d0 + l.d2 * r.c0 + l.d1 * r.b0 + r.d0;
    out.d1 = l.d0 * r.a1 + l.d1 * r.b1 + l.d2 * r.c1 + r.d1;
    out.d2 = r.a2 * l.d0 + l.d1 * r.b2 + l.d2 * r.c2 + r.d2;

    return out;
}

// ref: FUN_00984450
// A rotation of `angle` radians about Z, applied before this transform.
void C34Matrix::RotateAroundZ(float angle) {
    C34Matrix rotation;
    rotation.a0 = std::cos(angle);
    rotation.a1 = std::sin(angle);
    rotation.a2 = 0.0f;
    rotation.b0 = -rotation.a1;
    rotation.b1 = rotation.a0;

    *this = rotation * *this;
}
