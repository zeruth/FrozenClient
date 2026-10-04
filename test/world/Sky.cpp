#include "world/DayNightLight.hpp"
#include "catch.hpp"
#include <cmath>

// The day-night sky's geometry and the outdoor light's direction. The sun direction is the
// finding recorded in docs/ref/parity-sky.md: not an arc and not from Light.dbc -- FUN_007eea90
// holds the azimuth at 225 degrees and moves the zenith angle between 127 and 110 degrees twice a
// day, and the vector points AWAY from the light.
//
// Both go through the client's polynomial cosine of x * pi (1 - (6 - 4f) f^2 on the fraction of a
// truncating split, sign flipped on odd half turns), which is only within a few percent of the real
// cosine. So the expectations below are that polynomial, written out independently here, rather
// than the exact trigonometry.

namespace {

const float PI = 3.14159265f;
const float DEG = PI / 180.0f;

// The reference's split (FUN_005fe800): truncate, minus one at or below zero.
float RefCosPi(float x) {
    int32_t whole = 0.0f < x ? static_cast<int32_t>(x) : static_cast<int32_t>(x) - 1;
    float f = x - static_cast<float>(whole);
    float v = 1.0f - (6.0f - 4.0f * f) * f * f;
    return (whole & 1) ? -v : v;
}

} // namespace

TEST_CASE("InterpBodyBand", "[world][sky]") {
    // Four keys at quarter days, alternating between two values.
    const float keys[8] = { 0.0f, 10.0f, 0.25f, 20.0f, 0.5f, 10.0f, 0.75f, 20.0f };

    SECTION("a key's own time gives its value") {
        CHECK(InterpBodyBand(keys, 4, 0.25f) == Approx(20.0f));
        CHECK(InterpBodyBand(keys, 4, 0.5f) == Approx(10.0f));
    }

    SECTION("between keys the value is linear") {
        CHECK(InterpBodyBand(keys, 4, 0.125f) == Approx(15.0f));
        CHECK(InterpBodyBand(keys, 4, 0.375f) == Approx(15.0f));
    }

    SECTION("the band wraps from the last key back to the first across midnight") {
        CHECK(InterpBodyBand(keys, 4, 0.875f) == Approx(15.0f));
    }

    SECTION("the time is clamped into the day") {
        CHECK(InterpBodyBand(keys, 4, -2.0f) == Approx(InterpBodyBand(keys, 4, 0.0f)));
    }
}

TEST_CASE("DNUpdateDirection", "[world][sky]") {
    // The sun's bands (0x00af4ac8 / 0x00af4ae8): theta 127 / 110 / 127 / 110 degrees at the quarter
    // days, phi 225 degrees throughout.
    const float thetaKeys[8] = { 0.0f, 2.2165682f, 0.25f, 1.9198622f, 0.5f, 2.2165682f, 0.75f, 1.9198622f };
    const float phi = 3.926991f;

    auto block = DayNightGetBlock();

    for (int32_t step = 0; step <= 96; step++) {
        float time = static_cast<float>(step) / 96.0f;
        block->timeOfDay = time;
        DNUpdateDirection();

        const C3Vector& d = block->direction;

        float theta = InterpBodyBand(thetaKeys, 4, time);
        float sinTheta = RefCosPi(theta / PI - 0.5f);
        float cosTheta = RefCosPi(theta / PI);
        float sinPhi = RefCosPi(phi / PI - 0.5f);
        float cosPhi = RefCosPi(phi / PI);

        CHECK(d.x == Approx(cosPhi * sinTheta).margin(1e-5));
        CHECK(d.y == Approx(sinTheta * sinPhi).margin(1e-5));
        CHECK(d.z == Approx(cosTheta).margin(1e-5));

        // The polynomial's sine and cosine of 225 degrees agree, so the azimuth is exactly 225.
        float azimuth = std::atan2(d.y, d.x);
        if (azimuth < 0.0f) {
            azimuth += 2.0f * PI;
        }
        CHECK(azimuth == Approx(225.0f * DEG).epsilon(1e-5));

        // Close to unit length, and always pointing down: away from a sun above the horizon.
        CHECK(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) == Approx(1.0f).epsilon(0.05));
        CHECK(d.z < 0.0f);
    }

    SECTION("the zenith angle reaches its two extremes at the quarter days") {
        block->timeOfDay = 0.0f;
        DNUpdateDirection();
        float atMidnight = block->direction.z;

        block->timeOfDay = 0.25f;
        DNUpdateDirection();
        float atMorning = block->direction.z;

        // 127 degrees from the zenith is lower in the sky than 110: the vector points less down.
        CHECK(atMidnight == Approx(RefCosPi(127.0f / 180.0f)).margin(1e-4));
        CHECK(atMorning == Approx(RefCosPi(110.0f / 180.0f)).margin(1e-4));
        CHECK(atMidnight < atMorning);
    }
}

TEST_CASE("DomeBuild", "[world][sky]") {
    static DNDome dome;
    DomeBuild(&dome, 1.0f);

    // The ring angles as fractions of a half turn (0x00af4c68).
    const float rings[7] = { 0.0f, 0.17f, 0.2f, 0.23f, 0.24f, 0.25f, 1.0f };
    const float sink = std::cos(0.7853981633974483);

    SECTION("seven rings: a zenith point, five rings of 24, a nadir point") {
        CHECK(dome.segments == 24);
        CHECK(dome.vertexCount == 1 + 5 * 24 + 1);
        CHECK(dome.indexCount == 300);
    }

    SECTION("each ring sits at the polynomial cosine of its angle, sunk by cos(45 degrees)") {
        CHECK(dome.verts[0].z == Approx(RefCosPi(0.0f) - sink).margin(1e-5));
        CHECK(dome.verts[0].x == Approx(0.0f).margin(1e-5));

        for (int32_t ring = 1; ring < 6; ring++) {
            uint32_t first = 1 + (ring - 1) * 24;
            CHECK(dome.verts[first].z == Approx(RefCosPi(rings[ring]) - sink).margin(1e-5));

            // Vertex 0 of a ring is at angle 0 around it: on the +y axis.
            CHECK(dome.verts[first].x == Approx(0.0f).margin(1e-5));
            CHECK(dome.verts[first].y == Approx(RefCosPi(rings[ring] - 0.5f)).margin(1e-5));
        }

        CHECK(dome.verts[dome.vertexCount - 1].z == Approx(RefCosPi(1.0f) - sink).margin(1e-5));
    }

    SECTION("the 45 degree ring (0.25 of a half turn) sits on the horizon, give or take the polynomial") {
        CHECK(dome.verts[1 + 4 * 24].z == Approx(0.0f).margin(0.03));
    }

    SECTION("the vertices stay close to the unit sphere around (0, 0, -sink)") {
        for (uint32_t i = 0; i < dome.vertexCount; i++) {
            const C3Vector& v = dome.verts[i];
            float z = v.z + sink;
            CHECK(std::sqrt(v.x * v.x + v.y * v.y + z * z) == Approx(1.0f).epsilon(0.05));
        }
    }

    SECTION("the strip indices stay inside the vertex array") {
        for (uint32_t i = 0; i < dome.indexCount; i++) {
            CHECK(dome.indices[i] < dome.vertexCount);
        }
    }
}
