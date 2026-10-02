#ifndef TEMPEST_MATRIX_C_34MATRIX_HPP
#define TEMPEST_MATRIX_C_34MATRIX_HPP

// A 3x3 rotation and a translation row: an affine transform in twelve floats.
class C34Matrix {
    public:
    // Member variables
    float a0 = 1.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float b0 = 0.0f;
    float b1 = 1.0f;
    float b2 = 0.0f;
    float c0 = 0.0f;
    float c1 = 0.0f;
    float c2 = 1.0f;
    float d0 = 0.0f;
    float d1 = 0.0f;
    float d2 = 0.0f;

    // Member functions
    void RotateAroundZ(float angle);
};

C34Matrix operator*(const C34Matrix& l, const C34Matrix& r);

#endif
