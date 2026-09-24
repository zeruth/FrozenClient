#include "tempest/quaternion/C4Quaternion.hpp"

// The Hamilton product.
//
// Transcribing this from the disassembly is not safe on its own: LLVM prints the two-operand
// reverse-subtract as `fsubrp %st, %st(1)`, and which operand is subtracted from which changes the
// sign of every component. The four components below each match the standard product term for
// term, which is what settles the reading -- ST(1) - ST(0).
//
// ref: FUN_004f4320
C4Quaternion operator*(const C4Quaternion& a, const C4Quaternion& b) {
    return C4Quaternion(
        a.w * b.x + a.y * b.z + a.x * b.w - a.z * b.y,
        a.y * b.w + a.w * b.y + a.z * b.x - a.x * b.z,
        a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

C4Quaternion C4Quaternion::Nlerp(float ratio, const C4Quaternion& q1, const C4Quaternion& q2) {
    float x = (q2.x - q1.x) * ratio + q1.x;
    float y = (q2.y - q1.y) * ratio + q1.y;
    float z = (q2.z - q1.z) * ratio + q1.z;
    float w = (q2.w - q1.w) * ratio + q1.w;

    float m = x * x + y * y + z * z + w * w;
    float v9 = ((m - 0.95906597) * -0.532516) + 1.021435;

    if (m <= 0.91521198) {
        v9 *= (((v9 * v9 * m) - 0.95906597) * -0.532516) + 1.021435;

        if (m <= 0.6521197) {
            v9 *= (((v9 * v9 * m) - 0.95906597) * -0.532516) + 1.021435;
        }
    }

    x *= v9;
    y *= v9;
    z *= v9;
    w *= v9;

    return { x, y, z, w };
}
