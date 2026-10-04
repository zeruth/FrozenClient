#include "world/CWFrustum.hpp"
#include "world/CWorldScene.hpp"
#include "world/map/MapOcclusion.hpp"
#include "catch.hpp"

// The polygon clipper the shadow cascades and the occluders use (ClipToUnitCube, FUN_00791380)
// and the occluder test the scene hides distant objects with (FUN_007ccfa0).

namespace {

bool InUnitCube(const C3Vector& p) {
    const float eps = 1e-5f;
    return p.x >= -eps && p.x <= 1.0f + eps && p.y >= -eps && p.y <= 1.0f + eps && p.z >= -eps && p.z <= 1.0f + eps;
}

C4Plane Plane(float nx, float ny, float nz, float d) {
    C4Plane p;
    p.n = { nx, ny, nz };
    p.d = d;
    return p;
}

} // namespace

TEST_CASE("ClipToUnitCube", "[world][frustum]") {
    const C3Vector* const* out = nullptr;
    uint32_t outCount = 0;

    SECTION("a polygon wholly inside comes back as it is") {
        const C3Vector tri[3] = { { 0.1f, 0.1f, 0.5f }, { 0.9f, 0.1f, 0.5f }, { 0.5f, 0.9f, 0.5f } };

        REQUIRE(ClipToUnitCube(tri, 3, &out, &outCount) == 1);
        REQUIRE(outCount == 3);
        CHECK(out[0] == &tri[0]);
        CHECK(out[2] == &tri[2]);
    }

    SECTION("a polygon wholly outside one face is rejected") {
        const C3Vector tri[3] = { { 2.0f, 0.1f, 0.5f }, { 3.0f, 0.1f, 0.5f }, { 2.5f, 0.9f, 0.5f } };
        CHECK(ClipToUnitCube(tri, 3, &out, &outCount) == 0);
    }

    SECTION("a polygon crossing a face is cut at it") {
        // A square from x = 0.5 to 1.5: the part past x = 1 is removed.
        const C3Vector quad[4] = {
            { 0.5f, 0.2f, 0.5f },
            { 1.5f, 0.2f, 0.5f },
            { 1.5f, 0.8f, 0.5f },
            { 0.5f, 0.8f, 0.5f },
        };

        REQUIRE(ClipToUnitCube(quad, 4, &out, &outCount) == 1);
        CHECK(outCount == 4);

        float maxX = 0.0f;

        for (uint32_t i = 0; i < outCount; i++) {
            CHECK(InUnitCube(*out[i]));
            maxX = out[i]->x > maxX ? out[i]->x : maxX;
        }

        CHECK(maxX == Approx(1.0f));
    }

    SECTION("a polygon larger than the cube is cut on every face it crosses") {
        const C3Vector quad[4] = {
            { -1.0f, -1.0f, 0.5f },
            { 2.0f, -1.0f, 0.5f },
            { 2.0f, 2.0f, 0.5f },
            { -1.0f, 2.0f, 0.5f },
        };

        REQUIRE(ClipToUnitCube(quad, 4, &out, &outCount) == 1);
        CHECK(outCount == 4);

        for (uint32_t i = 0; i < outCount; i++) {
            CHECK(InUnitCube(*out[i]));
        }
    }

    SECTION("no points is nothing") {
        CHECK(ClipToUnitCube(nullptr, 0, &out, &outCount) == 0);
    }
}

TEST_CASE("MapOcclusion::PolygonOccluded", "[world][occlusion]") {
    MapOcclusion::ClearVolumes();

    SECTION("with no volumes nothing is occluded") {
        const C3Vector p[1] = { { 0.0f, 0.0f, 0.0f } };
        CHECK(MapOcclusion::PolygonOccluded(p, 1) == 0);
    }

    SECTION("a polygon is occluded only when every point is behind every plane of one volume") {
        // A box from -1 to 1: inside is where every plane is non-positive.
        auto& planes = CWorldScene::s_occlusionPlanes;
        planes.SetCount(6);
        planes[0] = Plane(1.0f, 0.0f, 0.0f, -1.0f);
        planes[1] = Plane(-1.0f, 0.0f, 0.0f, -1.0f);
        planes[2] = Plane(0.0f, 1.0f, 0.0f, -1.0f);
        planes[3] = Plane(0.0f, -1.0f, 0.0f, -1.0f);
        planes[4] = Plane(0.0f, 0.0f, 1.0f, -1.0f);
        planes[5] = Plane(0.0f, 0.0f, -1.0f, -1.0f);

        auto& volumes = CWorldScene::s_occlusionVolumes;
        volumes.SetCount(1);
        volumes[0].firstPlane = 0;
        volumes[0].planeCount = 6;

        CHECK(MapOcclusion::GetVolumeCount() == 1);

        const C3Vector inside[3] = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.0f }, { -0.5f, 0.2f, 0.9f } };
        CHECK(MapOcclusion::PolygonOccluded(inside, 3) == 1);

        const C3Vector straddling[3] = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.0f }, { 1.5f, 0.0f, 0.0f } };
        CHECK(MapOcclusion::PolygonOccluded(straddling, 3) == 0);

        // A point exactly on a face is still inside.
        const C3Vector onFace[1] = { { 1.0f, 0.0f, 0.0f } };
        CHECK(MapOcclusion::PolygonOccluded(onFace, 1) == 1);

        SECTION("a second volume can occlude what the first does not") {
            planes.SetCount(12);
            for (int32_t i = 0; i < 6; i++) {
                planes[6 + i] = planes[i];
                planes[6 + i].d -= 10.0f;   // the same box, ten times larger
            }

            volumes.SetCount(2);
            volumes[1].firstPlane = 6;
            volumes[1].planeCount = 6;

            CHECK(MapOcclusion::PolygonOccluded(straddling, 3) == 1);
        }
    }

    MapOcclusion::ClearVolumes();
}
