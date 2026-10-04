#include "ui/game/ScreenLayout.hpp"
#include "gx/Coordinate.hpp"
#include "catch.hpp"
#include <tempest/Rect.hpp>

// Where floating world text goes on the screen (ScriptEvents.cpp 0x00615050..0x00615cd0): a rect is
// moved breadth-first until it clears everything already placed in its list, and kept on the screen.
// The rects are DDC, minY the TOP edge and maxY the bottom, as the callers fill them.

namespace {

CRect Centred(float x, float y, float halfWidth, float halfHeight) {
    CRect rect;
    rect.minY = y + halfHeight;
    rect.minX = x - halfWidth;
    rect.maxY = y - halfHeight;
    rect.maxX = x + halfWidth;
    return rect;
}

bool Overlap(const CRect& a, const CRect& b) {
    return a.minX < b.maxX && a.maxX > b.minX && a.maxY < b.minY && a.minY > b.maxY;
}

bool OnScreen(const CRect& r) {
    const float eps = 1e-5f;
    return r.minX >= -eps && r.maxX <= NDCToDDCWidth(1.0f) + eps && r.maxY >= -eps && r.minY <= NDCToDDCHeight(1.0f) + eps;
}

} // namespace

TEST_CASE("ScreenLayoutPlace", "[ui][worldtext]") {
    CoordinateSetAspectRatio(4.0f / 3.0f);
    ScreenLayoutClear(1);

    SECTION("a rect on an empty list stays where it is") {
        CRect rect = Centred(0.4f, 0.3f, 0.05f, 0.01f);
        CRect before = rect;
        ScreenLayoutPlace(1, rect);

        CHECK(rect.minX == Approx(before.minX));
        CHECK(rect.minY == Approx(before.minY));
        CHECK(rect.maxX == Approx(before.maxX));
        CHECK(rect.maxY == Approx(before.maxY));
    }

    SECTION("texts at one spot are spread apart and keep their size") {
        CRect placed[6];

        for (auto& rect : placed) {
            rect = Centred(0.4f, 0.3f, 0.05f, 0.01f);
            ScreenLayoutPlace(1, rect);
        }

        for (int32_t i = 0; i < 6; i++) {
            CHECK(placed[i].maxX - placed[i].minX == Approx(0.1f));
            CHECK(placed[i].minY - placed[i].maxY == Approx(0.02f));
            CHECK(OnScreen(placed[i]));

            for (int32_t j = 0; j < i; j++) {
                CHECK_FALSE(Overlap(placed[i], placed[j]));
            }
        }
    }

    SECTION("a rect off the edge is pulled back on") {
        CRect rect = Centred(-0.5f, 0.3f, 0.05f, 0.01f);
        ScreenLayoutPlace(1, rect);
        CHECK(OnScreen(rect));

        CRect high = Centred(0.4f, 5.0f, 0.05f, 0.01f);
        ScreenLayoutPlace(1, high);
        CHECK(OnScreen(high));
    }

    SECTION("the lists are independent, and clearing frees the space") {
        CRect a = Centred(0.4f, 0.3f, 0.05f, 0.01f);
        ScreenLayoutPlace(1, a);

        CRect b = Centred(0.4f, 0.3f, 0.05f, 0.01f);
        ScreenLayoutPlace(0, b);
        CHECK(b.minX == Approx(a.minX));
        CHECK(b.minY == Approx(a.minY));

        ScreenLayoutClear(1);
        CRect c = Centred(0.4f, 0.3f, 0.05f, 0.01f);
        ScreenLayoutPlace(1, c);
        CHECK(c.minY == Approx(a.minY));

        ScreenLayoutClear(0);
    }

    ScreenLayoutClear(1);
}
