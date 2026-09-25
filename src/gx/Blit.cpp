#include "gx/Blit.hpp"
#include "util/Unimplemented.hpp"
#include <algorithm>
#include <cstring>
#include <tempest/Vector.hpp>

int32_t initBlit = 0;
BLIT_FUNCTION s_blits[BlitFormats_Last][BlitFormats_Last][BlitAlphas_Last];

BlitFormat GxGetBlitFormat(EGxTexFormat format) {
    static BlitFormat blitTable[] = {
        BlitFormat_Unknown,     // GxTex_Unknown
        BlitFormat_Abgr8888,    // GxTex_Abgr8888
        BlitFormat_Argb8888,    // GxTex_Argb8888
        BlitFormat_Argb4444,    // GxTex_Argb4444
        BlitFormat_Argb1555,    // GxTex_Argb1555
        BlitFormat_Rgb565,      // GxTex_Rgb565
        BlitFormat_Dxt1,        // GxTex_Dxt1
        BlitFormat_Dxt3,        // GxTex_Dxt3
        BlitFormat_Dxt5,        // GxTex_Dxt5
        BlitFormat_Uv88,        // GxTex_Uv88
        BlitFormat_Gr1616F,     // GxTex_Gr1616F
        BlitFormat_R32F,        // GxTex_R32F
        BlitFormat_D24X8        // GxTex_D24X8
    };

    return blitTable[format];
}

void Blit_uint16_uint16(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (inStride == 2 * size.x && outStride == 2 * size.x) {
        memcpy(out, in, 2 * size.x * size.y);
        return;
    }

    auto in_ = static_cast<const char*>(in);
    auto out_ = static_cast<char*>(out);

    for (int32_t i = 0; i < size.y; i++) {
        memcpy(out_, in_, 2 * size.x);
        in_ += inStride;
        out_ += outStride;
    }
}

void Blit_uint32_uint32(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (inStride == 4 * size.x && outStride == 4 * size.x) {
        memcpy(out, in, 4 * size.x * size.y);
        return;
    }

    auto in_ = static_cast<const char*>(in);
    auto out_ = static_cast<char*>(out);

    for (int32_t i = 0; i < size.y; i++) {
        memcpy(out_, in_, 4 * size.x);
        in_ += inStride;
        out_ += outStride;
    }
}

void Blit_Argb8888_Abgr8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

// Copies ARGB8888 texels, collapsing alpha to fully opaque or fully transparent
void Blit_Argb8888_Argb8888_A1(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    auto inRow = static_cast<const uint8_t*>(in);
    auto outRow = static_cast<uint8_t*>(out);

    for (int32_t row = 0; row < size.y; row++) {
        for (int32_t col = 0; col < size.x; col++) {
            outRow[col * 4 + 0] = inRow[col * 4 + 0];
            outRow[col * 4 + 1] = inRow[col * 4 + 1];
            outRow[col * 4 + 2] = inRow[col * 4 + 2];
            outRow[col * 4 + 3] = inRow[col * 4 + 3] >= 0x80 ? 0xFF : 0x00;
        }

        inRow += inStride;
        outRow += outStride;
    }
}

void Blit_Argb8888_Argb8888_A8(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    Blit_uint32_uint32(size, in, inStride, out, outStride);
}

void Blit_Argb8888_Argb4444(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    auto inRow = static_cast<const uint8_t*>(in);
    auto outRow = static_cast<uint8_t*>(out);

    for (int32_t row = 0; row < size.y; row++) {
        for (int32_t col = 0; col < size.x; col++) {
            // Each ARGB8888 pixel is 4 bytes: [B, G, R, A]
            uint8_t b = inRow[col * 4 + 0];
            uint8_t g = inRow[col * 4 + 1];
            uint8_t r = inRow[col * 4 + 2];
            uint8_t a = inRow[col * 4 + 3];

            uint16_t px = ((a & 0xF0) << 8)     // Alpha: bits 15-12
                        | ((r & 0xF0) << 4)     // Red: bits 11-8
                        | (g & 0xF0)            // Green: bits 7-4
                        | (b >> 4);             // Blue: bits 3-0

            *(reinterpret_cast<uint16_t*>(&outRow[col * 2])) = px;
        }

        inRow += inStride;
        outRow += outStride;
    }
}

