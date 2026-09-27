#ifndef TEMPEST_SPLINE_C_BEZIER_SPLINE_HPP
#define TEMPEST_SPLINE_C_BEZIER_SPLINE_HPP

#include "tempest/vector/C3Vector.hpp"

#include <cstdint>

// A piecewise CUBIC BEZIER through a list of control points.
//
// Named by behaviour: the reference's own name for the class is not in the binary's strings, but
// what it is admits no doubt. Its evaluator blends four control points with the basis matrix at
// 0x00ac3778, which is
//
//     -1   3  -3   1
//      3  -6   3   0
//     -3   3   0   0
//      1   0   0   0
//
// -- the Bernstein basis, term for term. Its tangent evaluator builds a second matrix whose rows
// are (-3, 6, -3), (9, -12, 3), (-9, 6, 0) and (3, 0, 0), and those are exactly the derivatives of
// the four cubic Bernstein polynomials in (t^2, t, 1):
//
//     B0' = -3(1-t)^2            = -3t^2 +  6t - 3
//     B1' = 3(1-t)^2 - 6t(1-t)   =  9t^2 - 12t + 3
//     B2' = 6t(1-t) - 3t^2       = -9t^2 +  6t
//     B3' = 3t^2                 =  3t^2
//
// Those four were derived by hand before the constants were read out of the binary, and they agree
// term for term, which is a stronger check on the identification than the basis matrix alone.
//
// SEGMENTS ARE CHAINED, THREE POINTS APART. Segment i blends points 3i .. 3i+3, so neighbouring
// segments share an endpoint and the curve is continuous; the count divided by three is the segment
// count. A curve of n segments therefore wants 3n + 1 points.
//
// THE FIRST 25 POINTS LIVE INLINE and the rest on the heap, which is the reference's own storage and
// not a frozen convenience: its evaluator tests the point index against 24 and reaches for the heap
// pointer beyond that, with the heap array indexed from point 25. Reproduced because the offsets
// this produces are what identify the class inside the particle emitter that derives from it -- the
// emitter's deleting destructor frees the heap pointer at its own +0x388, which is this class's
// +0x13c once the emitter's +0x24c base offset is taken off.
class CBezierSpline {
    public:
        // Static constants
        // How many control points sit inside the object before the heap is used.
        static const uint32_t INLINE_POINTS = 25;
        // Points per segment step. Four points make a segment, and the next segment starts three
        // later, on the previous one's last point.
        static const uint32_t POINTS_PER_SEGMENT = 3;

        // Member variables
        // +0x08: the first INLINE_POINTS control points.
        C3Vector m_points[INLINE_POINTS];
        // +0x13c: control points from INLINE_POINTS onwards, indexed from zero.
        C3Vector* m_overflow = nullptr;
        // +0x144: how many control points there are in total, inline and overflow together.
        uint32_t m_pointCount = 0;

        // Member functions
        virtual ~CBezierSpline() {}

        // How many chained cubic segments the control points make up.
        uint32_t SegmentCount() const;

        // One control point, wherever it is stored.
        const C3Vector& Point(uint32_t index) const;

        // The curve at `t`, which runs 0 .. 1 across the whole curve rather than one segment.
        // ref: FUN_004c4880 over FUN_004c39d0
        void Evaluate(float t, C3Vector& out) const;

        // The curve's derivative at `t`. Not normalised -- it carries the segment's own scale, which
        // is what a caller wanting a speed along the curve needs. ref: FUN_004c4930 over FUN_004c3e70
        void Tangent(float t, C3Vector& out) const;

        // Where `t` lands: which segment, and how far along that segment. ref: FUN_004c3e00
        void SegmentAt(float t, uint32_t& segment, float& localT) const;
};

#endif
