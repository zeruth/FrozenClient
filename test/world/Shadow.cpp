#include "world/Shadow.hpp"
#include "world/ShadowMap.hpp"
#include "catch.hpp"
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <cstring>

// The decal projection the blob shadows and footprints draw through (DecalBuildTransforms, the
// two texture-stage matrices of FUN_007e4480's caller) and the shadow map's quality table.

namespace {

C3Vector Apply(const C44Matrix& m, C3Vector p) {
    C3Vector out;
    TransformPointInPlace(out, p, m);
    return p;
}

} // namespace

TEST_CASE("DecalBuildTransforms", "[world][shadow]") {
    CAaBox box;
    box.b = { 10.0f, 20.0f, -2.0f };
    box.t = { 14.0f, 30.0f, 2.0f };

    C44Matrix stage0;
    C44Matrix stage1;
    // Absolute: world-space positions, not camera-relative ones.
    DecalBuildTransforms(stage0, stage1, box, nullptr, 0.25f, 1);

    SECTION("the box centre lands in the middle of the texture") {
        C3Vector uv = Apply(stage0, { 12.0f, 25.0f, 0.0f });
        CHECK(uv.x == Approx(0.5f));
        CHECK(uv.y == Approx(0.5f));
    }

    SECTION("the footprint's four corners land on the texture's four corners") {
        const C3Vector corners[4] = {
            { box.b.x, box.b.y, 0.0f },
            { box.b.x, box.t.y, 0.0f },
            { box.t.x, box.b.y, 0.0f },
            { box.t.x, box.t.y, 0.0f },
        };

        int32_t seen = 0;

        for (const auto& corner : corners) {
            C3Vector uv = Apply(stage0, corner);
            int32_t u = static_cast<int32_t>(uv.x + 0.5f);
            int32_t v = static_cast<int32_t>(uv.y + 0.5f);

            CHECK(uv.x == Approx(static_cast<float>(u)).margin(1e-4));
            CHECK(uv.y == Approx(static_cast<float>(v)).margin(1e-4));
            seen |= 1 << (u * 2 + v);
        }

        CHECK(seen == 0xf);
    }

    SECTION("the second stage ramps one unit across the box's height, offset by the bias") {
        C3Vector bottom = Apply(stage1, { 12.0f, 25.0f, box.b.z });
        C3Vector middle = Apply(stage1, { 12.0f, 25.0f, 0.0f });
        C3Vector top = Apply(stage1, { 12.0f, 25.0f, box.t.z });

        CHECK(middle.x == Approx(0.25f));
        CHECK(top.x - bottom.x == Approx(1.0f));
        CHECK(middle.y == Approx(1.0f));
    }

    SECTION("a box with no footprint leaves the matrices as they were") {
        CAaBox flat;
        flat.b = { 0.0f, 0.0f, 0.0f };
        flat.t = { 0.0f, 5.0f, 1.0f };

        C44Matrix a(2.0f);
        C44Matrix b(3.0f);
        DecalBuildTransforms(a, b, flat, nullptr, 0.0f, 1);

        CHECK(a.a0 == 2.0f);
        CHECK(b.a0 == 3.0f);
    }
}

TEST_CASE("ShadowMapQualityName", "[world][shadow]") {
    CHECK(strncmp(ShadowMapQualityName(0), "[LOWEST]", 8) == 0);
    CHECK(strncmp(ShadowMapQualityName(5), "[VERY HIGH]", 11) == 0);
    CHECK(strncmp(ShadowMapQualityName(6), "[INVALID]", 9) == 0);
    CHECK(strncmp(ShadowMapQualityName(99), "[INVALID]", 9) == 0);
    CHECK(strncmp(ShadowMapQualityName(-1), "[INVALID]", 9) == 0);
}
