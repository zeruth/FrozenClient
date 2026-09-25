#include "world/map/CMapChunk.hpp"
#include "world/map/CMapRenderChunk.hpp"
#include <storm/Error.hpp>
#include <cstring>

// The MCAL and MCSH unpackers (reference Map.cpp, the block behind CMapChunk::UnpackAlphaBits and
// CMapChunk::UnpackAlphaShadowBits). An alpha map is 64x64 (AlphaSize, 0x40 >> the ADT's MAMP
// value): 4-bit nibbles two to a byte in the classic format, one byte per texel in the "big
// alpha" format (MCNK flag 0x8000; MCLY flag 0x200 marks a row-RLE compressed layer); the MCSH
// shadow is one bit per texel. The classic maps carry 63 valid rows and columns and the last is
// duplicated. Every unpacker has a half-size twin (render chunk flag 0x8, every other texel and
// row) for chunks past the alpha distance.

uint8_t CMapChunk::s_alphaRows[4][0x40];    // DAT_00d1ced8: a decompressed row per layer
uint8_t CMapChunk::s_zeroShadowRow[0x40];   // DAT_00d1cfd8: the row of a chunk without MCSH
uint8_t CMapChunk::s_zeroAlphaRow[0x80];    // DAT_00d1d018: the row of a layer without MCAL

static const uint32_t s_nibbleMask[2] = { 0x0f, 0xf0 };     // DAT_00a3fff4
static const uint32_t s_nibbleShift[2] = { 0, 4 };          // DAT_00a40004: down to the value
static const uint32_t s_nibbleUpShift[2] = { 4, 0 };        // DAT_00a3fffc: up to the high nibble
static const uint32_t s_bitMask[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };  // DAT_00a4000c
static const uint32_t s_bitShift[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };                          // DAT_00a4002c
static const uint8_t s_shadowByte[2] = { 0xff, 0x00 };      // DAT_00a4004c: lit, in shadow
static const uint16_t s_shadowTexel[2] = { 0x0000, 0xffff }; // DAT_00aee214

// The nibble of texel i, as a value 0..15
static inline uint32_t Nibble(const uint8_t* row, uint32_t i) {
    return (row[i >> 1] & s_nibbleMask[i & 1]) >> s_nibbleShift[i & 1];
}

// The nibble of texel i, in the high four bits of a byte
static inline uint32_t NibbleHigh(const uint8_t* row, uint32_t i) {
    return static_cast<uint8_t>((row[i >> 1] & s_nibbleMask[i & 1]) << s_nibbleUpShift[i & 1]);
}

// The shadow bit of texel i
static inline uint32_t ShadowBit(const uint8_t* row, uint32_t i) {
    return (row[i >> 3] & s_bitMask[i & 7]) >> s_bitShift[i & 7];
}

// An alpha byte darkened when its shadow bit is set
static inline uint8_t Shade(uint8_t alpha, uint32_t shadowBit) {
    return shadowBit ? static_cast<uint8_t>((alpha * 0xb2) >> 8) : alpha;
}

static inline uint32_t Argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

static inline uint32_t Grey(uint8_t v) {
    return Argb(v, v, v, v);
}

// ref: FUN_007b7420
// One row of a compressed big-alpha layer: runs of a repeated byte (high bit set, count in the
// low seven) or of literal bytes, until count texels are out. Returns where the next row starts.
const uint8_t* CMapChunk::DecompressAlphaRow(uint8_t* dst, int32_t count, const uint8_t* src) {
    int32_t in = 0;
    int32_t out = 0;

    while (out < count) {
        uint8_t code = src[in];
        int32_t next = in + 1;

        if (code & 0x80) {
            in += 2;
            uint32_t run = code & 0x7f;

            if (run) {
                memset(dst + out, src[next], run);
                out += run;
            }
        } else {
            for (uint32_t n = code; n; n--) {
                dst[out++] = src[next];
                next++;
            }

            in = next;
        }
    }

    return src + in;
}

