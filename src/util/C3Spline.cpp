#include "util/C3Spline.hpp"
#include "util/DataStore.hpp"
#include <cmath>

namespace {

// 0x00ac37b8: the Catmull-Rom basis, four cubic coefficients (t^3, t^2, t, 1) per point.
const float CATMULL_ROM_BASIS[16] = {
    -0.5f,  1.0f, -0.5f, 0.0f,
     1.5f, -2.5f,  0.0f, 1.0f,
    -1.5f,  2.0f,  0.5f, 0.0f,
     0.5f, -0.5f,  0.0f, 0.0f,
};

// 0x00ac37f8, expanded by FUN_004c3680 into rows of four: its derivative, three quadratic
// coefficients (t^2, t, 1) per point.
const float CATMULL_ROM_DERIVATIVE[16] = {
    -1.5f,  2.0f, -0.5f, 0.0f,
     4.5f, -5.0f,  0.0f, 0.0f,
    -4.5f,  4.0f,  0.5f, 0.0f,
     1.5f, -1.0f,  0.0f, 1.0f,
};

const float SPLINE_EPSILON = 2.384185791015625e-07f;   // 0x009ea27c
const float SPLINE_TANGENT_EPSILON = 0.0001f;          // 0x009e8cd0

float Distance(const C3Vector& a, const C3Vector& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;

    return std::sqrt(dy * dy + dz * dz + dx * dx);
}

} // namespace

uint32_t C3Spline::PointCount() const {
    return this->m_points.Count();
}

// ref: FUN_004c36f0
const C3Vector& C3Spline::Point(uint32_t index) const {
    return this->m_points[index];
}

float C3Spline::SegmentLength(uint32_t segment) const {
    return this->m_segmentLengths[segment];
}

// ref: FUN_004c3830
// Take the points, and with at least one segment measure them.
void C3Spline::SetPoints(const C3Vector* points, uint32_t count) {
    this->CopyPoints(points, count);

    if (3 < this->PointCount()) {
        this->ComputeSegmentLengths();
        this->m_length = this->Length();
    }
}

// ref: FUN_004c3870
// The point at `t`: the first point up to 0, the last from 1, and between them by arc length or
// by parameter.
void C3Spline::Evaluate(float t, C3Vector& out, int32_t byLength) const {
    if (!(0.0f < t)) {
        out = this->Point(0);
        return;
    }

    if (1.0f <= t) {
        out = this->Point(this->PointCount() - 1);
        return;
    }

    if (byLength == 0) {
        this->EvaluateByParameter(t, out);
    } else if (byLength == 1) {
        this->EvaluateByLength(t, out);
    }
}

// ref: FUN_004c3920
void C3Spline::Tangent(float t, C3Vector& out, int32_t byLength) const {
    float clamped = 0.0f;

    if (0.0f <= t) {
        clamped = 1.0f <= t ? 1.0f : t;
    }

    if (byLength == 0) {
        this->TangentByParameter(clamped, out);
    } else if (byLength == 1) {
        this->TangentByLength(clamped, out);
    }
}

// ref: FUN_004c3980
void C3Spline::Frame(float t, C3SplineFrame& out, int32_t byLength) const {
    float clamped = 0.0f;

    if (0.0f <= t) {
        clamped = 1.0f <= t ? 1.0f : t;
    }

    if (byLength == 1) {
        this->FrameByLength(clamped, out);
    }
}

// ref: FUN_004c3c80
float C3Spline::SumSegmentLengths(uint32_t count) const {
    float sum = 0.0f;

    for (uint32_t i = 0; i < count; i++) {
        sum += this->SegmentLength(i);
    }

    return sum;
}

// ref: FUN_004c3720
float C3Spline::LengthToPoint(uint32_t point) const {
    float sum = 0.0f;

    if (point == 0) {
        return sum;
    }

    for (uint32_t i = 0; i < point - 1; i++) {
        sum += this->SegmentLength(i);
    }

    return sum;
}

// ref: FUN_004c3bd0
// Which segment `t` of the way along lies in, and how far into it.
void C3Spline::SegmentAtLength(float t, uint32_t segmentCount, uint32_t& segment, float& localT) const {
    if (segmentCount < 2) {
        segment = 0;
        localT = t;
        return;
    }

    float target = this->m_length * t;
    float covered = 0.0f;
    segment = 0;

    for (;;) {
        float next = this->SegmentLength(segment) + covered;

        if (target < next) {
            break;
        }

        segment++;
        covered = next;

        if (!(segment < segmentCount - 1)) {
            break;
        }
    }

    localT = (target - covered) / this->SegmentLength(segment);
}

