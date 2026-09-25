#include "tempest/math/CMath.hpp"

// ref: FUN_006f7a60
void CMath::SinCos(float angle, float& sine, float& cosine) {
    sine = std::sin(angle);
    cosine = std::cos(angle);
}

// The reference branches on the sign but both arms are the same fistp under a truncating
// control word (0xc00).
// ref: FUN_00407930
int32_t CMath::fint(float n) {
    if (n > 0.0f) {
        return static_cast<int32_t>(n);
    }

    return static_cast<int32_t>(n);
}

const float CMath::PI = 3.1415927f;
const float CMath::TWO_PI = 6.2831855f;
const float CMath::OO_TWO_PI = 1.0f / CMath::TWO_PI;
const float CMath::EPSILON = 0.00000023841858f;
const float CMath::DEG2RAD = CMath::PI / 180.0f;
