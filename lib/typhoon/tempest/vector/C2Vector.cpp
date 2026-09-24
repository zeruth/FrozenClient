#include "tempest/vector/C2Vector.hpp"
#include "tempest/vector/C3Vector.hpp"

// ref: FUN_004c4df0
C2Vector& C2Vector::operator=(const C3Vector& v) {
    this->x = v.x;
    this->y = v.y;

    return *this;
}

bool C2Vector::operator==(const C2Vector& v) {
    return this->x == v.x && this->y == v.y;
}
