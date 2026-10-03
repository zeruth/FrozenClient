#include "gx/Blit.hpp"
#include "util/Unimplemented.hpp"
#include <algorithm>
#include <cstring>
#include <tempest/ColorConvert.hpp>
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

// ref: FUN_006ac200
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

// ref: FUN_006ac190
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

// ref: FUN_006abf40
// ARGB8888 to ABGR8888: red and blue trade places and the other two bytes stay where they are.
// Both formats hold alpha in the top byte, so only bytes 0 and 2 move.
//
// Written byte by byte rather than as a shift-and-mask word, which is how the reference does it.
void Blit_Argb8888_Abgr8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (size.x <= 0 || size.y <= 0) {
        return;
    }

    auto src = static_cast<const unsigned char*>(in);
    auto dst = static_cast<unsigned char*>(out);

    for (int32_t y = 0; y < size.y; y++) {
        const unsigned char* s = src;
        unsigned char* d = dst;

        for (int32_t x = 0; x < size.x; x++) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = s[3];

            s += 4;
            d += 4;
        }

        src += inStride;
        dst += outStride;
    }
}

// The same-format copies the table holds: one-line forwards to the row copiers above, which are
// what the reference's table entries call.
// ref: FUN_006acce0
void Blit_uint32(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    Blit_uint32_uint32(size, in, inStride, out, outStride);
}

// ref: FUN_006acd70
void Blit_uint16(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    Blit_uint16_uint16(size, in, inStride, out, outStride);
}

// ref: FUN_006abfc0
// One-bit alpha onto an existing image: wherever the source has any alpha at all its colour
// replaces the destination's, and the destination keeps its own alpha.
void Blit_Argb8888_Argb8888_A1(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    auto inRow = static_cast<const uint8_t*>(in);
    auto outRow = static_cast<uint8_t*>(out);

    for (int32_t row = size.y; row; row--) {
        auto src = reinterpret_cast<const uint32_t*>(inRow);
        auto dst = reinterpret_cast<uint32_t*>(outRow);

        for (int32_t col = 0; col < size.x; col++) {
            uint32_t pixel = src[col];

            if (pixel & 0xff000000) {
                dst[col] ^= (dst[col] ^ pixel) & 0xffffff;
            }
        }

        inRow += inStride;
        outRow += outStride;
    }
}

// ref: FUN_006accf0
// Eight-bit alpha onto an existing image: each destination colour pulled toward the source's by
// the source's alpha, the destination's alpha left alone.
void Blit_Argb8888_Argb8888_A8(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    auto inRow = static_cast<const uint8_t*>(in);
    auto outRow = static_cast<uint8_t*>(out);

    for (int32_t row = size.y; row; row--) {
        auto src = reinterpret_cast<const CImVector*>(inRow);
        auto dst = reinterpret_cast<CImVector*>(outRow);

        for (int32_t col = 0; col < size.x; col++) {
            if (src[col].a) {
                LerpColor(dst[col], src[col].a, src[col]);
            }
        }

        inRow += inStride;
        outRow += outStride;
    }
}

// ref: FUN_006abc20
// ARGB8888 down to a 16-bit format: each channel shifted right to its width and left to its place.
// Rows are packed two pixels to a 32-bit store, or one at a time when the image is a single pixel
// wide.
//
// DIVERGED at an odd width: the reference's pair loop runs while the column is below the width,
// so its last pair reads one pixel past the row and writes one past it. Frozen writes that last
// pixel on its own.
static void BlitArgb8888To16(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride,
                             uint32_t aRight, uint32_t rRight, uint32_t gRight, uint32_t bRight,
                             uint32_t aLeft, uint32_t rLeft, uint32_t gLeft, uint32_t bLeft) {
    auto inRow = static_cast<const uint8_t*>(in);
    auto outRow = static_cast<uint8_t*>(out);

    auto pack = [&](const uint8_t* px) -> uint32_t {
        return static_cast<uint32_t>(px[0] >> bRight) << bLeft
            | static_cast<uint32_t>(px[1] >> gRight) << gLeft
            | static_cast<uint32_t>(px[2] >> rRight) << rLeft
            | static_cast<uint32_t>(px[3] >> aRight) << aLeft;
    };

    for (int32_t row = size.y; row; row--) {
        auto dst16 = reinterpret_cast<uint16_t*>(outRow);

        if (size.x < 2) {
            for (int32_t col = 0; col < size.x; col++) {
                dst16[col] = static_cast<uint16_t>(pack(inRow + col * 4));
            }
        } else {
            int32_t col = 0;

            for (; col + 1 < size.x; col += 2) {
                *reinterpret_cast<uint32_t*>(&dst16[col]) = (pack(inRow + col * 4) & 0xffff) | pack(inRow + col * 4 + 4) << 16;
            }

            if (col < size.x) {
                dst16[col] = static_cast<uint16_t>(pack(inRow + col * 4));
            }
        }

        inRow += inStride;
        outRow += outStride;
    }
}