// ref: FUN_007b74a0
// The next row of the shadow and of each of four big-alpha layers: a layer without data reads
// the zero row, a compressed one is decompressed into its scratch row, and every pointer moves
// on. The shadow row is size / 8 bytes, an alpha row size bytes.
void CMapChunk::NextAlphaRows(const uint8_t** shadow, const uint8_t** shadowRow, SMLayerAlpha* layers, uint32_t size, const uint8_t** rows) {
    if (!*shadow) {
        *shadowRow = CMapChunk::s_zeroShadowRow;
    } else {
        *shadowRow = *shadow;
        *shadow += size >> 3;
    }

    for (int32_t i = 0; i < 4; i++) {
        if (!layers[i].alpha) {
            rows[i] = CMapChunk::s_zeroAlphaRow;
        } else if (!(layers[i].flags & 0x200)) {
            rows[i] = layers[i].alpha;
            layers[i].alpha += size;
        } else {
            rows[i] = CMapChunk::s_alphaRows[i];
            layers[i].alpha = CMapChunk::DecompressAlphaRow(CMapChunk::s_alphaRows[i], size, layers[i].alpha);
        }
    }
}

// ref: FUN_007b7530
// The nibble-format twin of NextAlphaRows: an alpha row is size / 2 bytes and never compressed
void CMapChunk::NextNibbleRows(uint32_t size, const uint8_t** shadow, const uint8_t** shadowRow, SMLayerAlpha* layers, const uint8_t** rows) {
    if (!*shadow) {
        *shadowRow = CMapChunk::s_zeroShadowRow;
    } else {
        *shadowRow = *shadow;
        *shadow += size >> 3;
    }

    for (int32_t i = 0; i < 4; i++) {
        if (!layers[i].alpha) {
            rows[i] = CMapChunk::s_zeroAlphaRow;
        } else {
            rows[i] = layers[i].alpha;
            layers[i].alpha += size >> 1;
        }
    }
}

// ref: FUN_007b7860
// The MCLY flags and MCAL pointer of each layer a render chunk draws from its chunk
void CMapChunk::GatherLayerAlphas(const CMapRenderChunk* renderChunk, const CMapChunk* chunk, SMLayerAlpha* out) {
    for (uint32_t i = 0; i < 4; i++) {
        out[i].alpha = nullptr;

        if (renderChunk->m_layerCount > i) {
            const SMLayer* layer = &chunk->m_layers[i];

            if (layer->flags & 0x100) {
                out[i].alpha = chunk->m_alpha + layer->offsetInMCAL;
            }

            out[i].flags = layer->flags;
        }
    }
}

// ref: FUN_007b84a0
// One row of the packed 4444 alpha: shadow in A, layers 1..3 in R, G, B, the last column
// repeated
void CMapChunk::PackNibbleRow(uint16_t* dst, int32_t count, const uint8_t** rows, const uint8_t* shadowRow) {
    if (count == 1) {
        return;
    }

    uint16_t texel = 0;
    uint32_t x = 0;

    for (; x < static_cast<uint32_t>(count - 1); x++) {
        texel = static_cast<uint16_t>(
            ((s_shadowByte[ShadowBit(shadowRow, x)] & 0xf0) << 8)
            | ((NibbleHigh(rows[1], x) & 0xf0) << 4)
            | (NibbleHigh(rows[2], x) & 0xf0)
            | (NibbleHigh(rows[3], x) >> 4));
        dst[x] = texel;
    }

    dst[x] = texel;
}

// ---- One layer's alpha map (UnpackAlphaBits) -------------------------------------------------

// ref: FUN_007b75b0
// Big-alpha chunk, nibble source, 4444 output: the nibble in A over white
static void UnpackAlpha4(uint16_t* dst, uint32_t size, const SMLayerAlpha* layer) {
    const uint8_t* src = layer->alpha;
    uint32_t i = 0;

    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            *dst++ = static_cast<uint16_t>((Nibble(src, i) << 12) | 0xfff);
            i++;
        }
    }
}

