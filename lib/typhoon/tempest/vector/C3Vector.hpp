#ifndef TEMPEST_VECTOR_C_3VECTOR_HPP
#define TEMPEST_VECTOR_C_3VECTOR_HPP

#include "tempest/vector/CImVector.hpp"

class C33Matrix;
class C44Matrix;

class C3Vector {
    public:
    // Static functions
    static C3Vector Cross(const C3Vector& l, const C3Vector& r);
    static C3Vector Max(const C3Vector& l, const C3Vector& r);
    static C3Vector Min(const C3Vector& l, const C3Vector& r);

    // Member variables
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    // Member functions
    C3Vector() = default;
    C3Vector(float x, float y, float z)
        : x(x)
        , y(y)
        , z(z) {};
    C3Vector(const CImVector& color)
        : x(color.r / 255.0f)
        , y(color.g / 255.0f)
        , z(color.b / 255.0f) {};
    C3Vector operator-() const;
    C3Vector& operator*=(float a);
    C3Vector& operator+=(const C3Vector& v);
    float Mag() const;
    void Normalize();
    float SquaredMag() const;
};

C3Vector operator+(const C3Vector& l, const C3Vector& r);

C3Vector operator-(const C3Vector& l, const C3Vector& r);

C3Vector operator*(const C3Vector& l, const C33Matrix& r);

C3Vector operator*(const C3Vector& l, const C44Matrix& r);

// Transform a point by a matrix IN PLACE, and copy the result to `out` as well. Distinct from the
// operator above in the reference and kept distinct here; see the definition.
void TransformPointInPlace(C3Vector& out, C3Vector& v, const C44Matrix& m);

// The same over a C33Matrix: rewrites the vector and copies it to `out`.
void TransformInPlace(C3Vector& out, C3Vector& v, const C33Matrix& m);

// Transform a DIRECTION by a matrix: the 3x3 part only, so the translation row does not apply.
// The same nine multiplies as `C3Vector * C33Matrix` above, over a C44Matrix instead -- a
// separate function in the reference because the caller has a C44Matrix in hand and building the
// C33Matrix would cost more than striding over it.
void TransformDirection(C3Vector& out, const C3Vector& v, const C44Matrix& m);

bool operator!=(const C3Vector& l, const C3Vector& r);

// A count and a C3Vector array over storage the caller provides (alloca at the known call site).
struct C3VectorStackArray {
    uint32_t m_count;
    C3Vector* m_data;

    void Init(C3Vector* storage, uint32_t count, int32_t construct);
};

#endif