// ref: FUN_006abe00
void Blit_Argb8888_Argb4444(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    BlitArgb8888To16(size, in, inStride, out, outStride, 4, 4, 4, 4, 12, 8, 4, 0);
}

// ref: FUN_006abe30
void Blit_Argb8888_Argb1555(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    BlitArgb8888To16(size, in, inStride, out, outStride, 7, 3, 3, 3, 15, 10, 5, 0);
}

// ref: FUN_006abe60
// Alpha shifted right by eight is always zero: the format has none.
void Blit_Argb8888_Rgb565(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    BlitArgb8888To16(size, in, inStride, out, outStride, 8, 3, 2, 3, 0, 11, 5, 0);
}

// ref: FUN_006abe90
// ARGB4444 to ABGR8888: widen every channel from four bits to eight AND reorder, in one pass.
//
// The widening is REPLICATION -- `v << 4 | v` -- so a full nibble reaches 0xFF and the scale is
// preserved. That is worth contrasting with the DXT3 to ARGB8888 path, which widens the same four
// bits with a plain `v << 4` and therefore stops at 0xF0. Both are the reference's; they are
// different functions and they genuinely disagree.
//
// Destination byte order is red, green, blue, alpha from the low byte up, which is ABGR8888 read
// as a little-endian word.
void Blit_Argb4444_Abgr8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (size.x <= 0 || size.y <= 0) {
        return;
    }

    auto src = static_cast<const unsigned char*>(in);
    auto dst = static_cast<unsigned char*>(out);

    for (int32_t y = 0; y < size.y; y++) {
        auto s = reinterpret_cast<const uint16_t*>(src);
        unsigned char* d = dst;

        for (int32_t x = 0; x < size.x; x++) {
            uint32_t v = s[x];

            uint32_t a = (v >> 12) & 0xF;
            uint32_t r = (v >> 8) & 0xF;
            uint32_t g = (v >> 4) & 0xF;
            uint32_t b = v & 0xF;

            d[0] = static_cast<unsigned char>((r << 4) | r);
            d[1] = static_cast<unsigned char>((g << 4) | g);
            d[2] = static_cast<unsigned char>((b << 4) | b);
            d[3] = static_cast<unsigned char>((a << 4) | a);

            d += 4;
        }

        src += inStride;
        dst += outStride;
    }
}

// The seven DXT-to-uncompressed blitters. Their addresses were recovered from InitBlit
// (FUN_006ae6e0) by decoding the slot each assignment writes: the table index is
// `alpha + (srcFmt * 13 + dstFmt) * 4` and each entry is 4 bytes, so the byte offset from the table
// base at 0x00c60930 is `16 * (src * 13 + dst) + 4 * alpha`.
//
// Each is a wrapper choosing between two walkers by whether the image is whole 4x4 blocks
// (DxtIsAligned), aligned then general:
//
//     Blit_Dxt1_Rgb565    FUN_006ae440   ->  006ad5b0 / 006ad440
//     Blit_Dxt1_Argb1555  FUN_006ae4a0   ->  006ad7d0 / 006ad660
//     Blit_Dxt1_Argb8888  FUN_006ae500   ->  006ada10 / 006ad880
//     Blit_Dxt3_Argb4444  FUN_006ae560   ->  006adc60 / 006adae0
//     Blit_Dxt3_Argb8888  FUN_006ae5c0   ->  006adeb0 / 006add20
//     Blit_Dxt5_Argb4444  FUN_006ae620   ->  006ae110 / 006adf90
//     Blit_Dxt5_Argb8888  FUN_006ae680   ->  006ae360 / 006ae1d0
//
// Argb8888 is what GetTextureFormats falls back to when the device cannot sample compressed
// textures, which is the case the GLES backend cares about.
// ------------------------------------------------------------------------------------------------
// DXT decoding, transcribed from the reference: colour expanders, block writers that take four row
// pointers and a clip rectangle, and per format pair an aligned and a general block walker.
// ------------------------------------------------------------------------------------------------

// The DXT weight tables (built by FUN_006ae820 below).
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

// ref: FUN_006ae820
static void BuildDxtWeights() {
    for (uint32_t i = 0; i < 64; i++) {
        s_dxtWeight1_3[i] = static_cast<uint16_t>(((i << 8) / 3) + 1);
        s_dxtWeight2_3[i] = static_cast<uint16_t>(((i << 9) / 3) + 1);
    }

    s_dxtWeightsBuilt = 1;
}

#if defined(_MSC_VER)
#define BLIT_NOINLINE __declspec(noinline)
#else
#define BLIT_NOINLINE __attribute__((noinline))
#endif

// The part of a 4x4 block a block writer fills: columns left..right and rows top..bottom,
// inclusive, and how many texels wide that is. The reference passes it with an array of four row
// pointers, one per texel row of the block, and the writer moves each pointer it wrote along by
// `width` texels -- so a walker sets the pointers once per row of blocks and then calls along it.
struct DXTRECT {
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
    uint32_t width;
    uint32_t height;
};

// How a DXT3 or DXT5 block writer turns a stored alpha into the target's alpha width.
typedef uint32_t (*DXTALPHAFUNC)(uint32_t);

