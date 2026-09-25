#ifndef TEMPEST_RECT_C_IRECT_HPP
#define TEMPEST_RECT_C_IRECT_HPP

#include <cstdint>

class CiRect {
    public:
    // Static functions
    static CiRect Intersection(const CiRect& l, const CiRect& r);

    // Member variables
    int32_t minY;
    int32_t minX;
    int32_t maxY;
    int32_t maxX;
};

#endif
