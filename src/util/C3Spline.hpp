#ifndef UTIL_C3_SPLINE_HPP
#define UTIL_C3_SPLINE_HPP

#include <common/DataStore.hpp>

// TODO move these classes to typhoon
class C3Spline {
    public:
        // TODO
        // Total arc length of the spline, named by behaviour: the reference divides it by the
        // spline duration to get the traversal speed. It reads the value at CMoveSpline +0x38,
        // which falls +0x4 into the embedded spline object; that mapping is inferred from the
        // offset alone, not from a decompiled accessor. TODO nothing populates this yet, so
        // CMovementShared::GetCurrentSpeed reports 0 for a unit moving along a spline.
        float m_length = 0.0f;
        // TODO
};

class C3Spline_CatmullRom : public C3Spline {
    public:
        // TODO
};

// TODO move this operator>> to util/DataStore.hpp
CDataStore& operator>>(CDataStore& msg, C3Spline_CatmullRom& spline);

#endif
