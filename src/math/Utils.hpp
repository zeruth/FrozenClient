#ifndef MATH_UTILS_HPP
#define MATH_UTILS_HPP

#include <cmath>
#include <cstdint>
#include <cstring>

#define WHOA_EPSILON_1 0.00000023841858
#define WHOA_EPSILON_2 0.0000099999997

// The epsilon is a float in the reference, and so is the comparison.
// ref: FUN_00482870
inline bool AreEqual(float a, float b, float epsilon) {
    return std::abs(a - b) < epsilon;
}

// The negation of AreEqual rather than `>=`, so a NaN difference counts as not equal.
// ref: FUN_004828c0
inline bool NotEqual(float a, float b, float epsilon) {
    return !(std::abs(a - b) < epsilon);
}

// The same two tests against the reference's fixed 2^-22 (0x009ea27c).
// ref: FUN_00482890
inline bool AreEqual(float a, float b) {
    return std::abs(a - b) < 0.00000023841858f;
}

// ref: FUN_00482900
inline bool NotEqual(float a, float b) {
    return !(std::abs(a - b) < 0.00000023841858f);
}

// Keep `value` inside [lo, hi): below lo becomes lo, at or above hi becomes hi.
// ref: FUN_00497a90
inline void Clamp(float& value, float lo, float hi) {
    float v = value;

    if (lo <= v && v < hi) {
        value = v;
        return;
    }

    value = lo <= v ? hi : lo;
}

// The magnitude of `magnitude` with the sign bit of `sign`, done on the bits.
// ref: FUN_00714c10
inline float CopySign(float magnitude, float sign) {
    uint32_t m;
    uint32_t s;
    std::memcpy(&m, &magnitude, sizeof(m));
    std::memcpy(&s, &sign, sizeof(s));

    uint32_t bits = ((s ^ m) & 0x7FFFFFFF) ^ s;

    float result;
    std::memcpy(&result, &bits, sizeof(result));

    return result;
}

inline float SignOf(float value) {
    return value >= 0.0 ? 1.0f : -1.0f;
}

#endif