// ref: FUN_007b7620
// The half-size twin of UnpackAlpha4. The stride arithmetic, including the skip of 0x42 texels
// on the fourth-last row, is the reference's.
static void UnpackAlpha4Half(uint16_t* dst, uint32_t size, const SMLayerAlpha* layer) {
    const uint8_t* src = layer->alpha;
    uint32_t i = 0;

    for (uint32_t y = 0; y < size; y += 2) {
        if (size != 2) {
            for (uint32_t n = ((size - 3) >> 1) + 1; n; n--) {
                *dst++ = static_cast<uint16_t>((Nibble(src, i) << 12) | 0xfff);
                i += 2;
            }
        }

        uint32_t next = i + 2;
        *dst++ = static_cast<uint16_t>((Nibble(src, i + 1) << 12) | 0xfff);

        if (y == size - 4) {
            next = i + 0x42;
        }

        i = next + size;
    }
}

// ref: FUN_007b76f0
// Classic chunk, nibble source, 4444 output: 63 valid columns and rows, the last of each repeated
static void UnpackAlpha4Edge(uint16_t* dst, uint32_t size, const SMLayerAlpha* layer) {
    const uint8_t* src = layer->alpha;
    uint32_t i = 0;
    uint16_t texel = 0xfff;
    uint32_t n = size - 1;

    for (uint32_t y = 0; y < n; y++) {
        for (uint32_t x = 0; x < n; x++) {
            texel = static_cast<uint16_t>(((Nibble(src, i) & 0xf) << 12) | 0xfff);
            *dst++ = texel;
            i++;
        }

        *dst++ = texel;
        i++;
    }

    i -= size;

    for (uint32_t x = 0; x < n; x++) {
        texel = static_cast<uint16_t>(((Nibble(src, i) & 0xf) << 12) | 0xfff);
        *dst++ = texel;
        i++;
    }

    *dst = texel;
}

// ref: FUN_007b77d0
// The half-size twin of UnpackAlpha4Edge: every other texel of every other row, no repeat
static void UnpackAlpha4EdgeHalf(uint16_t* dst, uint32_t size, const SMLayerAlpha* layer) {
    const uint8_t* src = layer->alpha;
    uint32_t i = 0;

    if (size == 1) {
        return;
    }

    for (uint32_t rows = ((size - 2) >> 1) + 1; rows; rows--) {
        for (uint32_t n = ((size - 2) >> 1) + 1; n; n--) {
            *dst++ = static_cast<uint16_t>((Nibble(src, i) << 12) | 0xfff);
            i += 2;
        }

        i += size;
    }
}

// The next row of one big-alpha layer, decompressed when the layer is compressed
static const uint8_t* NextAlphaRow(SMLayerAlpha* layer, uint32_t size) {
    if (!(layer->flags & 0x200)) {
        const uint8_t* row = layer->alpha;
        layer->alpha += size;
        return row;
    }

    layer->alpha = CMapChunk::DecompressAlphaRow(CMapChunk::s_alphaRows[0], size, layer->alpha);
    return CMapChunk::s_alphaRows[0];
}

// The next shadow row, or the zero row without MCSH
static const uint8_t* NextShadowRow(const uint8_t** shadow, uint32_t size) {
    if (!*shadow) {
        return CMapChunk::s_zeroShadowRow;
    }

    const uint8_t* row = *shadow;
    *shadow += size >> 3;
    return row;
}

// ref: FUN_007b88d0
// Big-alpha chunk, byte source, 8888 output: the alpha darkened by the shadow bit, in every channel
static void UnpackAlpha8Shadow(uint32_t* dst, uint32_t size, SMLayerAlpha* layer, const uint8_t* shadow) {
    for (uint32_t y = 0; y < size; y++) {
        const uint8_t* shadowRow = NextShadowRow(&shadow, size);
        const uint8_t* row = NextAlphaRow(layer, size);

        for (uint32_t x = 0; x < size; x++) {
            *dst++ = Grey(Shade(row[x], ShadowBit(shadowRow, x)));
        }
    }
}

