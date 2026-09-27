#include "tempest/spline/CBezierSpline.hpp"

#include <cmath>
#include <storm/Memory.hpp>

namespace {

// The cubic Bernstein basis, DAT_00ac3778. Row k is control point k's blending polynomial, highest
// power first, so a row evaluates by Horner as ((r0*t + r1)*t + r2)*t + r3.
const float BEZIER_BASIS[4][4] = {
    { -1.0f,  3.0f, -3.0f, 1.0f },
    {  3.0f, -6.0f,  3.0f, 0.0f },
    { -3.0f,  3.0f,  0.0f, 0.0f },
    {  1.0f,  0.0f,  0.0f, 0.0f }
};

// The same four polynomials differentiated, DAT_00b4a308. One degree lower, so three coefficients
// a row and one less Horner step. The reference builds this lazily behind a once-flag; frozen has
// it as data, which is the same table with no initialisation order to get wrong.
const float BEZIER_DERIVATIVE_BASIS[4][3] = {
    { -3.0f,   6.0f, -3.0f },
    {  9.0f, -12.0f,  3.0f },
    { -9.0f,   6.0f,  0.0f },
    {  3.0f,   0.0f,  0.0f }
};

}

CBezierSpline::~CBezierSpline() {
    if (this->m_overflow) {
        SMemFree(this->m_overflow, __FILE__, __LINE__, 0);

        this->m_overflow = nullptr;
    }
}

// ref: FUN_004c4d50, reached through FUN_004c3830
// Copy the control points in, spilling past the inline block onto the heap.
//
// The reference works in SEGMENTS rather than points: it stores count / 3, grows a second array
// when that exceeds 25, and copies segments * 3 + 1 points. For well-formed input the two agree --
// a curve of s chained cubics is authored with exactly 3s + 1 points, so segments * 3 + 1 is the
// count it was handed. Storing the count as given is the same thing without the round trip, and it
// is what this class's own evaluator reads back (it divides by three to get the segments again).
//
// The second array the reference grows alongside the points is NOT reproduced: it is indexed by
// segment and the evaluator never touches it, so what it caches is unestablished. Its pointer is
// the emitter's +0x400, the other of the two the deleting destructor frees.
void CBezierSpline::SetPoints(const C3Vector* points, uint32_t count) {
    if (this->m_overflow) {
        SMemFree(this->m_overflow, __FILE__, __LINE__, 0);

        this->m_overflow = nullptr;
    }

    this->m_pointCount = 0;

    if (!points || !count) {
        return;
    }

    uint32_t inlineCount = count < CBezierSpline::INLINE_POINTS
                         ? count
                         : CBezierSpline::INLINE_POINTS;

    for (uint32_t i = 0; i < inlineCount; i++) {
        this->m_points[i] = points[i];
    }

    if (count > CBezierSpline::INLINE_POINTS) {
        uint32_t spill = count - CBezierSpline::INLINE_POINTS;

        this->m_overflow = static_cast<C3Vector*>(
            SMemAlloc(sizeof(C3Vector) * spill, __FILE__, __LINE__, 0));

        if (!this->m_overflow) {
            this->m_pointCount = inlineCount;

            return;
        }

        for (uint32_t i = 0; i < spill; i++) {
            this->m_overflow[i] = points[CBezierSpline::INLINE_POINTS + i];
        }
    }

    this->m_pointCount = count;
}

uint32_t CBezierSpline::SegmentCount() const {
    return this->m_pointCount / CBezierSpline::POINTS_PER_SEGMENT;
}

const C3Vector& CBezierSpline::Point(uint32_t index) const {
    if (index < CBezierSpline::INLINE_POINTS) {
        return this->m_points[index];
    }

    return this->m_overflow[index - CBezierSpline::INLINE_POINTS];
}

// ref: FUN_004c3e00
// Which segment `t` lands in and where inside it. The segment is floor(segments * t) -- the
// reference reaches that with a round of (segments * t - 0.5), which is the same thing for the
// non-negative t its callers clamp to -- and the local parameter is the remainder, so it runs 0 .. 1
// across the segment however many segments there are.
void CBezierSpline::SegmentAt(float t, uint32_t& segment, float& localT) const {
    float segments = static_cast<float>(this->SegmentCount());

    if (segments <= 0.0f) {
        segment = 0;
        localT = 0.0f;

        return;
    }

    float scaled = segments * t;
    int32_t index = static_cast<int32_t>(nearbyintf(scaled - 0.5f));

    if (index < 0) {
        index = 0;
    }

    segment = static_cast<uint32_t>(index);
    localT = scaled - static_cast<float>(index);
}

// ref: FUN_004c4880 over FUN_004c39d0
void CBezierSpline::Evaluate(float t, C3Vector& out) const {
    out.x = 0.0f;
    out.y = 0.0f;
    out.z = 0.0f;

    if (!this->m_pointCount) {
        return;
    }

    uint32_t segment;
    float localT;

    this->SegmentAt(t, segment, localT);

    uint32_t first = segment * CBezierSpline::POINTS_PER_SEGMENT;

    for (uint32_t k = 0; k < 4; k++) {
        const float* row = BEZIER_BASIS[k];

        float weight = ((row[0] * localT + row[1]) * localT + row[2]) * localT + row[3];

        // A curve whose last segment is short of its fourth point would read past the end. The
        // reference does not check -- its own filler always leaves 3n + 1 points -- so this is
        // FROZEN-ONLY, and it holds the last point rather than inventing one.
        uint32_t index = first + k;

        if (index >= this->m_pointCount) {
            index = this->m_pointCount - 1;
        }

        const C3Vector& point = this->Point(index);

        out.x += weight * point.x;
        out.y += weight * point.y;
        out.z += weight * point.z;
    }
}

// ref: FUN_004c4930 over FUN_004c3e70
void CBezierSpline::Tangent(float t, C3Vector& out) const {
    out.x = 0.0f;
    out.y = 0.0f;
    out.z = 0.0f;

    if (!this->m_pointCount) {
        return;
    }

    uint32_t segment;
    float localT;

    this->SegmentAt(t, segment, localT);

    uint32_t first = segment * CBezierSpline::POINTS_PER_SEGMENT;

    for (uint32_t k = 0; k < 4; k++) {
        const float* row = BEZIER_DERIVATIVE_BASIS[k];

        float weight = (row[0] * localT + row[1]) * localT + row[2];

        uint32_t index = first + k;

        if (index >= this->m_pointCount) {
            index = this->m_pointCount - 1;
        }

        const C3Vector& point = this->Point(index);

        out.x += weight * point.x;
        out.y += weight * point.y;
        out.z += weight * point.z;
    }
}
