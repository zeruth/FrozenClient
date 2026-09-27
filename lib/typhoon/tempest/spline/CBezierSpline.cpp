#include "tempest/spline/CBezierSpline.hpp"

#include <cmath>

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