// ref: FUN_006abab0
// Four bits to eight by a plain shift, so a full nibble stops at 0xF0.
static uint32_t DxtAlpha4To8(uint32_t alpha) {
    return alpha << 4;
}

// ref: FUN_006abc00
static uint32_t DxtAlpha8To4(uint32_t alpha) {
    return alpha >> 4;
}

// ref: FUN_006abc10
// DXT3 into four-bit alpha, and DXT5 into eight-bit alpha, need no conversion.
static uint32_t DxtAlphaSame(uint32_t alpha) {
    return static_cast<uint8_t>(alpha);
}

// The walker for an image made of whole blocks: every block written in full, four destination
// rows at a time.
template <uint32_t BlockBytes, class Decode>
static inline void DxtWalkAligned(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride,
                                  Decode decode) {
    auto src = static_cast<const unsigned char*>(in);
    auto dst = static_cast<unsigned char*>(out);

    for (int32_t y = 0; y < size.y; y += 4) {
        unsigned char* rows[4] = { dst, dst + outStride, dst + outStride * 2, dst + outStride * 3 };
        auto block = src;

        for (int32_t x = 0; x < size.x; x += 4) {
            DXTRECT rect = { 0, 0, 3, 3, 4, 4 };
            decode(block, rows, rect);

            block += BlockBytes;
        }

        src += inStride;
        dst += outStride * 4;
    }
}

// The walker for anything else: the last block of each row and column clipped to the image, and
// an image six times as wide as it is high walked as six cube faces side by side.
//
// DIVERGED twice, both where the reference reads or writes the wrong place:
//   - its destination row pointers are set from the face's first row for EVERY row of blocks, so
//     an image more than one block high (a 2x8 mip, say) has each row of blocks written over the
//     first and the rows below left as they were. Frozen moves down four rows per row of blocks.
//   - it moves the source on by face width times block size per face, which is four times the
//     face's row of blocks once a face is a block wide, and reads past the image for the later
//     faces. Frozen moves on by the face's own blocks.
// Neither arises for whole-block images, which take the aligned walker.
template <uint32_t BlockBytes, uint32_t TexelBytes, class Decode>
static inline void DxtWalkGeneral(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride,
                                  Decode decode) {
    int32_t height = size.y;
    int32_t faceWidth = size.x;
    int32_t faces = 1;

    if (size.x == size.y * 6) {
        faces = 6;
        faceWidth = size.x / 6;
    }

    auto faceSrc = static_cast<const unsigned char*>(in);
    auto faceDst = static_cast<unsigned char*>(out);

    for (; faces > 0; faces--) {
        int32_t rowsLeft = height - 1;

        for (int32_t y = 0; y < height; y += 4, rowsLeft -= 4) {
            auto block = faceSrc + (y >> 2) * inStride;
            auto dst = faceDst + y * outStride;
            unsigned char* rows[4] = { dst, dst + outStride, dst + outStride * 2, dst + outStride * 3 };
            int32_t colsLeft = faceWidth - 1;

            for (int32_t x = 0; x < faceWidth; x += 4, colsLeft -= 4) {
                uint32_t right = colsLeft < 3 ? colsLeft : 3;
                uint32_t bottom = rowsLeft < 3 ? rowsLeft : 3;
                DXTRECT rect = { 0, 0, right, bottom, right + 1, bottom + 1 };
                decode(block, rows, rect);

                block += BlockBytes;
            }
        }

        faceSrc += ((faceWidth + 3) / 4) * BlockBytes;
        faceDst += faceWidth * TexelBytes;
    }
}

// The reference's dispatch, the same in all seven DXT blitters: the aligned walker when both sides
// are whole blocks, the general one otherwise.
static inline int32_t DxtIsAligned(const C2iVector& size) {
    return size.x > 3 && size.y > 3 && (size.x & 3) == 0 && (size.y & 3) == 0;
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
// One DXT1 block into ARGB8888, over `rect`. Bytes 0..3 are the two endpoints and bytes 4..7 one
// byte of 2-bit indices per row, low bits leftmost.
static void Dxt1DecodeBlock(const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
    uint32_t colors[4];

    Dxt1ExpandColors(block, colors);

    for (uint32_t y = rect.top; y <= rect.bottom; y++) {
        uint32_t bits = block[4 + y] >> (rect.left * 2);
        auto out = reinterpret_cast<uint32_t*>(rows[y]);

        for (uint32_t x = rect.left; x <= rect.right; x++) {
            out[x] = colors[bits & 3];
            bits >>= 2;
        }

        rows[y] += rect.width * 4;
    }
}

// ref: FUN_006ad880
BLIT_NOINLINE void BlitDxt1Argb8888General(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkGeneral<8, 4>(size, in, inStride, out, outStride, Dxt1DecodeBlock);
}

// ref: FUN_006ada10
BLIT_NOINLINE void BlitDxt1Argb8888Aligned(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkAligned<8>(size, in, inStride, out, outStride, Dxt1DecodeBlock);
}

// ref: FUN_006ae500
// The weight tables are a static initialiser in the reference (FUN_006ae820 has no callers);
// frozen builds them on the first DXT blit instead.
void Blit_Dxt1_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (!s_dxtWeightsBuilt) {
        BuildDxtWeights();
    }

    if (DxtIsAligned(size)) {
        BlitDxt1Argb8888Aligned(size, in, inStride, out, outStride);
    } else {
        BlitDxt1Argb8888General(size, in, inStride, out, outStride);
    }
}