// ref: FUN_007b89c0
// The half-size twin of UnpackAlpha8Shadow
static void UnpackAlpha8ShadowHalf(uint32_t* dst, uint32_t size, SMLayerAlpha* layer, const uint8_t* shadow) {
    for (uint32_t y = 0; y < size; y += 2) {
        const uint8_t* shadowRow = NextShadowRow(&shadow, size);
        const uint8_t* row = NextAlphaRow(layer, size);
        uint32_t x = 0;

        if (size != 2) {
            for (; x < size - 2; x += 2) {
                *dst++ = Grey(Shade(row[x], ShadowBit(shadowRow, x)));
            }
        }

        *dst++ = Grey(Shade(row[x + 1], ShadowBit(shadowRow, x + 1)));

        if (y < size - 2) {
            NextShadowRow(&shadow, size);
            NextAlphaRow(layer, size);
        }

        if (y == size - 4) {
            NextShadowRow(&shadow, size);
            NextAlphaRow(layer, size);
        }
    }
}

// ref: FUN_007b8b80
// Big-alpha chunk, 8888 output for the base layer, which has no map of its own: whatever the
// other three layers leave of 255, darkened by the shadow bit
static void UnpackAlpha8Base(const CMapRenderChunk* renderChunk, uint32_t* dst, uint32_t size, const uint8_t* shadow) {
    SMLayerAlpha layers[4] = {};
    CMapChunk::GatherLayerAlphas(renderChunk, renderChunk->m_chunk, layers);

    for (uint32_t y = 0; y < size; y++) {
        const uint8_t* rows[4];
        const uint8_t* shadowRow;
        CMapChunk::NextAlphaRows(&shadow, &shadowRow, layers, size, rows);

        for (uint32_t x = 0; x < size; x++) {
            uint32_t a = ((0xff - rows[1][x]) - rows[3][x]) - rows[2][x];

            if (ShadowBit(shadowRow, x)) {
                a = (a * 0xb2) >> 8;
            }

            *dst++ = Grey(static_cast<uint8_t>(a));
        }
    }
}

// ref: FUN_007b8c70
// The half-size twin of UnpackAlpha8Base
static void UnpackAlpha8BaseHalf(const CMapRenderChunk* renderChunk, uint32_t* dst, uint32_t size, const uint8_t* shadow) {
    SMLayerAlpha layers[4] = {};
    CMapChunk::GatherLayerAlphas(renderChunk, renderChunk->m_chunk, layers);

    for (uint32_t y = 0; y < size; y += 2) {
        const uint8_t* rows[4];
        const uint8_t* shadowRow;
        CMapChunk::NextAlphaRows(&shadow, &shadowRow, layers, size, rows);
        uint32_t x = 0;

        if (size != 2) {
            for (; x < size - 2; x += 2) {
                uint32_t a = ((0xff - rows[1][x]) - rows[3][x]) - rows[2][x];

                if (ShadowBit(shadowRow, x)) {
                    a = (a * 0xb2) >> 8;
                }

                *dst++ = Grey(static_cast<uint8_t>(a));
            }
        }

        uint32_t last = x + 1;
        uint32_t a = ((0xff - rows[1][last]) - rows[2][last]) - rows[3][last];

        if (ShadowBit(shadowRow, last)) {
            a = (a * 0xb2) >> 8;
        }

        *dst++ = Grey(static_cast<uint8_t>(a));

        if (y < size - 2) {
            CMapChunk::NextAlphaRows(&shadow, &shadowRow, layers, size, rows);
        }

        if (y == size - 4) {
            CMapChunk::NextAlphaRows(&shadow, &shadowRow, layers, size, rows);
        }
    }
}

