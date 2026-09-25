#include "tempest/vector/C2Vector.hpp"
#include "tempest/vector/C3Vector.hpp"
#include "tempest/Math.hpp"

// ref: FUN_004c4df0
C2Vector& C2Vector::operator=(const C3Vector& v) {
    this->x = v.x;
    this->y = v.y;

    return *this;
}

bool C2Vector::operator==(const C2Vector& v) {
    return this->x == v.x && this->y == v.y;
}

// C3Vector::Normalize's two-component twin, with the same 2^-22 floor (0x009ea27c): a vector
// shorter than that is left alone.
//
// ref: FUN_0048b820
void C2Vector::Normalize() {
    float sqr = this->x * this->x + this->y * this->y;

    if (0.00000023841858f < sqr) {
        float inv = 1.0f / CMath::sqrt(sqr);
        this->x = this->x * inv;
        this->y = inv * this->y;
    }
}