// The 5/6/5 blend the DXT1 expanders share, in 5/6/5 space rather than widened to eight bits.
// Both 16-bit targets below want it before their own packing, and both take the weights the same
// way round: colour 2 is two thirds of c0, colour 3 two thirds of c1.
static inline void DxtBlend565(uint32_t r0, uint32_t g0, uint32_t b0,
                               uint32_t r1, uint32_t g1, uint32_t b1,
                               uint32_t& r, uint32_t& g, uint32_t& b) {
    r = (s_dxtWeight2_3[r0] + s_dxtWeight1_3[r1]) >> 8;
    g = (s_dxtWeight2_3[g0] + s_dxtWeight1_3[g1]) >> 8;
    b = (s_dxtWeight2_3[b0] + s_dxtWeight1_3[b1]) >> 8;
}

// ref: FUN_006ac5d0
// A DXT1 block's four colours as RGB565. The two endpoints need no conversion at all -- a DXT1
// endpoint IS an RGB565 value, which is why this expander passes them straight through where the
// ARGB8888 one has to widen them.
//
// Same endpoint-order rule as every DXT1 colour block: c0 > c1 gives four colours, otherwise
// three and a fourth that is zero. Zero in RGB565 is black rather than transparent -- there is no
// alpha bit in this format to carry the distinction, and the reference stores 0 regardless.
static void Dxt1ExpandColorsRgb565(const unsigned char* block, uint16_t colors[4]) {
    uint32_t c0 = static_cast<uint32_t>(block[0]) | (static_cast<uint32_t>(block[1]) << 8);
    uint32_t c1 = static_cast<uint32_t>(block[2]) | (static_cast<uint32_t>(block[3]) << 8);

    colors[0] = static_cast<uint16_t>(c0);
    colors[1] = static_cast<uint16_t>(c1);

    uint32_t r0 = c0 >> 11, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    uint32_t r1 = c1 >> 11, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;

    if (c1 < c0) {
        uint32_t r, g, b;

        DxtBlend565(r0, g0, b0, r1, g1, b1, r, g, b);
        colors[2] = static_cast<uint16_t>((r << 11) | (g << 5) | b);

        DxtBlend565(r1, g1, b1, r0, g0, b0, r, g, b);
        colors[3] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    } else {
        colors[2] = static_cast<uint16_t>((((r0 + r1) / 2) << 11)
                                       | (((g0 + g1) / 2) << 5)
                                       | ((b0 + b1) / 2));
        colors[3] = 0;
    }
}

// ref: FUN_006ac780
// The same four colours as ARGB1555. Two things are worth naming because the reference expresses
// them as bit tricks rather than as what they mean:
//
//   - the alpha bit is set by OR-ing 0x20 into the five-bit RED before the shifts, which carries
//     it to bit 15. Every colour is opaque; DXT1's transparent fourth colour is still just zero.
//   - green loses its LOW bit (`& 0xFFFE` on a six-bit value) rather than being shifted down,
//     because the shift that follows drops it. Six bits of green become five by TRUNCATION.
static void Dxt1ExpandColorsArgb1555(const unsigned char* block, uint16_t colors[4]) {
    uint32_t c0 = static_cast<uint32_t>(block[0]) | (static_cast<uint32_t>(block[1]) << 8);
    uint32_t c1 = static_cast<uint32_t>(block[2]) | (static_cast<uint32_t>(block[3]) << 8);

    uint32_t r0 = c0 >> 11, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    uint32_t r1 = c1 >> 11, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;

    colors[0] = static_cast<uint16_t>(0x8000 | (r0 << 10) | ((g0 >> 1) << 5) | b0);
    colors[1] = static_cast<uint16_t>(0x8000 | (r1 << 10) | ((g1 >> 1) << 5) | b1);

    if (c1 < c0) {
        uint32_t r, g, b;

        DxtBlend565(r0, g0, b0, r1, g1, b1, r, g, b);
        colors[2] = static_cast<uint16_t>(0x8000 | (r << 10) | ((g >> 1) << 5) | b);

        DxtBlend565(r1, g1, b1, r0, g0, b0, r, g, b);
        colors[3] = static_cast<uint16_t>(0x8000 | (r << 10) | ((g >> 1) << 5) | b);
    } else {
        uint32_t r = (r0 + r1) / 2;
        uint32_t g = (g0 + g1) / 2;
        uint32_t b = (b0 + b1) / 2;

        colors[2] = static_cast<uint16_t>(0x8000 | (r << 10) | ((g >> 1) << 5) | b);
        colors[3] = 0;
    }
}

