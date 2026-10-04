#include "world/CWFrustum.hpp"
#include "catch.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include "gx/Transform.hpp"
#include <cmath>

// The world's frustum (WorldScene.cpp / the CWFrustum methods at 0x00983ae0..0x00984240) and the
// corner extraction the scene, the cursor ray and the shadow cascades all build on.

namespace {

// A box from (-1, -1, -1) to (1, 1, 1) in the frustum's own corner order: the near face 0..3 and the
// far face 4..7, each face going (-, -), (-, +), (+, +), (+, -) in x and y.
void UnitBoxCorners(C3Vector* c, float nearZ, float farZ, float halfNear, float halfFar) {
    c[0] = { -halfNear, -halfNear, nearZ };
    c[1] = { -halfNear, halfNear, nearZ };
    c[2] = { halfNear, halfNear, nearZ };
    c[3] = { halfNear, -halfNear, nearZ };
    c[4] = { -halfFar, -halfFar, farZ };
    c[5] = { -halfFar, halfFar, farZ };
    c[6] = { halfFar, halfFar, farZ };
    c[7] = { halfFar, -halfFar, farZ };
}

float PlaneDistance(const C4Plane& p, const C3Vector& v) {
    return p.n.x * v.x + p.n.y * v.y + p.n.z * v.z + p.d;
}

} // namespace

TEST_CASE("CWFrustum planes", "[world][frustum]") {
    C3Vector corners[8];
    UnitBoxCorners(corners, 1.0f, 10.0f, 1.0f, 10.0f);
    CWFrustum frustum(corners);

    C3Vector inside = { 0.0f, 0.0f, 5.0f };

    SECTION("every plane agrees on which side the inside is") {
        float first = PlaneDistance(frustum.planes[0], inside);
        REQUIRE(first != 0.0f);

        for (int32_t i = 1; i < 6; i++) {
            float d = PlaneDistance(frustum.planes[i], inside);
            CHECK((d > 0.0f) == (first > 0.0f));
        }
    }

    SECTION("the near plane is the far plane turned around through a near corner") {
        CHECK(frustum.planes[5].n.x == Approx(-frustum.planes[4].n.x).margin(1e-6));
        CHECK(frustum.planes[5].n.y == Approx(-frustum.planes[4].n.y).margin(1e-6));
        CHECK(frustum.planes[5].n.z == Approx(-frustum.planes[4].n.z).margin(1e-6));
        CHECK(PlaneDistance(frustum.planes[5], corners[2]) == Approx(0.0f).margin(1e-4));
    }

    SECTION("each side plane passes through the corners it is built from") {
        CHECK(PlaneDistance(frustum.planes[0], corners[1]) == Approx(0.0f).margin(1e-4));
        CHECK(PlaneDistance(frustum.planes[0], corners[5]) == Approx(0.0f).margin(1e-4));
        CHECK(PlaneDistance(frustum.planes[0], corners[6]) == Approx(0.0f).margin(1e-4));
        CHECK(PlaneDistance(frustum.planes[4], corners[4]) == Approx(0.0f).margin(1e-4));
    }
}

TEST_CASE("CWFrustum::SphereInside", "[world][frustum]") {
    C3Vector corners[8];
    UnitBoxCorners(corners, 1.0f, 10.0f, 1.0f, 10.0f);
    CWFrustum frustum(corners);

    // Orient the planes so that the inside is non-negative, as the scene does.
    if (PlaneDistance(frustum.planes[0], { 0.0f, 0.0f, 5.0f }) < 0.0f) {
        frustum.NegatePlanes();
    }

    CAaSphere centre;
    centre.c = { 0.0f, 0.0f, 5.0f };
    centre.r = 0.5f;
    CHECK(frustum.SphereInside(centre) != 0);

    CAaSphere behind;
    behind.c = { 0.0f, 0.0f, -50.0f };
    behind.r = 1.0f;
    CHECK(frustum.SphereInside(behind) == 0);

    CAaSphere beside;
    beside.c = { 100.0f, 0.0f, 5.0f };
    beside.r = 1.0f;
    CHECK(frustum.SphereInside(beside) == 0);

    SECTION("a sphere straddling a plane still counts") {
        CAaSphere edge;
        edge.c = { 0.0f, 0.0f, 0.5f };
        edge.r = 1.0f;
        CHECK(frustum.SphereInside(edge) != 0);
    }
}