// ref: FUN_004c39d0
// Four points from `first`, weighted by a cubic basis.
void C3Spline::BlendCubic(uint32_t first, float t, const float* basis, C3Vector& out) const {
    out = { 0.0f, 0.0f, 0.0f };

    for (uint32_t k = 0; k < 4; k++) {
        const float* b = basis + k * 4;
        float w = ((b[0] * t + b[1]) * t + b[2]) * t + b[3];
        const C3Vector& p = this->Point(first + k);

        out.x = out.x + w * p.x;
        out.y = p.y * w + out.y;
        out.z = p.z * w + out.z;
    }
}

// ref: FUN_004c3a70
// The same with a quadratic basis (a derivative's).
void C3Spline::BlendQuadratic(uint32_t first, float t, const float* basis, C3Vector& out) const {
    out = { 0.0f, 0.0f, 0.0f };

    for (uint32_t k = 0; k < 4; k++) {
        const float* b = basis + k * 4;
        float w = (b[0] * t + b[1]) * t + b[2];
        const C3Vector& p = this->Point(first + k);

        out.x = out.x + w * p.x;
        out.y = p.y * w + out.y;
        out.z = p.z * w + out.z;
    }
}

// ref: FUN_004c3b10
// A curved segment's length, from twenty chords.
float C3Spline::SampledSegmentLength(uint32_t first, const float* basis) const {
    float t = 0.05f;
    float length = 0.0f;
    C3Vector previous;
    this->BlendCubic(first, 0.0f, basis, previous);

    for (int32_t i = 0; i < 20; i++) {
        C3Vector current;
        this->BlendCubic(first, t, basis, current);

        length = Distance(previous, current) + length;
        previous = current;
        t += 0.05f;
    }

    return length;
}

// ref: FUN_004c3d80
void C3Spline::GetPoints(C3Vector* out, uint32_t count) const {
    uint32_t n = count < this->PointCount() ? count : this->PointCount();

    for (uint32_t i = 0; i < n; i++) {
        out[i] = this->Point(i);
    }
}

// ref: FUN_004c41b0
float C3Spline_CatmullRom::Length() const {
    return this->SumSegmentLengths(this->PointCount() - 3);
}

// ref: FUN_004c41c0
void C3Spline_CatmullRom::ComputeSegmentLengths() {
    uint32_t count = this->PointCount();

    if (count == 3) {
        return;
    }

    for (uint32_t i = 0; i < count - 3; i++) {
        this->m_segmentLengths[i] = this->MeasureSegment(i);
    }
}

// ref: FUN_004c3fd0
// A point of one segment: straight between its two inner points, or on the curve.
void C3Spline_CatmullRom::EvaluateSegment(uint32_t segment, float t, C3Vector& out) const {
    if (this->m_splineMode == 0) {
        const C3Vector& a = this->Point(segment + 1);
        const C3Vector& b = this->Point(segment + 2);

        out = {
            a.x + (b.x - a.x) * t,
            (b.y - a.y) * t + a.y,
            t * (b.z - a.z) + a.z,
        };

        return;
    }

    this->BlendCubic(segment, t, CATMULL_ROM_BASIS, out);
}

// ref: FUN_004c40b0
float C3Spline_CatmullRom::MeasureSegment(uint32_t segment) const {
    if (this->m_splineMode == 0) {
        return Distance(this->Point(segment + 1), this->Point(segment + 2));
    }

    return this->SampledSegmentLength(segment, CATMULL_ROM_BASIS);
}

// ref: FUN_004c4140
// Which segment parameter `t` lies in, the segments spread evenly over 0 .. 1.
void C3Spline_CatmullRom::SegmentAtParameter(float t, uint32_t& segment, float& localT) const {
    float n = static_cast<float>(this->PointCount() - 3);
    int32_t index = static_cast<int32_t>(std::nearbyint(n * t - 0.5f));

    segment = static_cast<uint32_t>(index);
    localT = n * (t - static_cast<float>(index) * (1.0f / n));
}

// ref: FUN_004c4230
void C3Spline_CatmullRom::EvaluateByLength(float t, C3Vector& out) const {
    uint32_t segment;
    float localT;
    this->SegmentAtLength(t, this->PointCount() - 3, segment, localT);
    this->EvaluateSegment(segment, localT, out);
}

// ref: FUN_004c4280
void C3Spline_CatmullRom::EvaluateByParameter(float t, C3Vector& out) const {
    uint32_t segment;
    float localT;
    this->SegmentAtParameter(t, segment, localT);
    this->EvaluateSegment(segment, localT, out);
}