// ref: FUN_006ad220
// One DXT1 block into RGB565, over `rect`.
static void Dxt1DecodeBlockRgb565(const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
    uint16_t colors[4];

    Dxt1ExpandColorsRgb565(block, colors);

    for (uint32_t y = rect.top; y <= rect.bottom; y++) {
        uint32_t bits = block[4 + y] >> (rect.left * 2);
        auto out = reinterpret_cast<uint16_t*>(rows[y]);

        for (uint32_t x = rect.left; x <= rect.right; x++) {
            out[x] = colors[bits & 3];
            bits >>= 2;
        }

        rows[y] += rect.width * 2;
    }
}

// ref: FUN_006ad2d0
// One DXT1 block into ARGB1555, over `rect`.
static void Dxt1DecodeBlockArgb1555(const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
    uint16_t colors[4];

    Dxt1ExpandColorsArgb1555(block, colors);

    for (uint32_t y = rect.top; y <= rect.bottom; y++) {
        uint32_t bits = block[4 + y] >> (rect.left * 2);
        auto out = reinterpret_cast<uint16_t*>(rows[y]);

        for (uint32_t x = rect.left; x <= rect.right; x++) {
            out[x] = colors[bits & 3];
            bits >>= 2;
        }

        rows[y] += rect.width * 2;
    }
}

// ref: FUN_006ad440
BLIT_NOINLINE void BlitDxt1Rgb565General(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkGeneral<8, 2>(size, in, inStride, out, outStride, Dxt1DecodeBlockRgb565);
}

// ref: FUN_006ad5b0
BLIT_NOINLINE void BlitDxt1Rgb565Aligned(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkAligned<8>(size, in, inStride, out, outStride, Dxt1DecodeBlockRgb565);
}

// ref: FUN_006ad660
BLIT_NOINLINE void BlitDxt1Argb1555General(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkGeneral<8, 2>(size, in, inStride, out, outStride, Dxt1DecodeBlockArgb1555);
}

// ref: FUN_006ad7d0
BLIT_NOINLINE void BlitDxt1Argb1555Aligned(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkAligned<8>(size, in, inStride, out, outStride, Dxt1DecodeBlockArgb1555);
}

// ref: FUN_006ae4a0
void Blit_Dxt1_Argb1555(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (!s_dxtWeightsBuilt) {
        BuildDxtWeights();
    }

    if (DxtIsAligned(size)) {
        BlitDxt1Argb1555Aligned(size, in, inStride, out, outStride);
    } else {
        BlitDxt1Argb1555General(size, in, inStride, out, outStride);
    }
}

// ref: FUN_006ae440
void Blit_Dxt1_Rgb565(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (!s_dxtWeightsBuilt) {
        BuildDxtWeights();
    }

    if (DxtIsAligned(size)) {
        BlitDxt1Rgb565Aligned(size, in, inStride, out, outStride);
    } else {
        BlitDxt1Rgb565General(size, in, inStride, out, outStride);
    }
}

// ref: FUN_006ac030
// A DXT1 row of blocks is half a byte a texel times four rows, so two bytes per texel of width.
void Blit_Dxt1_Dxt1(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    int32_t width = std::max(size.x, 4);
    int32_t rows = std::max(size.y >> 2, 1);
    uint32_t rowBytes = width * 2;

    if (inStride == rowBytes && outStride == rowBytes) {
        memcpy(out, in, rows * width * 2);
        return;
    }

    auto in_ = static_cast<const char*>(in);
    auto out_ = static_cast<char*>(out);

    for (; rows; rows--) {
        memcpy(out_, in_, rowBytes);
        in_ += inStride;
        out_ += outStride;
    }
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
// One DXT3 block into ARGB8888, over `rect`. A DXT3 block is 16 bytes: eight of alpha, four bits
// per texel and one uint16 per row, then an eight-byte colour block identical to DXT1's -- which
// is why the colour indices are at +0x0c rather than +0x04.
//
// The alpha goes through `alphaFunc`, which for this target is DxtAlpha4To8, `v << 4`: a fully
// opaque texel comes out 0xF0 rather than 0xFF. That is the reference's and is not tidied.
static void Dxt3DecodeBlock(const unsigned char* block, unsigned char** rows, const DXTRECT& rect, DXTALPHAFUNC alphaFunc) {
    uint32_t colors[4];

    DxtExpandColorsNoAlphaMode(block + 8, colors);

    for (uint32_t y = rect.top; y <= rect.bottom; y++) {
        uint32_t indices = block[0x0C + y] >> (rect.left * 2);
        uint32_t alpha = (static_cast<uint32_t>(block[y * 2])
                       | (static_cast<uint32_t>(block[y * 2 + 1]) << 8)) >> (rect.left * 4);

        auto out = reinterpret_cast<uint32_t*>(rows[y]);

        for (uint32_t x = rect.left; x <= rect.right; x++) {
            uint32_t color = colors[indices & 3];

            out[x] = (color & 0x00FFFFFF) | (alphaFunc(alpha & 0xF) << 24);

            indices >>= 2;
            alpha >>= 4;
        }

        rows[y] += rect.width * 4;
    }
}

// ref: FUN_006add20
BLIT_NOINLINE void BlitDxt3Argb8888General(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkGeneral<16, 4>(size, in, inStride, out, outStride,
        [](const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
            Dxt3DecodeBlock(block, rows, rect, DxtAlpha4To8);
        });
}

// ref: FUN_006adeb0
BLIT_NOINLINE void BlitDxt3Argb8888Aligned(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkAligned<16>(size, in, inStride, out, outStride,
        [](const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
            Dxt3DecodeBlock(block, rows, rect, DxtAlpha4To8);
        });
}

// ref: FUN_006ae5c0
void Blit_Dxt3_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (!s_dxtWeightsBuilt) {
        BuildDxtWeights();
    }

    if (DxtIsAligned(size)) {
        BlitDxt3Argb8888Aligned(size, in, inStride, out, outStride);
    } else {
        BlitDxt3Argb8888General(size, in, inStride, out, outStride);
    }
}

