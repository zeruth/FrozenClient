#include "ui/game/CGCamera.hpp"
#include "catch.hpp"
#include <cmath>

// The split under every polynomial sine and cosine the client draws with (FUN_005fe800, 26
// callers: the camera's easing and shake, the sky dome, the sun's direction). The reference
// truncates -- it sets the x87 rounding control to 0xc00 around its fistp -- and steps down one
// at or below zero; the decompiler prints that as ROUND().

TEST_CASE("CameraSplitFloor", "[ui][camera]") {
    float fraction;
    int32_t whole;

    SECTION("positive values truncate, never round up") {
        CameraSplitFloor(0.7f, &fraction, &whole);
        CHECK(whole == 0);
        CHECK(fraction == Approx(0.7f));

        CameraSplitFloor(2.9f, &fraction, &whole);
        CHECK(whole == 2);
        CHECK(fraction == Approx(0.9f));

        CameraSplitFloor(3.0f, &fraction, &whole);
        CHECK(whole == 3);
        CHECK(fraction == 0.0f);
    }

    SECTION("negative values floor") {
        CameraSplitFloor(-0.25f, &fraction, &whole);
        CHECK(whole == -1);
        CHECK(fraction == Approx(0.75f));

        CameraSplitFloor(-1.6f, &fraction, &whole);
        CHECK(whole == -2);
        CHECK(fraction == Approx(0.4f));
    }

    SECTION("zero and negative integers take the step down too, as the reference does") {
        CameraSplitFloor(0.0f, &fraction, &whole);
        CHECK(whole == -1);
        CHECK(fraction == 1.0f);

        CameraSplitFloor(-2.0f, &fraction, &whole);
        CHECK(whole == -3);
        CHECK(fraction == 1.0f);
    }

    SECTION("the fraction is always in (0, 1] below zero and [0, 1) above") {
        for (int32_t i = -100; i <= 100; i++) {
            float value = static_cast<float>(i) * 0.137f;
            CameraSplitFloor(value, &fraction, &whole);

            CHECK(static_cast<float>(whole) + fraction == Approx(value).margin(1e-5));
            CHECK(fraction >= 0.0f);
            CHECK(fraction <= 1.0f);
        }
    }
}

TEST_CASE("CGCamera::SineEase", "[ui][camera]") {
    // Despite frozen's name, FUN_005fff80 is the polynomial COSINE of t: one at zero, zero at a
    // quarter turn, minus one at a half turn.
    CHECK(CGCamera::SineEase(0.0f) == Approx(1.0f).margin(1e-5));
    CHECK(CGCamera::SineEase(3.14159265f * 0.5f) == Approx(0.0f).margin(1e-5));
    CHECK(CGCamera::SineEase(3.14159265f) == Approx(-1.0f).margin(1e-5));

    // Within a couple of percent of the real cosine across whole turns either way -- which the
    // rounding split broke past every half fraction.
    for (int32_t i = -40; i <= 40; i++) {
        float t = 3.14159265f * static_cast<float>(i) / 20.0f;
        CHECK(CGCamera::SineEase(t) == Approx(std::cos(t)).margin(0.03));
    }
}