// ref: FUN_007b8e20
// One layer's alpha map into the blend buffer, by the chunk's alpha format, the texture format
// asked for (3 = 4444, 2 = 8888) and the render chunk's half-size flag. The branches sit in the
// order the reference's code lays them out.
void CMapChunk::UnpackAlphaBits(const CMapRenderChunk* renderChunk, void* dst, uint32_t size, SMLayerAlpha* layer, const uint8_t* shadow, int32_t genFormat, uint32_t bigAlpha) {
    bool half = (renderChunk->m_flags10 & 0x8) != 0;

    if (bigAlpha) {
        if (genFormat == 3) {
            if (!half) {
                UnpackAlpha4(static_cast<uint16_t*>(dst), size, layer);
            } else {
                UnpackAlpha4Half(static_cast<uint16_t*>(dst), size, layer);
            }
            return;
        }

        if (genFormat == 2) {
            if (!layer->alpha) {
                if (!half) {
                    UnpackAlpha8Base(renderChunk, static_cast<uint32_t*>(dst), size, shadow);
                } else {
                    UnpackAlpha8BaseHalf(renderChunk, static_cast<uint32_t*>(dst), size, shadow);
                }
                return;
            }

            if (!half) {
                UnpackAlpha8Shadow(static_cast<uint32_t*>(dst), size, layer, shadow);
            } else {
                UnpackAlpha8ShadowHalf(static_cast<uint32_t*>(dst), size, layer, shadow);
            }
            return;
        }
    } else if (genFormat == 3) {
        if (!half) {
            UnpackAlpha4Edge(static_cast<uint16_t*>(dst), size, layer);
        } else {
            UnpackAlpha4EdgeHalf(static_cast<uint16_t*>(dst), size, layer);
        }
        return;
    }

    SErrDisplayAppFatal("CMapChunk::UnpackAlphaBits(): Bad genformat.");
}

// ---- The packed alpha map (UnpackAlphaShadowBits) --------------------------------------------

// ref: FUN_007b85a0
// Classic chunk, 4444 output: shadow and layers 1..3 packed per texel, the last row and column
// repeated
static void UnpackPacked4Edge(uint16_t* dst, uint32_t offset, uint32_t pitch, uint32_t size, SMLayerAlpha* layers, const uint8_t* shadow) {
    dst += offset;

    const uint8_t* rows[4] = { CMapChunk::s_zeroAlphaRow, CMapChunk::s_zeroAlphaRow, CMapChunk::s_zeroAlphaRow, CMapChunk::s_zeroAlphaRow };
    const uint8_t* shadowRow = CMapChunk::s_zeroShadowRow;

    for (uint32_t y = 0; y < size - 1; y++) {
        CMapChunk::NextNibbleRows(size, &shadow, &shadowRow, layers, rows);
        CMapChunk::PackNibbleRow(dst, size, rows, shadowRow);
        dst += pitch;
    }

    CMapChunk::PackNibbleRow(dst, size, rows, shadowRow);
}

// Moves the shadow and the four nibble layers past one row
static void SkipNibbleRow(uint32_t size, const uint8_t** shadow, SMLayerAlpha* layers) {
    if (*shadow) {
        *shadow += size >> 3;
    }

    for (int32_t i = 0; i < 4; i++) {
        if (layers[i].alpha) {
            layers[i].alpha += size >> 1;
        }
    }
}

// The 4444 packed texel of column x
static inline uint16_t Packed4(const uint8_t** rows, const uint8_t* shadowRow, uint32_t x) {
    return static_cast<uint16_t>(
        ((NibbleHigh(rows[1], x) & 0xf0) << 4)
        | (NibbleHigh(rows[2], x) & 0xf0)
        | ((s_shadowByte[ShadowBit(shadowRow, x)] & 0xf0) << 8)
        | (NibbleHigh(rows[3], x) >> 4));
}