// Defined further down, beside the ARGB8888 DXT5 decoder it was written for.
static void Dxt5ExpandAlpha(unsigned char table[8]);

// A 5/6/5 colour as ARGB4444, opaque. Every channel is TRUNCATED, not rounded -- red and blue
// lose their low bit and green its low two. The reference expresses it as masks applied before a
// shift (`& 0x1E` on red, `& 0xFFFC` on green) rather than as shifts down, which comes to the
// same thing.
//
// The 0xF alpha here is what the reference builds too, and both block writers below then mask it
// straight back off -- they keep only the low twelve bits and supply their own alpha. Transcribed
// rather than dropped because the expander is shared and its value is what the reference stores.
static inline uint16_t Dxt565ToArgb4444(uint32_t r5, uint32_t g6, uint32_t b5) {
    return static_cast<uint16_t>(0xF000 | ((r5 >> 1) << 8) | ((g6 >> 2) << 4) | (b5 >> 1));
}

// ref: FUN_006ac3f0
// A DXT3 or DXT5 colour block's four colours as ARGB4444. Four interpolated colours always, for
// the reason given at DxtExpandColorsNoAlphaMode: these formats carry alpha separately, so the
// endpoint order carries no transparency rule.
static void DxtExpandColorsArgb4444(const unsigned char* block, uint16_t colors[4]) {
    uint32_t c0 = static_cast<uint32_t>(block[0]) | (static_cast<uint32_t>(block[1]) << 8);
    uint32_t c1 = static_cast<uint32_t>(block[2]) | (static_cast<uint32_t>(block[3]) << 8);

    uint32_t r0 = c0 >> 11, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    uint32_t r1 = c1 >> 11, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;

    colors[0] = Dxt565ToArgb4444(r0, g0, b0);
    colors[1] = Dxt565ToArgb4444(r1, g1, b1);

    uint32_t r, g, b;

    DxtBlend565(r0, g0, b0, r1, g1, b1, r, g, b);
    colors[2] = Dxt565ToArgb4444(r, g, b);

    DxtBlend565(r1, g1, b1, r0, g0, b0, r, g, b);
    colors[3] = Dxt565ToArgb4444(r, g, b);
}

// ref: FUN_006acea0
// One DXT3 block into ARGB4444, over `rect`. The colour comes from the table with its alpha
// nibble masked off and the block's own alpha, through `alphaFunc` (DxtAlphaSame here), in its
// place.
static void Dxt3DecodeBlock4444(const unsigned char* block, unsigned char** rows, const DXTRECT& rect, DXTALPHAFUNC alphaFunc) {
    uint16_t colors[4];

    DxtExpandColorsArgb4444(block + 8, colors);

    for (uint32_t y = rect.top; y <= rect.bottom; y++) {
        uint32_t indices = block[0x0C + y] >> (rect.left * 2);
        uint32_t alpha = (static_cast<uint32_t>(block[y * 2])
                       | (static_cast<uint32_t>(block[y * 2 + 1]) << 8)) >> (rect.left * 4);

        auto out = reinterpret_cast<uint16_t*>(rows[y]);

        for (uint32_t x = rect.left; x <= rect.right; x++) {
            out[x] = static_cast<uint16_t>((colors[indices & 3] & 0x0FFF)
                                        | (alphaFunc(alpha & 0xF) << 12));

            indices >>= 2;
            alpha >>= 4;
        }

        rows[y] += rect.width * 2;
    }
}

