#ifndef TEMPEST_QUATERNION_C_4QUATERNION_HPP
#define TEMPEST_QUATERNION_C_4QUATERNION_HPP

class C4Quaternion {
    public:
    // Static functions
    static C4Quaternion Nlerp(float ratio, const C4Quaternion& q1, const C4Quaternion& q2);
    // True spherical interpolation, taking the SHORTEST arc. Nlerp above is what the M2
    // key interpolation uses; this is what the blend between two sequences uses, and the
    // difference shows on a wide blend.
    static C4Quaternion Slerp(float ratio, const C4Quaternion& q1, const C4Quaternion& q2);

    // Member variables
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    // Member functions
    C4Quaternion() = default;
    C4Quaternion(float x, float y, float z, float w)
        : x(x)
        , y(y)
        , z(z)
        , w(w) {};
};

// The Hamilton product, `a * b`. See the definition for why the reference's disassembly had to be
// checked against the formula rather than transcribed.
C4Quaternion operator*(const C4Quaternion& a, const C4Quaternion& b);

#endif
