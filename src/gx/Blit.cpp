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

// THE SEVEN DXT-TO-UNCOMPRESSED BLITTERS BELOW ARE ALL STUBS, and they are the only reason
// CBLPFile::Lock2 cannot convert a compressed mip -- see the long note at its COLOR_DXT case.
// Their reference addresses were recovered from InitBlit (FUN_006ae6e0) by decoding the slot each
// assignment writes: the table index is `alpha + (srcFmt * 13 + dstFmt) * 4` and each entry is 4
// bytes, so the byte offset from the table base at 0x00c60930 is `16 * (src * 13 + dst) + 4 *
// alpha`. Every one of InitBlit's twenty-five assignments decodes to a slot frozen also fills,
// which is what makes the seven below trustworthy rather than guessed.
//
// Each is a six-line WRAPPER of identical shape -- verified on three of them -- choosing between
// two decoders by whether the image is whole 4x4 blocks:
//
//     if (size.x > 3 && size.y > 3 && (size.x & 3) == 0 && (size.y & 3) == 0)
//         fast(...);   // every block complete
//     else
//         general(...);   // partial blocks at the right or bottom edge
//
// so the work is in the fourteen decoders, not the wrappers. Those addresses, general then fast:
//
//     Blit_Dxt1_Rgb565    FUN_006ae440   ->  006ad440 / 006ad5b0
//     Blit_Dxt1_Argb1555  FUN_006ae4a0   ->  006ad660 / (its own pair)
//     Blit_Dxt1_Argb8888  FUN_006ae500   ->  006ad880 / 006ada10
//     Blit_Dxt3_Argb4444  FUN_006ae560   ->  006adae0 / (its own pair)
//     Blit_Dxt3_Argb8888  FUN_006ae5c0   ->  006add20 / (its own pair)
//     Blit_Dxt5_Argb4444  FUN_006ae620   ->  006adf90 / (its own pair)
//     Blit_Dxt5_Argb8888  FUN_006ae680   ->  006ae1d0 / 006ae360
//
// Implementing Dxt1 to Argb8888 first is worth the most: DXT1 is the commonest encoding in the
// archives, and Argb8888 is what GetTextureFormats falls back to when the device cannot sample
// compressed textures -- which is the case the GLES backend cares about.
// ref: FUN_006ae500
void Blit_Dxt1_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

// ref: FUN_006ae4a0
void Blit_Dxt1_Argb1555(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

// ref: FUN_006ae440
void Blit_Dxt1_Rgb565(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

void Blit_Dxt1_Dxt1(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    int32_t v6 = std::max(size.x, 4);
    int32_t v7 = std::max(size.y, 4);

    memcpy(out, in, (4 * v6 * v7) >> 3);
}

// ref: FUN_006ae5c0
void Blit_Dxt3_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

// ref: FUN_006ae560
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

// ref: FUN_006ae680
void Blit_Dxt5_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    WHOA_UNIMPLEMENTED();
}

// ref: FUN_006ae620
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

// ref: FUN_006ae7c0
// The reference's own index arithmetic is `alpha + (srcFmt * 13 + dstFmt) * 4`, which is exactly
// this table's declaration order -- that is what identifies which argument is which index.
//
// THE NULL CHECK IS NOT DEFENSIVE PADDING, it is the reference's and it was missing. InitBlit
// fills 26 of the table's 676 slots, so most format pairs have no blitter at all, and calling
// the slot unconditionally was a null call waiting for the first unhandled pair. The reference
// answers 0 instead, which is why this returns int32_t rather than void.
int32_t Blit(const C2iVector& size, BlitAlpha alpha, const void* src, uint32_t srcStride, BlitFormat srcFmt, void* dst, uint32_t dstStride, BlitFormat dstFmt) {
    if (!initBlit) {
        InitBlit();
        initBlit = 1;
    }

    BLIT_FUNCTION blit = s_blits[srcFmt][dstFmt][alpha];

    if (!blit) {
        return 0;
    }

    blit(size, src, srcStride, dst, dstStride);

    return 1;
}
