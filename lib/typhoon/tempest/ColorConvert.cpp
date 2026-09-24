#include "tempest/ColorConvert.hpp"
#include "tempest/Intersect.hpp"
#include <cmath>

// ref: FUN_009829f0
uint32_t MinAxis(const C3Vector& v) {
    if (fabsf(v.x) < fabsf(v.y)) {
        return fabsf(v.x) >= fabsf(v.z) ? 2 : 0;
    }

    return fabsf(v.y) > fabsf(v.z) ? 2 : 1;
}

// ref: FUN_00984f60
void RgbToHsv(const C3Vector& rgb, C3Vector& hsv) {
    const float* c = &rgb.x;

    uint32_t maxI = DominantAxis(rgb);
    uint32_t minI = MinAxis(rgb);

    float v = c[maxI];
    hsv.z = v;

    float s = 0.0f;

    if (v != 0.0f) {
        s = (c[maxI] - c[minI]) / c[maxI];
    }

    hsv.y = s;

    if (s != 0.0f) {
        float delta = c[maxI] - c[minI];

        if (maxI == 0) {
            hsv.x = (rgb.y - rgb.z) / delta;
        } else if (maxI == 1) {
            hsv.x = (rgb.z - rgb.x) / delta + 2.0f;
        } else if (maxI == 2) {
            hsv.x = (rgb.x - rgb.y) / delta + 4.0f;
        }

        hsv.x = hsv.x * 60.0f;

        if (hsv.x < 0.0f) {
            hsv.x = hsv.x + 360.0f;
        }

        return;
    }

    hsv.x = -1.0f;
}

// ref: FUN_00985030
void HsvToRgb(const C3Vector& hsv, C3Vector& rgb) {
    if (hsv.y != 0.0f) {
        float h = hsv.x;

        if (h >= 360.0f) {
            h = h - 360.0f;
        }

        int32_t i = static_cast<int32_t>(nearbyintf(h * (1.0f / 60.0f) - 0.5f));

        if (i > 5) {
            i = 5;
        }

        float f = h * (1.0f / 60.0f) - static_cast<float>(i);
        float s = hsv.y >= 1.0f ? 1.0f : hsv.y;
        float v = hsv.z;

        float p = (1.0f - s) * v;
        float q = (1.0f - s * f) * v;
        float t = (1.0f - (1.0f - f) * s) * v;

        switch (i) {
        case 0:
            rgb = { v, t, p };
            return;
        case 1:
            rgb = { q, v, p };
            return;
        case 2:
            rgb = { p, v, t };
            return;
        case 3:
            rgb = { p, q, v };
            return;
        case 4:
            rgb = { t, p, v };
            return;
        case 5:
            rgb = { v, p, q };
            return;
        default:
            return;
        }
    }

    rgb = { hsv.z, hsv.z, hsv.z };
}

// ref: FUN_009851a0
void PackColor(CImVector& out, const C3Vector& rgb) {
    out.a = 0xFF;
    out.r = static_cast<uint8_t>(static_cast<int32_t>(nearbyintf(rgb.x * 255.0f)));
    out.g = static_cast<uint8_t>(static_cast<int32_t>(nearbyintf(rgb.y * 255.0f)));
    out.b = static_cast<uint8_t>(static_cast<int32_t>(nearbyintf(rgb.z * 255.0f)));
}

// The sibling of PackColor above, and in the reference they sit in the same neighbourhood --
// 0x00982970 against PackColor's 0x009851a0. It reads the bytes at +2, +1 and +0, which is r, g
// and b of a CImVector, and scales each by the 1/255 at 0x00a45564.
//
// Note it does NOT round, where PackColor does: the reference converts each byte with fild and
// multiplies, with no nearbyint anywhere. Going this direction there is nothing to round to.
// ref: FUN_00982970
void UnpackColor(C3Vector& out, const CImVector& color) {
    out.x = color.r * (1.0f / 255.0f);
    out.y = color.g * (1.0f / 255.0f);
    out.z = color.b * (1.0f / 255.0f);
}

// ref: FUN_006acc50
void LerpColor(CImVector& color, uint32_t alpha, const CImVector& target) {
    if (alpha == 0xFF) {
        color.value = color.value ^ ((target.value ^ color.value) & 0xFFFFFF);
        return;
    }

    uint8_t r = static_cast<uint8_t>(((static_cast<uint32_t>(target.r) - color.r) * alpha >> 8) + color.r);
    uint8_t g = static_cast<uint8_t>(((static_cast<uint32_t>(target.g) - color.g) * alpha >> 8) + color.g);
    uint8_t b = static_cast<uint8_t>(((static_cast<uint32_t>(target.b) - color.b) * alpha >> 8) + color.b);

    color.value = (static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 | b) | (color.value & 0xFF000000);
}
