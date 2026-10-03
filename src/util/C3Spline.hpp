#ifndef UTIL_C3_SPLINE_HPP
#define UTIL_C3_SPLINE_HPP

#include <common/DataStore.hpp>
#include <storm/Array.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

// The orientation and position of a spline at a point: the tangent (row 0), the horizontal side
// (row 1), the up (row 2) and the position (row 3), each row padded to four floats as the
// reference lays its 4x4 out (FUN_004c43b0 writes rows 0..2 and the position at +0x30).
struct C3SplineFrame {
    C3Vector forward = { 1.0f, 0.0f, 0.0f };
    float pad0 = 0.0f;
    C3Vector side = { 0.0f, 1.0f, 0.0f };
    float pad1 = 0.0f;
    C3Vector up = { 0.0f, 0.0f, 1.0f };
    float pad2 = 0.0f;
    C3Vector position = { 0.0f, 0.0f, 0.0f };
    float pad3 = 1.0f;
};

// The reference's spline base (tempest, 0x004c3680 .. 0x004c4600, which the module table files
// under TextureBlob.cpp by its nearest path string). A list of points, the length of each segment,
// and the total length, evaluated either by its parameter (0 .. 1 spread evenly over the segments)
// or by arc length (0 .. 1 of the way along).
//
// STORAGE DIVERGES: the reference keeps the first 25 points (and segment lengths) inline and the
// rest on the heap, which CBezierSpline reproduces because its offsets identify the class. Nothing
// reads this one's layout from outside, so it keeps one growable array of each.
class C3Spline {
    public:
        // Member variables
        // +0x04: the total arc length, which a moving spline divides by its duration for a speed.
        float m_length = 0.0f;
        TSGrowableArray<C3Vector> m_points;
        TSGrowableArray<float> m_segmentLengths;

        // Virtual member functions (vtable 0x009e2f28 for the Catmull-Rom spline)
        virtual ~C3Spline() = default;
        virtual float Length() const = 0;
        virtual void ComputeSegmentLengths() = 0;
        virtual void EvaluateByLength(float t, C3Vector& out) const = 0;
        virtual void EvaluateByParameter(float t, C3Vector& out) const = 0;
        virtual void TangentByLength(float t, C3Vector& out) const = 0;
        virtual void TangentByParameter(float t, C3Vector& out) const = 0;
        virtual void FrameByLength(float t, C3SplineFrame& out) const = 0;
        virtual void CopyPoints(const C3Vector* points, uint32_t count) = 0;
        virtual uint32_t PointsAfter(float t, C3Vector* out, uint32_t max) const = 0;

        // Member functions
        uint32_t PointCount() const;
        const C3Vector& Point(uint32_t index) const;
        float SegmentLength(uint32_t segment) const;
        void SetPoints(const C3Vector* points, uint32_t count);
        void Evaluate(float t, C3Vector& out, int32_t byLength) const;
        void Tangent(float t, C3Vector& out, int32_t byLength) const;
        void Frame(float t, C3SplineFrame& out, int32_t byLength) const;
        float SumSegmentLengths(uint32_t count) const;
        void SegmentAtLength(float t, uint32_t segmentCount, uint32_t& segment, float& localT) const;
        void BlendCubic(uint32_t first, float t, const float* basis, C3Vector& out) const;
        void BlendQuadratic(uint32_t first, float t, const float* basis, C3Vector& out) const;
        float SampledSegmentLength(uint32_t first, const float* basis) const;
        void GetPoints(C3Vector* out, uint32_t count) const;
};

// A Catmull-Rom curve, or a polyline, through points 1 .. n-2: the first and last points only
// shape the ends. Its segments are count - 3.
class C3Spline_CatmullRom : public C3Spline {
    public:
        // Member variables
        // +0x1c0: 0 a straight polyline between the points, 1 the Catmull-Rom curve.
        int32_t m_splineMode = 1;

        // Virtual member functions
        float Length() const override;
        void ComputeSegmentLengths() override;
        void EvaluateByLength(float t, C3Vector& out) const override;
        void EvaluateByParameter(float t, C3Vector& out) const override;
        void TangentByLength(float t, C3Vector& out) const override;
        void TangentByParameter(float t, C3Vector& out) const override;
        void FrameByLength(float t, C3SplineFrame& out) const override;
        void CopyPoints(const C3Vector* points, uint32_t count) override;
        uint32_t PointsAfter(float t, C3Vector* out, uint32_t max) const override;

        // Member functions
        void EvaluateSegment(uint32_t segment, float t, C3Vector& out) const;
        float MeasureSegment(uint32_t segment) const;
        void SegmentAtParameter(float t, uint32_t& segment, float& localT) const;
};

// TODO move this operator>> to util/DataStore.hpp
CDataStore& operator>>(CDataStore& msg, C3Spline_CatmullRom& spline);

#endif
