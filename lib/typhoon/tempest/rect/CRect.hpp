#ifndef TEMPEST_RECT_C_RECT_HPP
#define TEMPEST_RECT_C_RECT_HPP

class C2Vector;

class CRect {
    public:
    // Static functions
    static CRect Intersection(const CRect& l, const CRect& r);

    // Member variables
    float minY = 0.0f; // t
    float minX = 0.0f; // l
    float maxY = 0.0f; // b
    float maxX = 0.0f; // r

    // Member functions
    CRect() = default;
    CRect(float minY, float minX, float maxY, float maxX)
        : minY(minY)
        , minX(minX)
        , maxY(maxY)
        , maxX(maxX) {};
    // A rect collapsed onto one point. ref: FUN_00978e90
    explicit CRect(const C2Vector& pt);
    bool operator==(const CRect& r);
    // ref: FUN_004f5d90
    bool operator!=(const CRect& r) const;
    bool IsPointInside(const C2Vector& pt);
    bool Sub4826D0() const;
};

#endif