TEST_CASE("CWFrustum::Translate", "[world][frustum]") {
    C3Vector corners[8];
    UnitBoxCorners(corners, 1.0f, 10.0f, 1.0f, 10.0f);
    CWFrustum frustum(corners);
    CWFrustum moved = frustum;

    C3Vector offset = { 100.0f, -20.0f, 3.0f };
    moved.Translate(offset);

    for (int32_t i = 0; i < 8; i++) {
        CHECK(moved.corners[i].x == Approx(frustum.corners[i].x + offset.x));
        CHECK(moved.corners[i].y == Approx(frustum.corners[i].y + offset.y));
        CHECK(moved.corners[i].z == Approx(frustum.corners[i].z + offset.z));
    }

    // A point keeps its signed distance to every plane when both move together.
    C3Vector point = { 0.25f, -0.5f, 4.0f };
    C3Vector movedPoint = { point.x + offset.x, point.y + offset.y, point.z + offset.z };

    for (int32_t i = 0; i < 6; i++) {
        CHECK(PlaneDistance(moved.planes[i], movedPoint) == Approx(PlaneDistance(frustum.planes[i], point)).margin(1e-3));
    }
}

TEST_CASE("CWFrustum::GetBounds and MirrorCorners", "[world][frustum]") {
    C3Vector corners[8];
    UnitBoxCorners(corners, 1.0f, 10.0f, 1.0f, 10.0f);
    CWFrustum frustum(corners);

    CAaBox bounds;
    frustum.GetBounds(bounds);
    CHECK(bounds.b.x == -10.0f);
    CHECK(bounds.t.x == 10.0f);
    CHECK(bounds.b.z == 1.0f);
    CHECK(bounds.t.z == 10.0f);

    // Mirroring swaps the left and right columns of both faces.
    frustum.MirrorCorners();
    CHECK(frustum.corners[0].x == corners[3].x);
    CHECK(frustum.corners[3].x == corners[0].x);
    CHECK(frustum.corners[1].y == corners[2].y);
    CHECK(frustum.corners[5].x == corners[6].x);
}

TEST_CASE("FrustumCorners", "[world][frustum]") {
    C44Matrix view;

    SECTION("an orthographic projection gives the clip cube back through the identity view") {
        C44Matrix proj;
        C3Vector corners[8];
        FrustumCorners(view, proj, corners);

        CHECK(corners[0].x == Approx(-1.0f));
        CHECK(corners[0].y == Approx(-1.0f));
        CHECK(corners[0].z == Approx(-1.0f));
        CHECK(corners[6].x == Approx(1.0f));
        CHECK(corners[6].y == Approx(1.0f));
        CHECK(corners[6].z == Approx(1.0f));
    }

    SECTION("a perspective projection gives a near face smaller than the far face") {
        // The client's own projection (w = +z): near 1, far 100, a 90 degree field of view.
        float n = 1.0f;
        float f = 100.0f;
        C44Matrix proj;
        GxuXformCreateProjection_Exact(1.5707963705062866f, 1.0f, n, f, proj);

        C3Vector corners[8];
        FrustumCorners(view, proj, corners);

        float nearWidth = std::fabs(corners[2].x - corners[0].x);
        float farWidth = std::fabs(corners[6].x - corners[4].x);

        CHECK(nearWidth == Approx(2.0f).epsilon(1e-3));
        CHECK(farWidth == Approx(200.0f).epsilon(1e-3));
        CHECK(corners[0].z == Approx(n).epsilon(1e-3));
        CHECK(corners[4].z == Approx(f).epsilon(1e-3));

        // The face order: corner 0 is (-, -), 2 is (+, +).
        CHECK(corners[0].x < corners[2].x);
        CHECK(corners[0].y < corners[2].y);
    }
}

TEST_CASE("FrustumUnitBasis", "[world][frustum]") {
    // A box-shaped "frustum": the basis maps its corners onto the unit cube's.
    C3Vector corners[8];
    UnitBoxCorners(corners, 2.0f, 6.0f, 1.0f, 1.0f);
    CWFrustum frustum(corners);

    C44Matrix basis;
    REQUIRE(FrustumUnitBasis(frustum, basis, 1) == 1);

    auto map = [&](C3Vector p) {
        C3Vector out;
        TransformPointInPlace(out, p, basis);
        return p;
    };

    C3Vector origin = map(corners[0]);
    CHECK(origin.x == Approx(0.0f).margin(1e-5));
    CHECK(origin.y == Approx(0.0f).margin(1e-5));
    CHECK(origin.z == Approx(0.0f).margin(1e-5));

    C3Vector across = map(corners[3]);
    CHECK(across.x == Approx(1.0f));
    CHECK(across.y == Approx(0.0f).margin(1e-5));

    C3Vector up = map(corners[1]);
    CHECK(up.y == Approx(1.0f));

    C3Vector deep = map(corners[4]);
    CHECK(deep.z == Approx(1.0f));

    SECTION("a degenerate frustum has no basis") {
        C3Vector flat[8];
        for (auto& c : flat) {
            c = { 1.0f, 1.0f, 1.0f };
        }

        CWFrustum degenerate(flat);
        CHECK(FrustumUnitBasis(degenerate, basis, 1) == 0);
    }
}
