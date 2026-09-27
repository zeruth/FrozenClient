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
// ------------------------------------------------------------------------------------------------
// DXT1 decoding. The colour maths below is transcribed from the reference; the loop that walks the
// blocks is not, and the difference is called out where it happens.
// ------------------------------------------------------------------------------------------------

// ref: FUN_006ae820
// The 1/3 and 2/3 blend weights, precomputed so the interpolated endpoints cost two table reads
// and an add. The reference keeps FOUR tables -- a 32-entry and a 64-entry copy of each weight,
// for 5-bit and 6-bit channels -- built by one function over the same two formulas. frozen keeps
// two 64-entry tables instead, because the formulas do not depend on the channel width and a
// 6-bit table answers every 5-bit question. Same values, half the tables.
//
// The +1 is the reference's and is not rounding: it biases the sum so that two endpoints that are
// equal reproduce themselves exactly after the >> 8.
static uint16_t s_dxtWeight1_3[64];
static uint16_t s_dxtWeight2_3[64];
static int32_t s_dxtWeightsBuilt = 0;

static void BuildDxtWeights() {
    for (uint32_t i = 0; i < 64; i++) {
        s_dxtWeight1_3[i] = static_cast<uint16_t>(((i << 8) / 3) + 1);
        s_dxtWeight2_3[i] = static_cast<uint16_t>(((i << 9) / 3) + 1);
    }

    s_dxtWeightsBuilt = 1;
}

// A 5/6/5 colour as ARGB8888, opaque. The reference does this with one expression per endpoint --
// `(((r | 0xFFFFFFE0) << 9 | g) << 7 | b) * 8` -- where the 0xFFFFFFE0 is not a mask on r but the
// alpha: shifted left 19 in total it becomes 0xFF000000. Checked against the reference by
// evaluating it: r=31, g=63, b=31 gives 0xFFF8FCF8, which is what this returns.
static inline uint32_t Dxt565ToArgb8888(uint32_t r5, uint32_t g6, uint32_t b5) {
    return 0xFF000000u | (r5 << 19) | (g6 << 10) | (b5 << 3);
}