// ref: FUN_007b8620
// The half-size twin of UnpackPacked4Edge
static void UnpackPacked4EdgeHalf(uint16_t* dst, uint32_t offset, uint32_t pitch, uint32_t size, SMLayerAlpha* layers, const uint8_t* shadow) {
    dst += offset;

    if (size == 1) {
        return;
    }

    const uint8_t* rows[4];
    const uint8_t* shadowRow;
    uint32_t y = 0;

    for (uint32_t n = ((size - 2) >> 1) + 1; n; n--) {
        CMapChunk::NextNibbleRows(size, &shadow, &shadowRow, layers, rows);
        uint32_t x = 0;

        if (size != 1) {
            for (uint32_t out = 0; x < size - 1; x += 2, out++) {
                dst[out] = Packed4(rows, shadowRow, x);
            }
        }

        SkipNibbleRow(size, &shadow, layers);
        dst += pitch;
        y += 2;
    }
}

// The 8888 packed texel of column x: shadow in A, layers 1..3 in R, G, B; the base layer's
// channel, when it is not layer 0, holds what the four rows leave of 255
static inline uint32_t Packed8(const uint8_t** rows, const uint8_t* shadowRow, uint32_t x, uint32_t baseLayer) {
    uint32_t v[4] = { rows[0][x], rows[1][x], rows[2][x], rows[3][x] };

    if (baseLayer) {
        v[baseLayer] = (((0xff - rows[3][x]) - rows[2][x]) - rows[1][x]) - rows[0][x];
    }

    return Argb(s_shadowByte[ShadowBit(shadowRow, x)], static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2]), static_cast<uint8_t>(v[3]));
}

// ref: FUN_007b7c60
// Big-alpha chunk, 8888 output
static void UnpackPacked8(uint32_t* dst, uint32_t offset, uint32_t pitch, uint32_t size, SMLayerAlpha* layers, uint32_t baseLayer, const uint8_t* shadow) {
    dst += offset;

    for (uint32_t y = 0; y < size; y++) {
        const uint8_t* rows[4];
        const uint8_t* shadowRow;
        CMapChunk::NextAlphaRows(&shadow, &shadowRow, layers, size, rows);

        for (uint32_t x = 0; x < size; x++) {
            dst[x] = Packed8(rows, shadowRow, x, baseLayer);
        }

        dst += pitch;
    }
}

// ref: FUN_007b7dc0
// The half-size twin of UnpackPacked8
static void UnpackPacked8Half(uint32_t* dst, uint32_t offset, uint32_t pitch, uint32_t size, SMLayerAlpha* layers, uint32_t baseLayer, const uint8_t* shadow) {
    dst += offset;

    for (uint32_t y = 0; y < size; y += 2) {
        const uint8_t* rows[4];
        const uint8_t* shadowRow;
        CMapChunk::NextAlphaRows(&shadow, &shadowRow, layers, size, rows);
        uint32_t x = 0;
        uint32_t out = 0;

        if (size != 2) {
            for (; x < size - 2; x += 2, out++) {
                dst[out] = Packed8(rows, shadowRow, x, baseLayer);
            }
        }

        dst[out] = Packed8(rows, shadowRow, x + 1, baseLayer);

        if (y < size - 2) {
            CMapChunk::NextAlphaRows(&shadow, &shadowRow, layers, size, rows);
        }

        if (y == size - 4) {
            CMapChunk::NextAlphaRows(&shadow, &shadowRow, layers, size, rows);
        }

        dst += pitch;
    }
}

// ref: FUN_007b8070
// Big-alpha chunk, nibble source, 4444 output
static void UnpackPacked4(uint16_t* dst, uint32_t offset, uint32_t pitch, uint32_t size, SMLayerAlpha* layers, const uint8_t* shadow) {
    dst += offset;

    for (uint32_t y = 0; y < size; y++) {
        const uint8_t* rows[4];
        const uint8_t* shadowRow;
        CMapChunk::NextNibbleRows(size, &shadow, &shadowRow, layers, rows);

        for (uint32_t x = 0; x < size; x++) {
            dst[x] = Packed4(rows, shadowRow, x);
        }

        dst += pitch;
    }
}

