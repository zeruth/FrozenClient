#ifndef TEMPEST_VECTOR_C_2VECTOR_HPP
#define TEMPEST_VECTOR_C_2VECTOR_HPP

class C3Vector;

class C2Vector {
    public:
    // Member variables
    float x = 0.0f;
    float y = 0.0f;

    // Member functions
    C2Vector() = default;
    C2Vector(float x, float y)
        : x(x)
        , y(y) {};
    bool operator==(const C2Vector& v);

    // Take a 3-vector's x and y and drop its z. An overload rather than a copy assignment: the
    // reference's is a distinct function and declaring the copy assignment would make C2Vector
    // non-trivially-copyable for the whole codebase. ref: FUN_004c4df0
    C2Vector& operator=(const C3Vector& v);
};

#endif