// ref: FUN_006ad0e0
// One DXT5 block into ARGB4444, over `rect`. The alpha index read is the same straddling
// three-bit read as the ARGB8888 version, and the table it indexes the same eight eight-bit
// values; `alphaFunc` (DxtAlpha8To4 here) narrows the result on the way out.
static void Dxt5DecodeBlock4444(const unsigned char* block, unsigned char** rows, const DXTRECT& rect, DXTALPHAFUNC alphaFunc) {
    uint16_t colors[4];

    DxtExpandColorsArgb4444(block + 8, colors);

    unsigned char alpha[8];

    alpha[0] = block[0];
    alpha[1] = block[1];

    Dxt5ExpandAlpha(alpha);

    for (uint32_t y = rect.top; y <= rect.bottom; y++) {
        uint32_t indices = block[0x0C + y] >> (rect.left * 2);
        auto out = reinterpret_cast<uint16_t*>(rows[y]);

        for (uint32_t x = rect.left; x <= rect.right; x++) {
            uint32_t bitPos = (y * 4 + x) * 3;
            uint32_t byteIdx = bitPos >> 3;
            uint32_t shift = bitPos & 7;

            uint32_t ai = ((static_cast<uint32_t>(block[byteIdx + 2]) >> shift)
                        | (static_cast<uint32_t>(block[byteIdx + 3]) << (8 - shift))) & 7;

            out[x] = static_cast<uint16_t>((colors[indices & 3] & 0x0FFF)
                                        | (alphaFunc(alpha[ai]) << 12));

            indices >>= 2;
        }

        rows[y] += rect.width * 2;
    }
}

// ref: FUN_006adae0
BLIT_NOINLINE void BlitDxt3Argb4444General(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkGeneral<16, 2>(size, in, inStride, out, outStride,
        [](const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
            Dxt3DecodeBlock4444(block, rows, rect, DxtAlphaSame);
        });
}

// ref: FUN_006adc60
BLIT_NOINLINE void BlitDxt3Argb4444Aligned(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkAligned<16>(size, in, inStride, out, outStride,
        [](const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
            Dxt3DecodeBlock4444(block, rows, rect, DxtAlphaSame);
        });
}

// ref: FUN_006ae560
void Blit_Dxt3_Argb4444(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (!s_dxtWeightsBuilt) {
        BuildDxtWeights();
    }

    if (DxtIsAligned(size)) {
        BlitDxt3Argb4444Aligned(size, in, inStride, out, outStride);
    } else {
        BlitDxt3Argb4444General(size, in, inStride, out, outStride);
    }
}

// ref: FUN_006ac0b0
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

// ref: FUN_006abac0
// A DXT5 block's eight alpha values, from the two stored endpoints. Which rule applies depends on
// their ORDER -- the same trick the DXT1 colour block plays: a0 > a1 gives six interpolated
// values, and otherwise four plus the two constants 0 and 255.
//
// The +3 and +2 are the reference's rounding, added before the divide by 7 and by 5. Dropping
// them would bias every interpolated alpha downwards by up to most of a step.
static void Dxt5ExpandAlpha(unsigned char table[8]) {
    uint32_t a0 = table[0];
    uint32_t a1 = table[1];

    if (a1 < a0) {
        table[2] = static_cast<unsigned char>((a0 * 6 + a1 + 3) / 7);
        table[3] = static_cast<unsigned char>((a0 * 5 + a1 * 2 + 3) / 7);
        table[4] = static_cast<unsigned char>((a0 * 4 + a1 * 3 + 3) / 7);
        table[5] = static_cast<unsigned char>((a0 * 3 + a1 * 4 + 3) / 7);
        table[6] = static_cast<unsigned char>((a0 * 2 + a1 * 5 + 3) / 7);
        table[7] = static_cast<unsigned char>((a0 + a1 * 6 + 3) / 7);
    } else {
        table[2] = static_cast<unsigned char>((a0 * 4 + a1 + 2) / 5);
        table[3] = static_cast<unsigned char>((a0 * 3 + a1 * 2 + 2) / 5);
        table[4] = static_cast<unsigned char>((a0 * 2 + a1 * 3 + 2) / 5);
        table[5] = static_cast<unsigned char>((a0 + a1 * 4 + 2) / 5);
        table[6] = 0;
        table[7] = 0xFF;
    }
}

// ref: FUN_006acf90
// One DXT5 block into ARGB8888, over `rect`. Sixteen bytes again, but the alpha half is arranged
// differently from DXT3's: two endpoint BYTES, then sixteen THREE-bit indices packed across the six
// bytes that follow, then the same eight-byte colour block.
//
// Three bits do not divide a byte, so an index can straddle two of them, and the read below is the
// reference's own way of handling that -- take the low part from one byte and the high part from
// the next, then mask. When the index sits entirely in one byte the second term contributes
// nothing, and for the last index the byte it reaches for is the first byte of the colour block,
// which the mask discards. Still in bounds, and deliberate.
//
// `alphaFunc` is DxtAlphaSame for this target: the table is already eight bits.
static void Dxt5DecodeBlock(const unsigned char* block, unsigned char** rows, const DXTRECT& rect, DXTALPHAFUNC alphaFunc) {
    uint32_t colors[4];

    DxtExpandColorsNoAlphaMode(block + 8, colors);

    unsigned char alpha[8];

    alpha[0] = block[0];
    alpha[1] = block[1];

    Dxt5ExpandAlpha(alpha);

    for (uint32_t y = rect.top; y <= rect.bottom; y++) {
        uint32_t indices = block[0x0C + y] >> (rect.left * 2);
        auto out = reinterpret_cast<uint32_t*>(rows[y]);

        for (uint32_t x = rect.left; x <= rect.right; x++) {
            uint32_t bitPos = (y * 4 + x) * 3;
            uint32_t byteIdx = bitPos >> 3;
            uint32_t shift = bitPos & 7;

            uint32_t ai = ((static_cast<uint32_t>(block[byteIdx + 2]) >> shift)
                        | (static_cast<uint32_t>(block[byteIdx + 3]) << (8 - shift))) & 7;

            uint32_t color = colors[indices & 3];

            out[x] = (color & 0x00FFFFFF) | (alphaFunc(alpha[ai]) << 24);

            indices >>= 2;
        }

        rows[y] += rect.width * 4;
    }
}

