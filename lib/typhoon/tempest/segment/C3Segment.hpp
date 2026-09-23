#ifndef TEMPEST_SEGMENT_C_3SEGMENT_HPP
#define TEMPEST_SEGMENT_C_3SEGMENT_HPP

#include "tempest/Vector.hpp"

class C3Segment {
    public:
    // Member variables
    C3Vector start;
    C3Vector end;

    // Member functions
    C3Segment() = default;
    C3Segment(const C3Vector& start, const C3Vector& end);
    void Lerp(C3Vector& out, float t) const;
};

#endif
