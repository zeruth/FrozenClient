#include "ui/game/CGCamera.hpp"
#include "console/CVar.hpp"
#include "catch.hpp"

void CameraRegisterCVars();

// The mouse wheel's path: CAMERAZOOMIN / OUT run CameraZoomIn(1) / CameraZoomOut(1), which queue a
// timed move (FUN_005ff950 / FUN_005ffa60) that AdvanceTimedValues (FUN_006000e0) walks into the
// distance target each frame. A move whose length comes out zero has no end and never releases.

namespace {

CGCamera* MakeCamera() {
    static bool registered = false;

    if (!registered) {
        CVar::Initialize();
        CameraRegisterCVars();
        registered = true;
    }

    auto camera = new CGCamera();
    camera->m_distance = 5.0f;
    camera->m_distanceBlend.m_target = 5.0f;

    return camera;
}

}

TEST_CASE("Mouse wheel zoom moves the camera and finishes", "[ui][camera]") {
    auto camera = MakeCamera();

    SECTION("zoom in") {
        camera->ZoomIn(1.0f, 1000, 0.0f);
        INFO("timedBits " << camera->m_timedBits << " end " << camera->m_timedEnd[0] << " flags2 " << camera->m_flags2);

        for (int32_t t = 1016; t <= 1400; t += 16) {
            camera->AdvanceTimedValues(t);
        }

        CHECK(camera->m_distance == Approx(4.0f).margin(0.01f));
        CHECK((camera->m_timedBits & 0x3) == 0);
    }

    SECTION("zoom out") {
        camera->ZoomOut(1.0f, 1000, 0.0f);

        for (int32_t t = 1016; t <= 1400; t += 16) {
            camera->AdvanceTimedValues(t);
        }

        CHECK(camera->m_distance == Approx(6.0f).margin(0.01f));
        CHECK((camera->m_timedBits & 0xC) == 0);
    }

    delete camera;
}