// ref: FUN_006ae1d0
BLIT_NOINLINE void BlitDxt5Argb8888General(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkGeneral<16, 4>(size, in, inStride, out, outStride,
        [](const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
            Dxt5DecodeBlock(block, rows, rect, DxtAlphaSame);
        });
}

// ref: FUN_006ae360
BLIT_NOINLINE void BlitDxt5Argb8888Aligned(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkAligned<16>(size, in, inStride, out, outStride,
        [](const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
            Dxt5DecodeBlock(block, rows, rect, DxtAlphaSame);
        });
}

// ref: FUN_006adf90
BLIT_NOINLINE void BlitDxt5Argb4444General(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkGeneral<16, 2>(size, in, inStride, out, outStride,
        [](const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
            Dxt5DecodeBlock4444(block, rows, rect, DxtAlpha8To4);
        });
}

// ref: FUN_006ae110
BLIT_NOINLINE void BlitDxt5Argb4444Aligned(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    DxtWalkAligned<16>(size, in, inStride, out, outStride,
        [](const unsigned char* block, unsigned char** rows, const DXTRECT& rect) {
            Dxt5DecodeBlock4444(block, rows, rect, DxtAlpha8To4);
        });
}

// ref: FUN_006ae680
void Blit_Dxt5_Argb8888(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (!s_dxtWeightsBuilt) {
        BuildDxtWeights();
    }

    if (DxtIsAligned(size)) {
        BlitDxt5Argb8888Aligned(size, in, inStride, out, outStride);
    } else {
        BlitDxt5Argb8888General(size, in, inStride, out, outStride);
    }
}

// ref: FUN_006ae620
void Blit_Dxt5_Argb4444(const C2iVector& size, const void* in, uint32_t inStride, void* out, uint32_t outStride) {
    if (!s_dxtWeightsBuilt) {
        BuildDxtWeights();
    }

    if (DxtIsAligned(size)) {
        BlitDxt5Argb4444Aligned(size, in, inStride, out, outStride);
    } else {
        BlitDxt5Argb4444General(size, in, inStride, out, outStride);
    }
}

// Straight copies are shared: every 16-bit format uses Blit_uint16_uint16 and every 32-bit one
// Blit_uint32_uint32, as the reference points those slots at the same two functions.
// ref: FUN_006ae6e0
void InitBlit() {
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Abgr8888]   [BlitAlpha_0]   = &Blit_Argb8888_Abgr8888;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb8888]   [BlitAlpha_0]   = &Blit_uint32;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb8888]   [BlitAlpha_1]   = &Blit_Argb8888_Argb8888_A1;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb8888]   [BlitAlpha_8]   = &Blit_Argb8888_Argb8888_A8;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb4444]   [BlitAlpha_0]   = &Blit_Argb8888_Argb4444;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Argb1555]   [BlitAlpha_0]   = &Blit_Argb8888_Argb1555;
    s_blits [BlitFormat_Argb8888]   [BlitFormat_Rgb565]     [BlitAlpha_0]   = &Blit_Argb8888_Rgb565;
    s_blits [BlitFormat_Rgb565]     [BlitFormat_Rgb565]     [BlitAlpha_0]   = &Blit_uint16;
    s_blits [BlitFormat_Argb4444]   [BlitFormat_Abgr8888]   [BlitAlpha_0]   = &Blit_Argb4444_Abgr8888;
    s_blits [BlitFormat_Argb4444]   [BlitFormat_Argb4444]   [BlitAlpha_0]   = &Blit_uint16;
    s_blits [BlitFormat_Argb1555]   [BlitFormat_Argb1555]   [BlitAlpha_0]   = &Blit_uint16;
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
    s_blits [BlitFormat_Uv88]       [BlitFormat_Uv88]       [BlitAlpha_0]   = &Blit_uint16;
    s_blits [BlitFormat_Gr1616F]    [BlitFormat_Gr1616F]    [BlitAlpha_0]   = &Blit_uint32;
    s_blits [BlitFormat_R32F]       [BlitFormat_R32F]       [BlitAlpha_0]   = &Blit_uint32;
    s_blits [BlitFormat_D24X8]      [BlitFormat_D24X8]      [BlitAlpha_0]   = &Blit_uint32;
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