void Blit_Argb8888_Argb1555(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    auto inRow = static_cast<const uint8_t*>(in);
    auto outRow = static_cast<uint8_t*>(out);

    for (int32_t row = 0; row < size.y; row++) {
        for (int32_t col = 0; col < size.x; col++) {
            // Each ARGB8888 pixel is 4 bytes: [B, G, R, A]
            uint8_t b = inRow[col * 4 + 0];
            uint8_t g = inRow[col * 4 + 1];
            uint8_t r = inRow[col * 4 + 2];
            uint8_t a = inRow[col * 4 + 3];

            uint16_t px = (a >= 0x80 ? 0x8000 : 0x0000)   // Alpha: bit 15
                        | ((r & 0xF8) << 7)                 // Red: bits 14-10
                        | ((g & 0xF8) << 2)                 // Green: bits 9-5
                        | (b >> 3);                         // Blue: bits 4-0

            *(reinterpret_cast<uint16_t*>(&outRow[col * 2])) = px;
        }

        inRow += inStride;
        outRow += outStride;
    }
}

void Blit_Argb8888_Rgb565(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (size.y == 0) {
        return;
    }

    auto inRow = static_cast<const uint8_t*>(in);
    auto outRow = static_cast<uint8_t*>(out);

    if (size.x >= 2) {
        // Process two pixels at a time
        for (int32_t row = 0; row < size.y; row++) {
            for (int32_t col = 0; col < size.x; col += 2) {
                // Each ARGB8888 pixel is 4 bytes: [B, G, R, A]

                // First pixel (bytes 0-3)
                auto b0 = inRow[col * 4 + 0];
                auto g0 = inRow[col * 4 + 1];
                auto r0 = inRow[col * 4 + 2];
                // Ignore alpha

                // Second pixel (bytes 4-7)
                auto b1 = inRow[col * 4 + 4];
                auto g1 = inRow[col * 4 + 5];
                auto r1 = inRow[col * 4 + 6];
                // Ignore alpha

                // Convert to RGB565 (pack bits)
                uint16_t p0 = ((r0 & 0xF8) << 8)    // Red: bits 15-11
                            | ((g0 & 0xFC) << 3)    // Green: bits 10-5
                            | ((b0 & 0xF8) >> 3);   // Blue: bits 4-0

                uint16_t p1 = ((r1 & 0xF8) << 8)
                            | ((g1 & 0xFC) << 3)
                            | ((b1 & 0xF8) >> 3);

                // Write packed pixels
                *(reinterpret_cast<uint32_t*>(&outRow[col * 2])) = p0 | (p1 << 16);
            }

            inRow += inStride;
            outRow += outStride;
        }
    } else {
        // Process one pixel at a time
        for (int32_t row = 0; row < size.y; row++) {
            for (int32_t col = 0; col < size.x; col++) {
                // Each ARGB8888 pixel is 4 bytes: [B, G, R, A]

                uint8_t b = inRow[col * 4 + 0];
                uint8_t g = inRow[col * 4 + 1];
                uint8_t r = inRow[col * 4 + 2];
                // Ignore alpha

                // Convert to RGB565 (pack bits)
                uint16_t px = ((r & 0xF8) << 8)     // Red: bits 15-11
                            | ((g & 0xFC) << 3)     // Green: bits 10-5
                            | ((b & 0xF8) >> 3);    // Blue: bits 4-0

                // Write packed pixel
                *(reinterpret_cast<uint16_t*>(&outRow[col * 2])) = px;
            }

            inRow += inStride;
            outRow += outStride;
        }
    }
}

void Blit_Argb4444_Abgr8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

void Blit_Dxt1_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

