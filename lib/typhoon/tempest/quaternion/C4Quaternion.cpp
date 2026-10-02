#include "tempest/matrix/C44Matrix.hpp"
#include <cstdint>
#include "tempest/quaternion/C4Quaternion.hpp"
#include "tempest/math/CMath.hpp"
#include "tempest/matrix/C33Matrix.hpp"
#include "tempest/vector/C3Vector.hpp"
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

// ref: FUN_00982630
C4Quaternion C4Quaternion::Nlerp(float ratio, const C4Quaternion& q1, const C4Quaternion& q2) {
    C4Quaternion result;

    result.x = q1.x + (q2.x - q1.x) * ratio;
    result.y = (q2.y - q1.y) * ratio + q1.y;
    result.z = (q2.z - q1.z) * ratio + q1.z;
    result.w = ratio * (q2.w - q1.w) + q1.w;

    result.NormalizeFast();

    return result;
}

// ref: FUN_00982570
void C4Quaternion::NormalizeFast() {
    float m = this->x * this->x + this->y * this->y + this->z * this->z + this->w * this->w;
    float scale = 1.021435f - (m - 0.95906597f) * 0.532516f;

    if (m <= 0.91521198f) {
        scale = (1.021435f - (scale * scale * m - 0.95906597f) * 0.532516f) * scale;

        if (m <= 0.6521197f) {
            scale = scale * (1.021435f - 0.532516f * (scale * scale * m - 0.95906597f));
        }
    }

    this->x *= scale;
    this->y *= scale;
    this->z = scale * this->z;
    this->w = scale * this->w;
}

// ref: FUN_00979110
void C4Quaternion::Normalize() {
    float lengthSquared = this->x * this->x + this->y * this->y + this->z * this->z + this->w * this->w;

    // 2^-22 (0x009ea27c).
    if (lengthSquared > 0.00000023841858f) {
        float scale = 1.0f / CMath::sqrt(lengthSquared);

        this->x *= scale;
        this->y *= scale;
        this->z = scale * this->z;
        this->w = scale * this->w;
    }
}

// ref: FUN_009826a0
// The quaternion of a row-major 3x3 rotation `m` whose trace the caller has already summed: the
// usual square-root extraction, through w when the trace is positive and otherwise through the
// largest diagonal element, walking the axes with the reference's next-axis table (0x00aa2e4c).
static void C4QuaternionFromRotation(const float* m, float trace, C4Quaternion& q) {
    static const int32_t s_next[3] = { 1, 2, 0 };

    float half = 0.5f;

    if (trace > 0.0f) {
        trace = trace + 1.0f;
        q.w = CMath::sqrt(trace) * 0.5f;
        half = half / CMath::sqrt(trace);
        q.x = (m[7] - m[5]) * half;
        q.y = (m[2] - m[6]) * half;
        q.z = (m[3] - m[1]) * half;

        return;
    }

    float* components = &q.x;

    int32_t i = m[0] < m[4] ? 1 : 0;

    if (m[i * 4] < m[8]) {
        i = 2;
    }

    int32_t j = s_next[i];
    int32_t k = s_next[j];

    float root = CMath::sqrt(((m[i * 4] - m[j * 4]) - m[k * 4]) + 1.0f);
    components[i] = root * 0.5f;
    half = half / root;

    q.w = (m[k * 3 + j] - m[j * 3 + k]) * half;
    components[j] = (m[i * 3 + j] + m[j * 3 + i]) * half;
    components[k] = (m[i * 3 + k] + m[k * 3 + i]) * half;
}

// ref: FUN_009828b0
// Through the transpose: the extraction above reads the matrix column-major.
C4Quaternion::C4Quaternion(const C33Matrix& m) {
    const float transposed[9] = {
        m.a0, m.b0, m.c0,
        m.a1, m.b1, m.c1,
        m.a2, m.b2, m.c2,
    };

    C4QuaternionFromRotation(transposed, m.c2 + m.b1 + m.a0, *this);
}

// ref: FUN_00982400
C4Quaternion::C4Quaternion(float angle, const C3Vector& axis) {
    float c = std::cos(angle * 0.5f);
    float s = std::sin(angle * 0.5f);

    this->w = c;
    this->x = axis.x * s;
    this->y = axis.y * s;
    this->z = s * axis.z;
}

// ref: FUN_00982340
// A unit quaternion packed into 64 bits: x in the top 22 bits (scaled by 2^-21), y and z in the
// next two 21-bit fields (2^-20), all signed; w is rebuilt as sqrt(1 - |xyz|^2), or 0 when xyz is
// already within 2^-20 of unit length.
C4Quaternion::C4Quaternion(uint64_t packed) {
    this->x = 0.0f;
    this->y = 0.0f;
    this->z = 0.0f;
    this->w = 0.0f;

    this->x = static_cast<float>(static_cast<int32_t>(static_cast<int64_t>(packed) >> 42)) * 4.76837158203125e-07f;
    this->y = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(packed >> 10)) >> 11) * 9.5367431640625e-07f;
    this->z = static_cast<float>(static_cast<int32_t>(static_cast<int64_t>(packed << 43) >> 43)) * 9.5367431640625e-07f;

    float lengthSquared = this->x * this->x + this->y * this->y + this->z * this->z;

    if (std::fabs(1.0f - lengthSquared) < 9.5367431640625e-07f) {
        this->w = 0.0f;
    } else {
        this->w = std::sqrt(1.0f - lengthSquared);
    }
}

// ref: FUN_009827b0
// The rotation in a row-major 3x3 (the upper left of `m`, given as 16 floats) as a quaternion,
// from its trace when that is positive, otherwise from its largest diagonal entry.
static void QuaternionFromRotation(const float* m, float trace, C4Quaternion& out) {
    static const uint32_t s_next[3] = { 1, 2, 0 };
    float* q = &out.x;

    if (trace > 0.0f) {
        float root = std::sqrt(trace + 1.0f);
        float scale = 0.5f / root;

        out.w = root * 0.5f;
        out.x = (m[9] - m[6]) * scale;
        out.y = (m[2] - m[8]) * scale;
        out.z = (m[4] - m[1]) * scale;

        return;
    }

    uint32_t i = m[0] < m[5] ? 1 : 0;

    if (m[i * 5] < m[10]) {
        i = 2;
    }

    uint32_t j = s_next[i];
    uint32_t k = s_next[j];

    float root = std::sqrt((m[i * 5] - m[j * 5]) - m[k * 5] + 1.0f);
    float scale = 0.5f / root;

    q[i] = root * 0.5f;
    out.w = (m[j + k * 4] - m[k + j * 4]) * scale;
    q[j] = (m[i + j * 4] + m[j + i * 4]) * scale;
    q[k] = (m[i + k * 4] + m[k + i * 4]) * scale;
}

// ref: FUN_00982910
// The rotation part of a 4x4 matrix, taken from its transpose.
C4Quaternion::C4Quaternion(const C44Matrix& m) {
    C44Matrix t = m.Transpose();
    QuaternionFromRotation(&t.a0, t.c2 + t.b1 + t.a0, *this);
}
