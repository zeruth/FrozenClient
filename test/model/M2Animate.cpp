#include "model/M2Animate.hpp"
#include "catch.hpp"
#include <cmath>

// The M2 track value conversions and interpolators. The constants are the reference's: compressed
// quaternions decode each unsigned 16-bit word as w * 3.0518044e-05 - 1 (0x00a45560, read with
// movzwl at 0x008286d2), fixed16 is a signed word over 32767 (0x009ea0b4, movswl at 0x0082af8a),
// and the spline weights are FUN_0082b460 / FUN_0082b8a0's.

namespace {

M2CompQuat Compressed(uint16_t x, uint16_t y, uint16_t z, uint16_t w) {
    M2CompQuat q;
    q.auCompQ[0] = static_cast<uint32_t>(x) | (static_cast<uint32_t>(y) << 16);
    q.auCompQ[1] = static_cast<uint32_t>(z) | (static_cast<uint32_t>(w) << 16);
    return q;
}

template<class T>
M2SplineKey<T> Key(T value, T inTan, T outTan) {
    M2SplineKey<T> key;
    key.value = value;
    key.inTan = inTan;
    key.outTan = outTan;
    return key;
}

} // namespace

TEST_CASE("M2SetValue compressed quaternion", "[model][animation]") {
    C4Quaternion q;

    SECTION("zero decodes to -1 and 0xffff to +1") {
        M2SetValue(Compressed(0, 0xffff, 0, 0xffff), q);
        CHECK(q.x == Approx(-1.0f));
        CHECK(q.y == Approx(1.0f).epsilon(1e-4));
        CHECK(q.z == Approx(-1.0f));
        CHECK(q.w == Approx(1.0f).epsilon(1e-4));
    }

    SECTION("the identity rotation is stored as 0x7fff, 0x7fff, 0x7fff, 0xffff") {
        M2SetValue(Compressed(0x7fff, 0x7fff, 0x7fff, 0xffff), q);
        CHECK(q.x == Approx(0.0f).margin(1e-4));
        CHECK(q.y == Approx(0.0f).margin(1e-4));
        CHECK(q.z == Approx(0.0f).margin(1e-4));
        CHECK(q.w == Approx(1.0f).epsilon(1e-4));
    }

    SECTION("the words are x, y in the first dword and z, w in the second, low half first") {
        M2SetValue(Compressed(0, 0x7fff, 0xffff, 0x7fff), q);
        CHECK(q.x < -0.99f);
        CHECK(std::fabs(q.y) < 1e-3f);
        CHECK(q.z > 0.99f);
        CHECK(std::fabs(q.w) < 1e-3f);
    }
}

TEST_CASE("M2SetValue fixed16", "[model][animation]") {
    float value;

    fixed16 full = { 32767 };
    M2SetValue(full, value);
    CHECK(value == Approx(1.0f));

    fixed16 half = { -16384 };
    M2SetValue(half, value);
    CHECK(value == Approx(-0.5f).epsilon(1e-4));
}

TEST_CASE("M2InterpolateLinear", "[model][animation]") {
    SECTION("vectors at the ends and the middle") {
        C3Vector a = { 0.0f, 10.0f, -4.0f };
        C3Vector b = { 2.0f, 20.0f, 4.0f };
        C3Vector v;

        M2InterpolateLinear(a, b, 0.0f, v);
        CHECK(v.x == 0.0f);
        CHECK(v.y == 10.0f);

        M2InterpolateLinear(a, b, 1.0f, v);
        CHECK(v.z == 4.0f);

        M2InterpolateLinear(a, b, 0.25f, v);
        CHECK(v.x == Approx(0.5f));
        CHECK(v.y == Approx(12.5f));
        CHECK(v.z == Approx(-2.0f));
    }

    SECTION("bytes interpolate and truncate") {
        uint8_t v;
        M2InterpolateLinear(static_cast<uint8_t>(0), static_cast<uint8_t>(255), 0.5f, v);
        CHECK(v == 127);
    }

    SECTION("16-bit tracks hold the key rather than interpolating (FUN_0082bb50)") {
        uint16_t v;
        M2InterpolateLinear(static_cast<uint16_t>(3), static_cast<uint16_t>(9), 0.9f, v);
        CHECK(v == 3);
    }

    SECTION("compressed quaternions blend to a unit quaternion") {
        C4Quaternion v;
        // The identity and a half turn about z.
        M2InterpolateLinear(Compressed(0x7fff, 0x7fff, 0x7fff, 0xffff), Compressed(0x7fff, 0x7fff, 0xffff, 0x7fff), 0.5f, v);

        float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
        CHECK(length == Approx(1.0f).epsilon(1e-3));
        CHECK(v.z == Approx(v.w).epsilon(1e-3));
    }
}

