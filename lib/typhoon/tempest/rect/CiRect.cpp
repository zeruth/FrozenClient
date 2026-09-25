#include "tempest/rect/CiRect.hpp"

// ref: FUN_00683a20
CiRect CiRect::Intersection(const CiRect& l, const CiRect& r) {
    CiRect i;

    i.maxX = r.maxX <= l.maxX ? r.maxX : l.maxX;
    i.maxY = r.maxY <= l.maxY ? r.maxY : l.maxY;
    i.minX = l.minX <= r.minX ? r.minX : l.minX;
    i.minY = r.minY < l.minY ? l.minY : r.minY;

    return i;
}