// ref: FUN_007b8190
// The half-size twin of UnpackPacked4
static void UnpackPacked4Half(uint16_t* dst, uint32_t offset, uint32_t pitch, uint32_t size, SMLayerAlpha* layers, const uint8_t* shadow) {
    dst += offset;

    for (uint32_t y = 0; y < size; y += 2) {
        const uint8_t* rows[4];
        const uint8_t* shadowRow;
        CMapChunk::NextNibbleRows(size, &shadow, &shadowRow, layers, rows);
        uint32_t x = 0;
        uint32_t out = 0;

        if (size != 2) {
            for (; x < size - 2; x += 2, out++) {
                dst[out] = Packed4(rows, shadowRow, x);
            }
        }

        dst[out] = Packed4(rows, shadowRow, x + 1);

        SkipNibbleRow(size, &shadow, layers);

        if (y == size - 4) {
            SkipNibbleRow(size, &shadow, layers);
        }

        dst += pitch;
    }
}

// ref: FUN_007b87f0
// The packed alpha map of one chunk into the blend buffer at a texel offset with a texel pitch
void CMapChunk::UnpackAlphaShadowBits(const CMapRenderChunk* renderChunk, void* dst, uint32_t offset, uint32_t pitch, uint32_t size, SMLayerAlpha* layers, uint32_t baseLayer, const uint8_t* shadow, int32_t genFormat, uint32_t bigAlpha) {
    bool half = (renderChunk->m_flags10 & 0x8) != 0;

    if (bigAlpha) {
        if (genFormat == 3) {
            if (!half) {
                UnpackPacked4(static_cast<uint16_t*>(dst), offset, pitch, size, layers, shadow);
            } else {
                UnpackPacked4Half(static_cast<uint16_t*>(dst), offset, pitch, size, layers, shadow);
            }
            return;
        }

        if (!half) {
            UnpackPacked8(static_cast<uint32_t*>(dst), offset, pitch, size, layers, baseLayer, shadow);
        } else {
            UnpackPacked8Half(static_cast<uint32_t*>(dst), offset, pitch, size, layers, baseLayer, shadow);
        }
        return;
    }

    if (genFormat == 3) {
        if (!half) {
            UnpackPacked4Edge(static_cast<uint16_t*>(dst), offset, pitch, size, layers, shadow);
        } else {
            UnpackPacked4EdgeHalf(static_cast<uint16_t*>(dst), offset, pitch, size, layers, shadow);
        }
        return;
    }

    SErrDisplayAppFatal("CMapChunk::UnpackAlphaShadowBits(): Bad genformat.");
}

// ---- The shadow map on its own ---------------------------------------------------------------

// ref: FUN_007b7930
// The MCSH bits as 4444 texels: white where the bit is set
void CMapChunk::UnpackShadowBits(uint16_t* dst, uint32_t size, const uint8_t* shadow) {
    uint32_t i = 0;

    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            *dst++ = s_shadowTexel[ShadowBit(shadow, i)];
            i++;
        }
    }
}

// ref: FUN_007b79a0
// The half-size twin of UnpackShadowBits, with the reference's row arithmetic
void CMapChunk::UnpackShadowBitsHalf(uint16_t* dst, uint32_t size, const uint8_t* shadow) {
    uint32_t i = 0;

    for (uint32_t y = 0; y < size; y += 2) {
        if (size != 2) {
            for (uint32_t n = ((size - 3) >> 1) + 1; n; n--) {
                *dst++ = s_shadowTexel[ShadowBit(shadow, i)];
                i += 2;
            }
        }

        uint32_t next = i + 2;
        *dst++ = s_shadowTexel[ShadowBit(shadow, i + 1)];

        if (y == size - 4) {
            next += size;
        }

        i = next + size;
    }
}