TEST_CASE("M2InterpolateCubicBezier", "[model][animation]") {
    // Track type 2: (1-t)^3 value, 3t(1-t)^2 outTan, 3t^2(1-t) inTan, t^3 value.
    auto start = Key(0.0f, 0.0f, 3.0f);
    auto end = Key(10.0f, 6.0f, 0.0f);
    float v;

    M2InterpolateCubicBezier(start, end, 0.0f, v);
    CHECK(v == Approx(0.0f));

    M2InterpolateCubicBezier(start, end, 1.0f, v);
    CHECK(v == Approx(10.0f));

    M2InterpolateCubicBezier(start, end, 0.5f, v);
    // 0.125*0 + 0.375*3 + 0.375*6 + 0.125*10
    CHECK(v == Approx(4.625f));

    SECTION("the vector form matches the float form per component") {
        auto vs = Key(C3Vector { 0.0f, 1.0f, 2.0f }, C3Vector { 0.0f, 0.0f, 0.0f }, C3Vector { 3.0f, 3.0f, 3.0f });
        auto ve = Key(C3Vector { 10.0f, 11.0f, 12.0f }, C3Vector { 6.0f, 6.0f, 6.0f }, C3Vector { 0.0f, 0.0f, 0.0f });
        C3Vector out;
        M2InterpolateCubicBezier(vs, ve, 0.5f, out);
        CHECK(out.x == Approx(4.625f));
        CHECK(out.y == Approx(4.625f + 0.125f + 0.125f));
    }
}

TEST_CASE("M2InterpolateCubicHermite", "[model][animation]") {
    // Track type 3: h00 value, h10 outTan, h01 value, h11 inTan, the tangents raw.
    auto start = Key(1.0f, 0.0f, 4.0f);
    auto end = Key(5.0f, -2.0f, 0.0f);
    float v;

    M2InterpolateCubicHermite(start, end, 0.0f, v);
    CHECK(v == Approx(1.0f));

    M2InterpolateCubicHermite(start, end, 1.0f, v);
    CHECK(v == Approx(5.0f));

    M2InterpolateCubicHermite(start, end, 0.5f, v);
    // h00 = 0.5, h10 = 0.125, h01 = 0.5, h11 = -0.125
    CHECK(v == Approx(0.5f * 1.0f + 0.125f * 4.0f + 0.5f * 5.0f - 0.125f * -2.0f));

    SECTION("with zero tangents the curve is a smoothstep") {
        auto a = Key(0.0f, 0.0f, 0.0f);
        auto b = Key(1.0f, 0.0f, 0.0f);
        M2InterpolateCubicHermite(a, b, 0.25f, v);
        CHECK(v == Approx(3.0f * 0.0625f - 2.0f * 0.015625f));
    }
}

TEST_CASE("M2BlendValue", "[model][animation]") {
    SECTION("vectors and floats move toward the secondary by the weight") {
        C3Vector v = { 0.0f, 0.0f, 0.0f };
        M2BlendValue(v, C3Vector { 4.0f, 8.0f, -4.0f }, 0.25f);
        CHECK(v.x == Approx(1.0f));
        CHECK(v.y == Approx(2.0f));
        CHECK(v.z == Approx(-1.0f));

        float f = 10.0f;
        M2BlendValue(f, 20.0f, 0.5f);
        CHECK(f == Approx(15.0f));
    }
}