// ref: FUN_006aca10
// A DXT1 block's four colours. The two stored endpoints come first; what follows them depends on
// their ORDER, which is how DXT1 encodes one bit of "this block has transparency" without a bit:
// c0 > c1 means four opaque colours, two of them interpolated at thirds, and c0 <= c1 means three
// colours plus fully transparent black.
//
// Note the two arms do not agree on how they blend, and that is the reference's: the four-colour
// arm goes through the weight tables, while the three-colour arm takes a plain integer average in
// 5/6/5 space.
static void Dxt1ExpandColors(const unsigned char* block, uint32_t colors[4]) {
    uint32_t c0 = static_cast<uint32_t>(block[0]) | (static_cast<uint32_t>(block[1]) << 8);
    uint32_t c1 = static_cast<uint32_t>(block[2]) | (static_cast<uint32_t>(block[3]) << 8);

    uint32_t r0 = c0 >> 11;
    uint32_t g0 = (c0 >> 5) & 0x3F;
    uint32_t b0 = c0 & 0x1F;

    uint32_t r1 = c1 >> 11;
    uint32_t g1 = (c1 >> 5) & 0x3F;
    uint32_t b1 = c1 & 0x1F;

    colors[0] = Dxt565ToArgb8888(r0, g0, b0);
    colors[1] = Dxt565ToArgb8888(r1, g1, b1);

    if (c1 < c0) {
        colors[2] = Dxt565ToArgb8888(
            (s_dxtWeight2_3[r0] + s_dxtWeight1_3[r1]) >> 8,
            (s_dxtWeight2_3[g0] + s_dxtWeight1_3[g1]) >> 8,
            (s_dxtWeight2_3[b0] + s_dxtWeight1_3[b1]) >> 8
        );

        colors[3] = Dxt565ToArgb8888(
            (s_dxtWeight1_3[r0] + s_dxtWeight2_3[r1]) >> 8,
            (s_dxtWeight1_3[g0] + s_dxtWeight2_3[g1]) >> 8,
            (s_dxtWeight1_3[b0] + s_dxtWeight2_3[b1]) >> 8
        );
    } else {
        colors[2] = Dxt565ToArgb8888((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2);
        colors[3] = 0;
    }
}

// ref: FUN_006ad380
// One 4x4 block into ARGB8888, clipped to `cols` by `rows` for a block that hangs off the right or
// bottom edge. Bytes 0..3 of a DXT1 block are the two endpoints and bytes 4..7 are one byte of
// 2-bit indices per row, low bits leftmost.
//
// DIVERGENCE, and it is bookkeeping rather than behaviour: the reference passes an array of four
// row pointers and a six-field rectangle, and the block decoder ADVANCES those pointers as it
// goes, so its callers reset them per block row. This takes a destination and a stride and
// addresses each texel directly. Same bytes written; Ghidra's rendering of the reference's
// pointer arithmetic is where its two loop variants become hard to read, and there is nothing to
// be gained by reproducing that.
static void Dxt1DecodeBlock(const unsigned char* block, unsigned char* dst, uint32_t dstStride,
                            uint32_t cols, uint32_t rows) {
    uint32_t colors[4];

    Dxt1ExpandColors(block, colors);

    for (uint32_t y = 0; y < rows; y++) {
        uint32_t bits = block[4 + y];
        auto out = reinterpret_cast<uint32_t*>(dst + y * dstStride);

        for (uint32_t x = 0; x < cols; x++) {
            out[x] = colors[bits & 3];
            bits >>= 2;
        }
    }
}
// ref: FUN_006ae500
// The reference splits this in two -- FUN_006ada10 when the image is whole 4x4 blocks and
// FUN_006ad880 when it is not -- and the wrapper picks between them on
// `w > 3 && h > 3 && (w & 3) == 0 && (h & 3) == 0`. frozen keeps one loop, because the aligned
// case is the general one with the clamps never binding, and a second copy of it would be a
// second place for an edge bug to hide.
//
// The reference's general arm also walks a cube map as six faces, dividing the width by six and
// advancing per face. That is not reproduced and does not need to be: a cube map arrives here as
// six faces side by side, its blocks tile the full width row by row, and decoding it as one wide
// image writes the same bytes to the same places.
void Blit_Dxt1_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (!s_dxtWeightsBuilt) {
        BuildDxtWeights();
    }

    if (size.x <= 0 || size.y <= 0) {
        return;
    }

    auto width = static_cast<uint32_t>(size.x);
    auto height = static_cast<uint32_t>(size.y);

    auto src = static_cast<const unsigned char*>(in);
    auto dst = static_cast<unsigned char*>(out);

    for (uint32_t y = 0; y < height; y += 4) {
        const unsigned char* block = src;
        uint32_t rows = height - y < 4 ? height - y : 4;

        for (uint32_t x = 0; x < width; x += 4) {
            uint32_t cols = width - x < 4 ? width - x : 4;

            Dxt1DecodeBlock(block, dst + x * 4, outStride, cols, rows);

            block += 8;
        }

        // One row of BLOCKS in the source, four rows of texels in the destination.
        src += inStride;
        dst += outStride * 4;
    }
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

// ref: FUN_006ac270
// The same four colours as Dxt1ExpandColors builds in its c0 > c1 arm, and ONLY that arm --
// there is no endpoint-order test here and no transparent fourth colour. That is not an
// omission: DXT3 and DXT5 carry their alpha in a block of its own, so the colour block always
// means four interpolated opaque colours whichever way round the endpoints are. The reference
// keeps this as a separate function from the DXT1 expander for exactly that reason.
static void DxtExpandColorsNoAlphaMode(const unsigned char* block, uint32_t colors[4]) {
    uint32_t c0 = static_cast<uint32_t>(block[0]) | (static_cast<uint32_t>(block[1]) << 8);
    uint32_t c1 = static_cast<uint32_t>(block[2]) | (static_cast<uint32_t>(block[3]) << 8);

    uint32_t r0 = c0 >> 11;
    uint32_t g0 = (c0 >> 5) & 0x3F;
    uint32_t b0 = c0 & 0x1F;

    uint32_t r1 = c1 >> 11;
    uint32_t g1 = (c1 >> 5) & 0x3F;
    uint32_t b1 = c1 & 0x1F;

    colors[0] = Dxt565ToArgb8888(r0, g0, b0);
    colors[1] = Dxt565ToArgb8888(r1, g1, b1);

    colors[2] = Dxt565ToArgb8888(
        (s_dxtWeight2_3[r0] + s_dxtWeight1_3[r1]) >> 8,
        (s_dxtWeight2_3[g0] + s_dxtWeight1_3[g1]) >> 8,
        (s_dxtWeight2_3[b0] + s_dxtWeight1_3[b1]) >> 8
    );

    colors[3] = Dxt565ToArgb8888(
        (s_dxtWeight1_3[r0] + s_dxtWeight2_3[r1]) >> 8,
        (s_dxtWeight1_3[g0] + s_dxtWeight2_3[g1]) >> 8,
        (s_dxtWeight1_3[b0] + s_dxtWeight2_3[b1]) >> 8
    );
}

// ref: FUN_006acd80
// One DXT3 block into ARGB8888, clipped like the DXT1 one. A DXT3 block is 16 bytes: eight of
// alpha, four bits per texel and one uint16 per row, then an eight-byte colour block identical
// to DXT1's -- which is why the colour indices are at +0x0c rather than +0x04.
//
// The alpha expansion is the reference's own and it is LOSSY in a way worth not tidying: it is
// FUN_006abab0, `v << 4`, so a fully opaque texel comes out 0xF0 rather than 0xFF. Expanding by
// `v * 0x11` would be the usual way and would reach 0xFF, and it is not what the reference does.
static void Dxt3DecodeBlock(const unsigned char* block, unsigned char* dst, uint32_t dstStride,
                            uint32_t cols, uint32_t rows) {
    uint32_t colors[4];

    DxtExpandColorsNoAlphaMode(block + 8, colors);

    for (uint32_t y = 0; y < rows; y++) {
        uint32_t indices = block[0x0C + y];
        uint32_t alpha = static_cast<uint32_t>(block[y * 2])
                       | (static_cast<uint32_t>(block[y * 2 + 1]) << 8);

        auto out = reinterpret_cast<uint32_t*>(dst + y * dstStride);

        for (uint32_t x = 0; x < cols; x++) {
            uint32_t color = colors[indices & 3];

            out[x] = (color & 0x00FFFFFF) | ((alpha & 0xF) << 4 << 24);

            indices >>= 2;
            alpha >>= 4;
        }
    }
}
// ref: FUN_006ae5c0
// One loop for both of the reference's arms, and no cube-map special case, for the reasons given
// at Blit_Dxt1_Argb8888. The only difference from that function is the block size -- 16 bytes
// here rather than 8, because of the alpha block in front.
void Blit_Dxt3_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (!s_dxtWeightsBuilt) {
        BuildDxtWeights();
    }

    if (size.x <= 0 || size.y <= 0) {
        return;
    }

    auto width = static_cast<uint32_t>(size.x);
    auto height = static_cast<uint32_t>(size.y);

    auto src = static_cast<const unsigned char*>(in);
    auto dst = static_cast<unsigned char*>(out);

    for (uint32_t y = 0; y < height; y += 4) {
        const unsigned char* block = src;
        uint32_t rows = height - y < 4 ? height - y : 4;

        for (uint32_t x = 0; x < width; x += 4) {
            uint32_t cols = width - x < 4 ? width - x : 4;

            Dxt3DecodeBlock(block, dst + x * 4, outStride, cols, rows);

            block += 16;
        }

        src += inStride;
        dst += outStride * 4;
    }
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
