#include "tempest/quaternion/C4Quaternion.hpp"
#include "tempest/math/CMath.hpp"
#include <cmath>

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

// ref: FUN_00982460
C4Quaternion C4Quaternion::Slerp(float ratio, const C4Quaternion& q1, const C4Quaternion& q2) {
    float dot = q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;

    // Negating the SECOND quaternion's contribution rather than the quaternion itself is
    // how the reference takes the shortest arc -- same result, one multiply.
    float sign = 1.0f;

    if (dot < 0.0f) {
        sign = -1.0f;
        dot = -dot;
    }

    // The fabs is the reference's: dot can exceed 1 by a rounding step, and the sqrt of a
    // small negative would be a NaN that propagates into every bone below this one.
    float sinTheta = CMath::sqrt(CMath::fabs(1.0f - dot * dot));

    // 2^-21, at 0x00aa2e58. Two quaternions this close have no meaningful arc between
    // them, and dividing by sinTheta below would blow up.
    if (CMath::fabs(sinTheta) < 0.00000047683716f) {
        return q1;
    }

    float theta = std::atan2(sinTheta, dot);
    float inv = 1.0f / sinTheta;

    float a = std::sin((1.0f - ratio) * theta) * inv;
    float b = sign * std::sin(theta * ratio) * inv;

    return {
        q1.x * a + q2.x * b,
        q1.y * a + q2.y * b,
        q1.z * a + q2.z * b,
        q1.w * a + q2.w * b
    };
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
