#include "ui/game/Cursor.hpp"
#include <cstring>

namespace {

// ref: DAT_00c25de4
int32_t s_cursorMode;

}

// ref: FUN_006160b0
// Fills a 32x32 ARGB cursor image. A 32x32 source is copied as is; a 64x64 one is halved by
// averaging each 2x2 block, rounding to nearest, and comes out fully opaque. Any other size, or
// no image, leaves the cursor blank and fails.
int32_t CopyCursorImage(uint32_t* dest, const uint32_t* const* image, int32_t width, int32_t height) {
    if (image) {
        if (width == 32) {
            if (height == 32) {
                memcpy(dest, *image, 0x1000);
                return 1;
            }
        } else if (width == 64 && height == 64) {
            auto row0 = *image;
            auto row1 = row0;
            auto end = dest + 0x400;
            auto out = dest;

            while (out < end) {
                row1 += 64;
                auto rowEnd = out + 32;

                while (out < rowEnd) {
                    auto green = ((row0[0] >> 2) & 0x3fc0) + ((row1[0] >> 2) & 0x3fc0) + ((row0[1] >> 2) & 0x3fc0) + 0x80 + ((row1[1] >> 2) & 0x3fc0);
                    auto redBlue = (row0[0] & 0xff00ff) + (row1[0] & 0xff00ff) + (row0[1] & 0xff00ff) + 0x20002 + (row1[1] & 0xff00ff);

                    *out = (green & 0xff00) | ((redBlue >> 2) & 0xffff00ff) | 0xff000000;

                    row0 += 2;
                    row1 += 2;
                    out++;
                }

                row0 += 64;
            }

            return 1;
        }
    }

    memset(dest, 0, 0x1000);

    return 0;
}

// ref: FUN_00616260
int32_t GetCursorMode() {
    return s_cursorMode;
}

// ref: FUN_00616270
void SetCursorMode(int32_t mode) {
    s_cursorMode = mode;
}
