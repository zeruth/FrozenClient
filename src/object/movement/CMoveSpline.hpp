#ifndef OBJECT_MOVEMENT_C_MOVE_SPLINE_HPP
#define OBJECT_MOVEMENT_C_MOVE_SPLINE_HPP

#include "util/C3Spline.hpp"
#include "util/GUID.hpp"
#include <common/DataStore.hpp>
#include <tempest/Vector.hpp>

struct CMoveSpline {
    // TODO
    union {
        C3Vector spot = {};
        WOWGUID guid;
        float facing;
    } face;
    uint32_t flags = 0;             // ref +0x20
    uint32_t start = 0;             // ref +0x24, when the spline started
    // TODO
    uint32_t uint20 = 0;
    uint32_t uint24 = 0;
    uint32_t uint28 = 0;  // the time along the spline
    uint32_t uint2C = 0;            // ref +0x2c, its duration
    uint32_t uint30 = 0;            // ref +0x30, the server's id for it
    C3Spline_CatmullRom spline;
    C3Vector vector1F8 = {};        // ref +0x1f8, where it ends
    float float204 = 1.0f;          // ref +0x204, the duration's stretch in force
    float float208 = 1.0f;          // ref +0x208, the stretch for the next lap
    float float20C = 0.0f;          // ref +0x20c, a parabola's vertical acceleration
    uint32_t uint210 = 0;           // ref +0x210, when its arc or animation starts
    // TODO

    static void Skip(CDataStore* msg);
};

CDataStore& operator>>(CDataStore& msg, CMoveSpline& spline);

#endif
