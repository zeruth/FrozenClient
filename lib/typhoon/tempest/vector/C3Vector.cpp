#include "tempest/vector/C3Vector.hpp"
#include "tempest/Math.hpp"
#include "tempest/Matrix.hpp"
#include <new>

C3Vector C3Vector::operator-() const {
    return { -this->x, -this->y, -this->z };
}

C3Vector& C3Vector::operator*=(float a) {
    this->x *= a;
    this->y *= a;
    this->z *= a;

    return *this;
}

// ref: FUN_00482970
C3Vector& C3Vector::operator+=(const C3Vector& v) {
    this->x = v.x + this->x;
    this->y = v.y + this->y;
    this->z = v.z + this->z;

    return *this;
}

C3Vector C3Vector::Cross(const C3Vector& l, const C3Vector& r) {
    return {
        (l.y * r.z) - (l.z * r.y),
        (l.z * r.x) - (l.x * r.z),
        (l.x * r.y) - (l.y * r.x)
    };
}

// ref: FUN_00714d70
C3Vector C3Vector::Max(const C3Vector& l, const C3Vector& r) {
    float z = l.z <= r.z ? r.z : l.z;
    float y = l.y <= r.y ? r.y : l.y;

    if (r.x < l.x) {
        return { l.x, y, z };
    }

    return { r.x, y, z };
}

// ref: FUN_00714d10
C3Vector C3Vector::Min(const C3Vector& l, const C3Vector& r) {
    float z = r.z <= l.z ? r.z : l.z;
    float y = r.y <= l.y ? r.y : l.y;

    if (l.x < r.x) {
        return { l.x, y, z };
    }

    return { r.x, y, z };
}

float C3Vector::Mag() const {
    return CMath::sqrt(this->SquaredMag());
}

// ref: FUN_004c3600
void C3Vector::Normalize() {
    // A vector shorter than the epsilon is left alone rather than blown up
    float sqr = this->x * this->x + this->y * this->y + this->z * this->z;

    if (0.00000023841858f < sqr) {
        float inv = 1.0f / CMath::sqrt(sqr);
        this->x = this->x * inv;
        this->y = inv * this->y;
        this->z = inv * this->z;
    }
}

float C3Vector::SquaredMag() const {
    return this->x * this->x + this->y * this->y + this->z * this->z;
}

// ref: FUN_00407990
C3Vector operator+(const C3Vector& l, const C3Vector& r) {
    float x = l.x + r.x;
    float y = l.y + r.y;
    float z = l.z + r.z;

    return { x, y, z };
}

// ref: FUN_004829a0
C3Vector operator-(const C3Vector& l, const C3Vector& r) {
    float x = l.x - r.x;
    float y = l.y - r.y;
    float z = l.z - r.z;

    return { x, y, z };
}

// ref: FUN_0057c2e0
void TransformDirection(C3Vector& out, const C3Vector& v, const C44Matrix& m) {
    out.x = v.x * m.a0 + v.y * m.b0 + v.z * m.c0;
    out.y = v.x * m.a1 + v.y * m.b1 + v.z * m.c1;
    out.z = v.x * m.a2 + v.y * m.b2 + v.z * m.c2;
}

// ref: FUN_005fed20
// Summed in the reference's order (x, then z, then y for the first row; z, x, y for the others),
// which differs from the natural one in the last bit.
C3Vector operator*(const C3Vector& l, const C33Matrix& r) {
    float x = l.x * r.a0 + r.c0 * l.z + r.b0 * l.y;
    float y = r.c1 * l.z + r.a1 * l.x + r.b1 * l.y;
    float z = r.c2 * l.z + r.a2 * l.x + r.b2 * l.y;

    return { x, y, z };
}

// ref: FUN_004c21b0
C3Vector operator*(const C3Vector& l, const C44Matrix& r) {
    float x = l.x * r.a0 + l.y * r.b0 + l.z * r.c0 + r.d0;
    float y = l.x * r.a1 + l.y * r.b1 + l.z * r.c1 + r.d1;
    float z = l.x * r.a2 + l.y * r.b2 + l.z * r.c2 + r.d2;

    return { x, y, z };
}

// The same arithmetic as operator* above, but applied IN PLACE and copied out as well, which is
// why the reference keeps it as a separate function (0x004c2300) rather than reusing 0x004c21b0.
// 64 call sites depend on the in-place half: they pass a vector they intend to see modified.
//
// Keeping both is deliberate. Folding this into operator* would mean either losing the mutation
// that those callers rely on, or giving operator* a side effect nothing expects.
// ref: FUN_004c2300
void TransformPointInPlace(C3Vector& out, C3Vector& v, const C44Matrix& m) {
    float x = v.x * m.a0 + m.b0 * v.y + m.c0 * v.z + m.d0;
    float y = m.a1 * v.x + m.b1 * v.y + m.c1 * v.z + m.d1;
    float z = m.a2 * v.x + m.b2 * v.y + m.c2 * v.z + m.d2;

    v.x = x;
    v.y = y;
    v.z = z;

    out = v;
}

// The 3x3 counterpart of TransformPointInPlace: the vector is rewritten and copied out.
// ref: FUN_0050f520
void TransformInPlace(C3Vector& out, C3Vector& v, const C33Matrix& m) {
    float x = v.x * m.a0 + m.b0 * v.y + m.c0 * v.z;
    float y = m.a1 * v.x + m.b1 * v.y + m.c1 * v.z;
    float z = m.a2 * v.x + m.b2 * v.y + m.c2 * v.z;

    v.x = x;
    v.y = y;
    v.z = z;

    out = v;
}

// ref: FUN_004bf540
bool operator!=(const C3Vector& l, const C3Vector& r) {
    return l.x != r.x || l.y != r.y || l.z != r.z;
}

// ref: FUN_00408270
void C3VectorStackArray::Init(C3Vector* storage, uint32_t count, int32_t construct) {
    this->m_data = storage;

    if (!construct) {
        this->m_count = 0;
        return;
    }

    this->m_count = count;

    for (uint32_t i = 0; i < count; i++) {
        new (&this->m_data[i]) C3Vector();
    }
}