void Blit_Dxt1_Argb1555(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

void Blit_Dxt1_Rgb565(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

void Blit_Dxt1_Dxt1(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    int32_t v6 = std::max(size.x, 4);
    int32_t v7 = std::max(size.y, 4);

    memcpy(out, in, (4 * v6 * v7) >> 3);
}

void Blit_Dxt3_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

void Blit_Dxt3_Argb4444(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

void Blit_Dxt35_Dxt35(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    int32_t v5 = std::max(size.x, 4);
    int32_t v6 = std::max(size.y / 4, 1);

    if (inStride == v5 * 4 && outStride == v5 * 4) {
        memcpy(out, in, v5 * v6 * 4);
        return;
    }

    auto in_ = static_cast<const char*>(in);
    auto out_ = static_cast<char*>(out);

    for (int32_t i = v6; i > 0; i--) {
        memcpy(out_, in_, v5 * 4);
        in_ += inStride;
        out_ += outStride;
    }
}

void Blit_Dxt5_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

void Blit_Dxt5_Argb4444(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

// Straight copies are shared: every 16-bit format uses Blit_uint16_uint16 and every 32-bit one
// Blit_uint32_uint32, as the reference points those slots at the same two functions.
// ref: FUN_006ae6e0
void InitBlit() {
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Abgr8888]   [BlitAlpha_0]   = &Blit_Argb8888_Abgr8888;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb8888]   [BlitAlpha_0]   = &Blit_uint32_uint32;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb8888]   [BlitAlpha_1]   = &Blit_Argb8888_Argb8888_A1;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb8888]   [BlitAlpha_8]   = &Blit_Argb8888_Argb8888_A8;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb4444]   [BlitAlpha_0]   = &Blit_Argb8888_Argb4444;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb1555]   [BlitAlpha_0]   = &Blit_Argb8888_Argb1555;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Rgb565]     [BlitAlpha_0]   = &Blit_Argb8888_Rgb565;
    s_blits [BlitFormat_Rgb565]     [BlitFormat_Rgb565]     [BlitAlpha_0]   = &Blit_uint16_uint16;
    s_blits [BlitFormat_Argb4444]   [BlitFormat_Abgr8888]   [BlitAlpha_0]   = &Blit_Argb4444_Abgr8888;
    s_blits [BlitFormat_Argb4444]   [BlitFormat_Argb4444]   [BlitAlpha_0]   = &Blit_uint16_uint16;
    s_blits [BlitFormat_Argb1555]   [BlitFormat_Argb1555]   [BlitAlpha_0]   = &Blit_uint16_uint16;
    s_blits [BlitFormat_Dxt1]       [BlitFormat_Dxt1]       [BlitAlpha_0]   = &Blit_Dxt1_Dxt1;
    s_blits [BlitFormat_Dxt3]       [BlitFormat_Dxt3]       [BlitAlpha_0]   = &Blit_Dxt35_Dxt35;
    s_blits [BlitFormat_Dxt5]       [BlitFormat_Dxt5]       [BlitAlpha_0]   = &Blit_Dxt35_Dxt35;
    s_blits [BlitFormat_Dxt1]       [BlitFormat_Rgb565]     [BlitAlpha_0]   = &Blit_Dxt1_Rgb565;
    s_blits [BlitFormat_Dxt1]       [BlitFormat_Argb1555]   [BlitAlpha_0]   = &Blit_Dxt1_Argb1555;
    s_blits [BlitFormat_Dxt1]       [BlitFormat_Argb8888]   [BlitAlpha_0]   = &Blit_Dxt1_Argb8888;
    s_blits [BlitFormat_Dxt3]       [BlitFormat_Argb4444]   [BlitAlpha_0]   = &Blit_Dxt3_Argb4444;
    s_blits [BlitFormat_Dxt3]       [BlitFormat_Argb8888]   [BlitAlpha_0]   = &Blit_Dxt3_Argb8888;
    s_blits [BlitFormat_Dxt5]       [BlitFormat_Argb4444]   [BlitAlpha_0]   = &Blit_Dxt5_Argb4444;
    s_blits [BlitFormat_Dxt5]       [BlitFormat_Argb8888]   [BlitAlpha_0]   = &Blit_Dxt5_Argb8888;
    s_blits [BlitFormat_Uv88]       [BlitFormat_Uv88]       [BlitAlpha_0]   = &Blit_uint16_uint16;
    s_blits [BlitFormat_Gr1616F]    [BlitFormat_Gr1616F]    [BlitAlpha_0]   = &Blit_uint32_uint32;
    s_blits [BlitFormat_R32F]       [BlitFormat_R32F]       [BlitAlpha_0]   = &Blit_uint32_uint32;
    s_blits [BlitFormat_D24X8]      [BlitFormat_D24X8]      [BlitAlpha_0]   = &Blit_uint32_uint32;
}

void Blit(const C2iVector& size, BlitAlpha alpha, const void* src, uint32_t srcStride, BlitFormat srcFmt, void* dst, uint32_t dstStride, BlitFormat dstFmt) {
    if (!initBlit) {
        InitBlit();
        initBlit = 1;
    }

    BLIT_FUNCTION blit = s_blits[srcFmt][dstFmt][alpha];

    blit(size, src, srcStride, dst, dstStride);
}