// ref: FUN_004c42c0
void C3Spline_CatmullRom::TangentByLength(float t, C3Vector& out) const {
    float clamped = 0.0f;

    if (0.0f <= t) {
        clamped = 1.0f <= t ? 1.0f : t;
    }

    uint32_t segment;
    float localT;
    this->SegmentAtLength(clamped, this->PointCount() - 3, segment, localT);
    this->BlendQuadratic(segment, localT, CATMULL_ROM_DERIVATIVE, out);
}

// ref: FUN_004c4340
void C3Spline_CatmullRom::TangentByParameter(float t, C3Vector& out) const {
    float clamped = 0.0f;

    if (0.0f <= t) {
        clamped = 1.0f <= t ? 1.0f : t;
    }

    uint32_t segment;
    float localT;
    this->SegmentAtParameter(clamped, segment, localT);
    this->BlendQuadratic(segment, localT, CATMULL_ROM_DERIVATIVE, out);
}

// ref: FUN_004c43b0
// The frame at `t` of the way along: the position, a forward along the segment (the curve's own
// tangent when it agrees with the segment within 60 degrees), a horizontal side and the up.
void C3Spline_CatmullRom::FrameByLength(float t, C3SplineFrame& out) const {
    uint32_t segment;
    float localT;
    this->SegmentAtLength(t, this->PointCount() - 3, segment, localT);
    this->EvaluateSegment(segment, localT, out.position);

    const C3Vector& a = this->Point(segment + 1);
    const C3Vector& b = this->Point(segment + 2);
    C3Vector chord = { b.x - a.x, b.y - a.y, b.z - a.z };
    float lengthSq = chord.x * chord.x + chord.y * chord.y + chord.z * chord.z;

    if (SPLINE_EPSILON < lengthSq) {
        float inv = 1.0f / std::sqrt(lengthSq);
        chord = { inv * chord.x, chord.y * inv, chord.z * inv };
    }

    if (this->m_splineMode == 0) {
        out.forward = chord;
    } else {
        C3Vector tangent;
        this->BlendQuadratic(segment, localT, CATMULL_ROM_DERIVATIVE, tangent);
        float tangentSq = tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z;

        if (SPLINE_TANGENT_EPSILON < tangentSq) {
            float inv = 1.0f / std::sqrt(tangentSq);
            C3Vector unit = { inv * tangent.x, tangent.y * inv, tangent.z * inv };

            out.forward = 0.5f <= unit.x * chord.x + unit.y * chord.y + unit.z * chord.z ? unit : chord;
        }
    }

    out.side = { -out.forward.y, out.forward.x, 0.0f };
    float sideSq = out.side.x * out.side.x + out.side.y * out.side.y;

    if (SPLINE_EPSILON < sideSq) {
        float inv = 1.0f / std::sqrt(sideSq);
        out.side.x = inv * out.side.x;
        out.side.y = inv * out.side.y;
    }

    out.up = {
        -(out.forward.z * out.side.y),
        out.forward.z * out.side.x,
        out.forward.x * out.side.y - out.forward.y * out.side.x,
    };
}

// ref: FUN_004c4da0
// The points, and room for a length per segment.
void C3Spline_CatmullRom::CopyPoints(const C3Vector* points, uint32_t count) {
    this->m_segmentLengths.SetCount(count ? count - 3 : 0);
    this->m_points.SetCount(count);

    for (uint32_t i = 0; i < count; i++) {
        this->m_points[i] = points[i];
    }
}

// ref: FUN_004c4600
// The points still ahead at `t` of the way along: from the far end of the segment `t` is in.
uint32_t C3Spline_CatmullRom::PointsAfter(float t, C3Vector* out, uint32_t max) const {
    uint32_t count = this->PointCount();
    uint32_t segment;
    float localT;
    this->SegmentAtLength(t, count - 3, segment, localT);

    uint32_t remaining = count - segment - 3;

    if (out) {
        if (max <= remaining) {
            remaining = max;
        }

        for (uint32_t i = 0; i < remaining; i++) {
            out[i] = this->Point(segment + 2 + i);
        }
    }

    return remaining;
}

CDataStore& operator>>(CDataStore& msg, C3Spline_CatmullRom& spline) {
    uint32_t pointCount = 0;
    msg.Get(pointCount);

    void* points;
    msg.GetDataInSitu(points, sizeof(C3Vector) * pointCount);

    uint8_t splineMode;
    msg.Get(splineMode);
    spline.m_splineMode = splineMode;

    if (pointCount && msg.IsValid()) {
        spline.SetPoints(static_cast<const C3Vector*>(points), pointCount);
    }

    return msg;
}
