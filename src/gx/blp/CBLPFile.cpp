#include "gx/blp/CBLPFile.hpp"
#include "gx/Blit.hpp"
#include "gx/CGxDevice.hpp"
#include <tempest/Vector.hpp>
#include "gx/Texture.hpp"
#include "util/SFile.hpp"
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <cstring>

TSGrowableArray<unsigned char> CBLPFile::s_blpFileLoadBuffer;

// Floyd-Steinberg error rows for the dithered palette conversions: two rows of 1026 pixels of
// three channels, alternating between the current and the next row.
static int32_t s_ditherErrors1555[2 * 0xC06];  // ref: DAT_00c67580
static int32_t s_ditherErrors565[2 * 0xC06];   // ref: DAT_00c6d5b0
static int32_t s_ditherErrors4444[2 * 0xC06];  // ref: DAT_00c61550
static int32_t s_ditherErrors2565[2 * 0xC06];  // ref: DAT_00c735e0

// Where each texel's two alpha bits live inside its byte of the trailing alpha plane, four
// texels to a byte. Both tables are the reference's own, at 0x00ad9188 (as BYTES -- the dword
// there reads 0xc0300c03) and 0x00ad918c.
static const unsigned char s_alpha2Mask[4] = { 0x03, 0x0C, 0x30, 0xC0 };
static const unsigned char s_alpha2Shift[4] = { 0, 2, 4, 6 };
// And where the two bits come FROM in a 4-bit source plane: the top two of each nibble.
static const unsigned char s_alpha2From4[2] = { 2, 6 };

// PIXEL_FORMAT -> BlitFormat, the reference's own table at 0x00ad91bc. It is not the identity:
// PIXEL_FORMAT orders DXT1 and DXT3 first and puts DXT5 at 7, where BlitFormat groups the three
// DXTs at 6, 7, 8 -- and ARGB1555 and ARGB4444 swap places between the two enums.
//
// The three that map to Unknown are the ones Blit has no converter for: A8, UNSPECIFIED and
// ARGB2565 (whose alpha is a separate plane, so a straight blit could not carry it anyway).
static const BlitFormat s_pixelToBlitFormat[NUM_PIXEL_FORMATS] = {
    BlitFormat_Dxt1,        // PIXEL_DXT1
    BlitFormat_Dxt3,        // PIXEL_DXT3
    BlitFormat_Argb8888,    // PIXEL_ARGB8888
    BlitFormat_Argb1555,    // PIXEL_ARGB1555
    BlitFormat_Argb4444,    // PIXEL_ARGB4444
    BlitFormat_Rgb565,      // PIXEL_RGB565
    BlitFormat_Unknown,     // PIXEL_A8
    BlitFormat_Dxt5,        // PIXEL_DXT5
    BlitFormat_Unknown,     // PIXEL_UNSPECIFIED
    BlitFormat_Unknown      // PIXEL_ARGB2565
};

// ref: FUN_004b58d0
// An empty BLP2 header: version 1, preferred format 2, bit 4 of the mip byte clear.
CBLPFile::CBLPFile() {
    this->m_images = nullptr;
    this->m_quality = 100;
    memset(&this->m_header, 0, sizeof(this->m_header));
    this->m_header.hasMips &= 0xEF;
    this->m_header.magic = 0x32504C42;
    this->m_header.formatVersion = 1;
    this->m_header.preferredFormat = 2;
    this->m_inMemoryImage = nullptr;
    this->m_mipMapAlgorithm = MMA_BOX;
}

// ref: FUN_006ae8b0
void CBLPFile::Close() {
    this->m_inMemoryImage = nullptr;

    if (this->m_images) {
        SMemFree(this->m_images, __FILE__, __LINE__, 0x0);
    }

    this->m_images = nullptr;
}

// ref: FUN_006ae8e0
int32_t CBLPFile::HeaderValid(const BLPHeader* header) {
    return header->magic == 0x32504C42 && header->formatVersion == 1 ? 1 : 0;
}

// ref: FUN_006af660
uint32_t CBLPFile::GetMipWidth(uint32_t mipLevel) {
    uint32_t width = this->m_header.width >> mipLevel;

    return width < 2 ? 1 : width;
}

// ref: FUN_006af680
uint32_t CBLPFile::GetMipHeight(uint32_t mipLevel) {
    uint32_t height = this->m_header.height >> mipLevel;

    return height < 2 ? 1 : height;
}

// ref: FUN_006ae9e0
// Every texel's palette colour, opaque, and then the alpha plane laid over it. The 1-bit plane is
// LSB-first through a { 0x00, 0xff } table, the 4-bit one low nibble first, scaled by 0x11.
void CBLPFile::DecompPalARGB8888(uint32_t* out, const unsigned char* in, uint32_t count) {
    static const uint8_t s_alpha1[2] = { 0x00, 0xff };                  // DAT_00ad90c0
    static const uint8_t s_alpha4[16] = {                               // DAT_00ad90b0
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };

    auto bytes = reinterpret_cast<uint8_t*>(out);
    const BlpPalPixel* palette = this->m_header.extended.palette;

    for (uint32_t i = 0; i < count; i++) {
        memcpy(&out[i], &palette[*in], sizeof(uint32_t));
        bytes[i * 4 + 3] = 0xff;
        in++;
    }

    switch (this->m_header.alphaSize) {
        case 1: {
            uint32_t whole = count >> 3;

            for (uint32_t i = 0; i < whole; i++) {
                uint8_t bits = in[i];

                for (uint32_t bit = 0; bit < 8; bit++) {
                    bytes[(i * 8 + bit) * 4 + 3] = s_alpha1[(bits >> bit) & 1];
                }
            }

            uint32_t rest = count & 7;

            if (rest) {
                uint8_t bits = in[whole];

                for (uint32_t bit = 0; bit < rest; bit++) {
                    bytes[(whole * 8 + bit) * 4 + 3] = s_alpha1[bits & 1];
                    bits >>= 1;
                }
            }

            break;
        }

        case 4: {
            uint32_t i = 0;

            for (; i < (count >> 1); i++) {
                bytes[(i * 2) * 4 + 3] = s_alpha4[in[i] & 0xf];
                bytes[(i * 2 + 1) * 4 + 3] = s_alpha4[in[i] >> 4];
            }

            if (count & 1) {
                bytes[(i * 2) * 4 + 3] = s_alpha4[in[i] & 0xf];
            }

            break;
        }

        case 8:
            for (uint32_t i = 0; i < count; i++) {
                bytes[i * 4 + 3] = in[i];
            }

            break;

        default:
            break;
    }
}

// ref: FUN_006ae990
// Palette indices followed by an 8-bit alpha plane of the same length.
void CBLPFile::DecompPalARGB8888Alpha8(uint32_t* out, const unsigned char* in, uint32_t count) {
    for (uint32_t i = count; i != 0; i--) {
        memcpy(out, &this->m_header.extended.palette[*in], sizeof(uint32_t));
        reinterpret_cast<unsigned char*>(out)[3] = in[count];

        in++;
        out++;
    }
}

// ref: FUN_006aee70
void CBLPFile::DecompPalARGB1555DitherFS(uint16_t* out, const unsigned char* in, uint32_t width, uint32_t height) {
    const BlpPalPixel* palette = this->m_header.extended.palette;
    uint16_t* row = out;

    memset(s_ditherErrors1555, 0, (width * 3 + 6) * 4);

    for (uint32_t y = 0; y < height; y++) {
        int32_t* next = &s_ditherErrors1555[((y - 1) & 1) * 0xC06];

        next[2] = 0;
        next[1] = 0;
        next[0] = 0;
        next[5] = 0;
        next[4] = 0;
        next[3] = 0;

        int32_t* below = next + 1;
        int32_t* cur = &s_ditherErrors1555[(y & 1) * 0xC06 + 6];

        for (uint32_t x = 0; x < width; x++) {
            const BlpPalPixel& color = palette[in[x]];

            int32_t r = color.r * 0x10000 + (cur[-3] >> 4);
            int32_t g = color.g * 0x10000 + (cur[-2] >> 4);
            int32_t b = color.b * 0x10000 + (cur[-1] >> 4);

            uint32_t br = static_cast<uint32_t>(b) + 0x40000;
            uint32_t gr = static_cast<uint32_t>(g) + 0x40000;
            uint32_t rr = static_cast<uint32_t>(r) + 0x40000;

            int32_t r5 = static_cast<int32_t>(rr) >> 19;
            int32_t g5 = static_cast<int32_t>(gr) >> 19;
            int32_t b5 = static_cast<int32_t>(br) >> 19;

            if (r5 < 0) {
                r5 = 0;
            } else if (r5 > 0x1F) {
                r5 = 0x1F;
            }

            if (g5 < 0) {
                g5 = 0;
            } else if (g5 > 0x1F) {
                g5 = 0x1F;
            }

            if (b5 < 0) {
                b5 = 0;
            } else if (b5 > 0x1F) {
                b5 = 0x1F;
            }

            r = static_cast<int32_t>(static_cast<uint32_t>(r) - (rr & 0xFFF80000));
            row[x] = static_cast<uint16_t>(((r5 << 5 | g5) << 5) | b5);
            g = static_cast<int32_t>(static_cast<uint32_t>(g) - (gr & 0xFFF80000));
            b = static_cast<int32_t>(static_cast<uint32_t>(b) - (br & 0xFFF80000));

            cur[0] += r * 7;
            cur[1] += g * 7;
            cur[2] += b * 7;
            below[-1] += r * 5;
            below[0] += g * 5;
            below[1] += b * 5;
            below[2] += r * 3;
            below[3] += g * 3;
            below[4] += b * 3;
            below[6] = g;
            below[7] = b;
            below[5] = r;

            cur += 3;
            below += 3;
        }

        in += width;
        row += width;
    }

    char alphaSize = this->m_header.alphaSize;
    uint32_t count = width * height;
    uint32_t bit = 0;

    if (alphaSize == 1) {
        for (; count != 0; count--) {
            uint8_t shift = static_cast<uint8_t>(bit);
            bit++;

            *out |= static_cast<uint16_t>((static_cast<uint16_t>(1 << (shift & 0x1F)) & static_cast<uint16_t>(*in)) << ((0xF - shift) & 0x1F));

            if (bit > 7) {
                bit = 0;
                in++;
            }

            out++;
        }
    } else if (alphaSize == 4) {
        for (; count != 0; count--) {
            if (bit == 0) {
                *out |= static_cast<uint16_t>((*in & 0xFFF8) << 12);
                bit = static_cast<unsigned char>(this->m_header.alphaSize);
            } else {
                bit = 0;
                *out |= static_cast<uint16_t>((*in & 0x80) << 8);
                in++;
            }

            out++;
        }
    } else if (alphaSize == 8) {
        for (; count != 0; count--) {
            unsigned char a = *in;
            in++;
            *out |= static_cast<uint16_t>((a & 0x80) << 8);
            out++;
        }
    }
}

// ref: FUN_006aeba0
// The palette-to-ARGB4444 conversion, dithered. Structurally the same pass as the 1555 one
// above -- same Floyd-Steinberg 7/5/3 weights, same two alternating error rows, same 16.16
// fixed-point accumulation -- with three differences, all of them just the narrower channel:
// the rounding term is 0x80000 rather than 0x40000, the quantised value comes out at >> 20
// rather than >> 19, and the residual is taken against 0xFFF00000 rather than 0xFFF80000.
//
// The alpha pass afterwards is the part that is NOT shared, because 4444 has a whole nibble of
// alpha where 1555 has one bit. A 1-bit plane expands to 0xF or 0x0, a 4-bit plane goes in as it
// is, and an 8-bit plane keeps only its TOP nibble -- the reference discards the low four bits
// rather than rounding them.
void CBLPFile::DecompPalARGB4444DitherFS(uint16_t* out, const unsigned char* in, uint32_t width, uint32_t height) {
    const BlpPalPixel* palette = this->m_header.extended.palette;
    uint16_t* row = out;

    memset(s_ditherErrors4444, 0, (width * 3 + 6) * 4);

    for (uint32_t y = 0; y < height; y++) {
        int32_t* next = &s_ditherErrors4444[((y - 1) & 1) * 0xC06];

        next[2] = 0;
        next[1] = 0;
        next[0] = 0;
        next[5] = 0;
        next[4] = 0;
        next[3] = 0;

        int32_t* below = next + 1;
        int32_t* cur = &s_ditherErrors4444[(y & 1) * 0xC06 + 6];

        for (uint32_t x = 0; x < width; x++) {
            const BlpPalPixel& color = palette[in[x]];

            int32_t r = color.r * 0x10000 + (cur[-3] >> 4);
            int32_t g = color.g * 0x10000 + (cur[-2] >> 4);
            int32_t b = color.b * 0x10000 + (cur[-1] >> 4);

            uint32_t br = static_cast<uint32_t>(b) + 0x80000;
            uint32_t gr = static_cast<uint32_t>(g) + 0x80000;
            uint32_t rr = static_cast<uint32_t>(r) + 0x80000;

            int32_t r4 = static_cast<int32_t>(rr) >> 20;
            int32_t g4 = static_cast<int32_t>(gr) >> 20;
            int32_t b4 = static_cast<int32_t>(br) >> 20;

            if (r4 < 0) {
                r4 = 0;
            } else if (r4 > 0xF) {
                r4 = 0xF;
            }

            if (g4 < 0) {
                g4 = 0;
            } else if (g4 > 0xF) {
                g4 = 0xF;
            }

            if (b4 < 0) {
                b4 = 0;
            } else if (b4 > 0xF) {
                b4 = 0xF;
            }

            r = static_cast<int32_t>(static_cast<uint32_t>(r) - (rr & 0xFFF00000));
            row[x] = static_cast<uint16_t>(((r4 << 4 | g4) << 4) | b4);
            g = static_cast<int32_t>(static_cast<uint32_t>(g) - (gr & 0xFFF00000));
            b = static_cast<int32_t>(static_cast<uint32_t>(b) - (br & 0xFFF00000));

            cur[0] += r * 7;
            cur[1] += g * 7;
            cur[2] += b * 7;
            below[-1] += r * 5;
            below[0] += g * 5;
            below[1] += b * 5;
            below[2] += r * 3;
            below[3] += g * 3;
            below[4] += b * 3;
            below[6] = g;
            below[7] = b;
            below[5] = r;

            cur += 3;
            below += 3;
        }

        in += width;
        row += width;
    }

    char alphaSize = this->m_header.alphaSize;
    uint32_t count = width * height;

    if (alphaSize == 1) {
        // LSB-first within each byte, and a set bit means opaque -- the same polarity the ARGB8888
        // path uses, here widened to the whole nibble.
        uint32_t bit = 1;

        for (; count != 0; count--) {
            if (*in & bit) {
                *out |= 0xF000;
            }

            bit *= 2;
            out++;

            if (bit > 0xFF) {
                bit = 1;
                in++;
            }
        }
    } else if (alphaSize == 4) {
        // Two texels per byte, low nibble first.
        for (uint32_t i = 0; i < count; i++) {
            if ((i & 1) == 0) {
                *out |= static_cast<uint16_t>(*in << 12);
            } else {
                *out |= static_cast<uint16_t>((*in & 0xF0) << 8);
                in++;
            }

            out++;
        }
    } else if (alphaSize == 8) {
        for (; count != 0; count--) {
            unsigned char a = *in;
            in++;
            *out |= static_cast<uint16_t>((a & 0xF0) << 8);
            out++;
        }
    }
}

// ref: FUN_006af140
void CBLPFile::DecompPalRGB565DitherFS(uint16_t* out, const unsigned char* in, uint32_t width, uint32_t height) {
    const BlpPalPixel* palette = this->m_header.extended.palette;

    memset(s_ditherErrors565, 0, (width * 3 + 6) * 4);

    for (uint32_t y = 0; y < height; y++) {
        int32_t* next = &s_ditherErrors565[((y - 1) & 1) * 0xC06];

        next[2] = 0;
        next[1] = 0;
        next[0] = 0;
        next[5] = 0;
        next[4] = 0;
        next[3] = 0;

        int32_t* below = next + 1;
        int32_t* cur = &s_ditherErrors565[(y & 1) * 0xC06 + 6];

        for (uint32_t x = 0; x < width; x++) {
            const BlpPalPixel& color = palette[in[x]];

            int32_t r = color.r * 0x10000 + (cur[-3] >> 4);
            int32_t g = color.g * 0x10000 + (cur[-2] >> 4);
            int32_t b = color.b * 0x10000 + (cur[-1] >> 4);

            uint32_t br = static_cast<uint32_t>(b) + 0x40000;
            uint32_t gr = static_cast<uint32_t>(g) + 0x20000;
            uint32_t rr = static_cast<uint32_t>(r) + 0x40000;

            int32_t r5 = static_cast<int32_t>(rr) >> 19;
            int32_t g6 = static_cast<int32_t>(gr) >> 18;
            int32_t b5 = static_cast<int32_t>(br) >> 19;

            if (r5 < 0) {
                r5 = 0;
            } else if (r5 > 0x1F) {
                r5 = 0x1F;
            }

            if (g6 < 0) {
                g6 = 0;
            } else if (g6 > 0x3F) {
                g6 = 0x3F;
            }

            if (b5 < 0) {
                b5 = 0;
            } else if (b5 > 0x1F) {
                b5 = 0x1F;
            }

            r = static_cast<int32_t>(static_cast<uint32_t>(r) - (rr & 0xFFF80000));
            out[x] = static_cast<uint16_t>(((r5 << 6 | g6) << 5) | b5);
            g = static_cast<int32_t>(static_cast<uint32_t>(g) - (gr & 0xFFFC0000));
            b = static_cast<int32_t>(static_cast<uint32_t>(b) - (br & 0xFFF80000));

            cur[0] += r * 7;
            cur[1] += g * 7;
            cur[2] += b * 7;
            below[-1] += r * 5;
            below[0] += g * 5;
            below[1] += b * 5;
            below[2] += r * 3;
            below[3] += g * 3;
            below[4] += b * 3;
            below[6] = g;
            below[5] = r;
            below[7] = b;

            cur += 3;
            below += 3;
        }

        in += width;
        out += width;
    }
}

// ref: FUN_006af6a0
uint32_t CBLPFile::GetMipPixelCount(uint32_t mipLevel) {
    uint32_t height = this->m_header.height >> (mipLevel & 0x1F);

    if (height < 2) {
        height = 1;
    }

    uint32_t width = this->m_header.width >> (mipLevel & 0x1F);

    if (width < 2) {
        width = 1;
    }

    return width * height;
}

// ref: FUN_006af730
// Bytes in one mip level and bytes per row of it, for the uncompressed formats.
int32_t CBLPFile::GetMipSize(PIXEL_FORMAT format, uint32_t mipLevel, uint32_t* size, uint32_t* stride) {
    uint32_t width = this->m_header.width >> (mipLevel & 0x1F);

    if (width < 2) {
        width = 1;
    }

    uint32_t height = this->m_header.height >> (mipLevel & 0x1F);

    if (height < 2) {
        height = 1;
    }

    uint32_t pixels = height * width;
    uint32_t alphaBytes = pixels >> 2;

    if (alphaBytes == 0) {
        alphaBytes = 1;
    }

    switch (format) {
        case PIXEL_ARGB8888:
            *size = pixels * 4;
            *stride = width * 4;
            return 1;

        case PIXEL_ARGB1555:
        case PIXEL_ARGB4444:
        case PIXEL_RGB565:
            *size = pixels * 2;
            *stride = width * 2;
            return 1;

        case PIXEL_ARGB2565:
            *size = alphaBytes + pixels * 2;
            *stride = width * 2;
            return 1;

        default:
            *size = 0;
            *stride = 0;
            return 0;
    }
}

// ref: FUN_006af810
// Decode one level of a palettized image into `data` in `format`. The 16-bit targets dither
// (Floyd-Steinberg carries its error along the row and into the next, so they take the level's
// width and height); ARGB8888 takes the alpha plane's own path.
int32_t CBLPFile::DecompPal(PIXEL_FORMAT format, uint32_t mipLevel, unsigned char* data, const unsigned char* in) {
    switch (format) {
        case PIXEL_ARGB8888:
            break;

        case PIXEL_ARGB1555:
            this->DecompPalARGB1555DitherFS(reinterpret_cast<uint16_t*>(data), in, this->GetMipWidth(mipLevel), this->GetMipHeight(mipLevel));
            return 1;

        case PIXEL_ARGB4444:
            this->DecompPalARGB4444DitherFS(reinterpret_cast<uint16_t*>(data), in, this->GetMipWidth(mipLevel), this->GetMipHeight(mipLevel));
            return 1;

        case PIXEL_RGB565:
            this->DecompPalRGB565DitherFS(reinterpret_cast<uint16_t*>(data), in, this->GetMipWidth(mipLevel), this->GetMipHeight(mipLevel));
            return 1;

        case PIXEL_ARGB2565:
            this->DecompPalARGB2565DitherFS(reinterpret_cast<uint16_t*>(data), in, this->GetMipWidth(mipLevel), this->GetMipHeight(mipLevel));
            return 1;

        default:
            return 0;
    }

    uint32_t count = this->GetMipPixelCount(mipLevel);

    if (this->m_header.alphaSize == 8) {
        this->DecompPalARGB8888Alpha8(reinterpret_cast<uint32_t*>(data), in, count);
        return 1;
    }

    this->DecompPalARGB8888(reinterpret_cast<uint32_t*>(data), in, count);

    return 1;
}

// ref: FUN_006af990
// One level, in `format`, without a buffer from the caller: data the file already holds in that
// format is answered in place, and anything decoded goes to a buffer of its own that the caller
// frees through m_lockDecompMem once it has copied it.
int32_t CBLPFile::Lock(PIXEL_FORMAT format, uint32_t mipLevel, unsigned char** data, uint32_t* stride) {
    if (!this->m_inMemoryImage) {
        SErrSetLastError(0x57);
        return 0;
    }

    this->m_lockDecompMem = nullptr;

    if (mipLevel && (!(this->m_header.hasMips & 0xF) || mipLevel >= this->m_numLevels)) {
        return 0;
    }

    unsigned char* mipData = static_cast<unsigned char*>(this->m_inMemoryImage) + this->m_header.mipOffsets[mipLevel];

    if (!this->m_header.mipSizes[mipLevel]) {
        return 0;
    }

    switch (this->m_header.colorEncoding) {
        case COLOR_PAL: {
            uint32_t size;

            if (!this->GetMipSize(format, mipLevel, &size, stride)) {
                return 0;
            }

            *data = static_cast<unsigned char*>(SMemAlloc(size, __FILE__, __LINE__, 0));

            int32_t result = this->DecompPal(format, mipLevel, *data, mipData);

            this->m_lockDecompMem = reinterpret_cast<char*>(*data);

            return result;
        }

        case COLOR_DXT:
            switch (format) {
                case PIXEL_DXT1:
                case PIXEL_DXT3:
                case PIXEL_DXT5:
                    *data = mipData;
                    return 1;

                case PIXEL_ARGB8888:
                case PIXEL_ARGB1555:
                case PIXEL_ARGB4444:
                case PIXEL_RGB565: {
                    uint32_t size;

                    if (!this->GetMipSize(format, mipLevel, &size, stride)) {
                        return 0;
                    }

                    auto preferred = static_cast<uint32_t>(this->m_header.preferredFormat);

                    if (preferred >= NUM_PIXEL_FORMATS) {
                        return 0;
                    }

                    BlitFormat srcFmt = s_pixelToBlitFormat[preferred];
                    BlitFormat dstFmt = s_pixelToBlitFormat[format];

                    *data = static_cast<unsigned char*>(SMemAlloc(size, __FILE__, __LINE__, 0));
                    this->m_lockDecompMem = reinterpret_cast<char*>(*data);

                    uint32_t width = this->m_header.width >> mipLevel;
                    uint32_t height = this->m_header.height >> mipLevel;

                    if (width < 1) {
                        width = 1;
                    }

                    if (height < 1) {
                        height = 1;
                    }

                    uint32_t srcStride = CGxDevice::TexFormatStride(static_cast<EGxTexFormat>(srcFmt), width, height);

                    C2iVector extent = {
                        static_cast<int32_t>(width),
                        static_cast<int32_t>(height)
                    };

                    if (!Blit(extent, BlitAlpha_0, mipData, srcStride, srcFmt, *data, *stride, dstFmt)) {
                        SMemFree(*data, __FILE__, __LINE__, 0);
                        return 0;
                    }

                    return 1;
                }

                default:
                    // The reference answers success without a level for the formats it cannot
                    // convert a DXT image to; LockChain skips a level it is given none for.
                    return 1;
            }

        case COLOR_3:
            *data = mipData;
            return 1;

        default:
            return 0;
    }
}

// ref: FUN_006afb70
// Every level from `mipLevel` down into `images`, allocating the chain when none is given (and
// freeing it again if a level will not lock).
int32_t CBLPFile::LockChain(PIXEL_FORMAT format, MipBits*& images, uint32_t mipLevel) {
    if (mipLevel && (!(this->m_header.hasMips & 0xF) || mipLevel >= this->m_numLevels)) {
        return 0;
    }

    uint32_t height = this->m_header.height >> mipLevel;

    if (height < 2) {
        height = 1;
    }

    uint32_t width = this->m_header.width >> mipLevel;

    if (width < 2) {
        width = 1;
    }

    void** allocated = nullptr;

    if (!images) {
        allocated = AllocMipChain(format, width, height, __FILE__, __LINE__);
        images = reinterpret_cast<MipBits*>(allocated);

        if (!allocated) {
            return 0;
        }
    } else {
        BuildMipLevelPointers(format, width, height, reinterpret_cast<void**>(images));
    }

    for (uint32_t level = mipLevel, i = 0; level < this->m_numLevels; level++, i++) {
        unsigned char* data = nullptr;
        uint32_t stride;

        if (!this->Lock(format, level, &data, &stride)) {
            if (allocated) {
                SMemFree(allocated, __FILE__, __LINE__, 0);
                images = nullptr;
            }

            return 0;
        }

        if (data) {
            size_t size = PixelFormatLevelSize(level, this->m_header.width, this->m_header.height, format);
            memcpy(images->mip[i], data, size);
        }

        if (this->m_lockDecompMem) {
            SMemFree(this->m_lockDecompMem, __FILE__, __LINE__, 0);
        }
    }

    return 1;
}

// ref: FUN_006afce0
int32_t CBLPFile::Lock2(const char* fileName, PIXEL_FORMAT format, uint32_t mipLevel, unsigned char* data, uint32_t& stride) {
    STORM_ASSERT(this->m_inMemoryImage);

    if (mipLevel && (!(this->m_header.hasMips & 0xF) || mipLevel >= this->m_numLevels)) {
        return 0;
    }

    unsigned char* mipData = static_cast<unsigned char*>(this->m_inMemoryImage) + this->m_header.mipOffsets[mipLevel];
    size_t mipSize = this->m_header.mipSizes[mipLevel];

    switch (this->m_header.colorEncoding) {
        case COLOR_PAL: {
            uint32_t size;

            if (!this->GetMipSize(format, mipLevel, &size, &stride)) {
                return 0;
            }

            int32_t result = this->DecompPal(format, mipLevel, data, mipData);
            this->m_lockDecompMem = reinterpret_cast<char*>(data);

            return result;
        }

        case COLOR_DXT:
            switch (format) {
                case PIXEL_DXT1:
                case PIXEL_DXT3:
                case PIXEL_DXT5:
                    memcpy(data, mipData, mipSize);
                    return 1;

                case PIXEL_ARGB8888:
                case PIXEL_ARGB1555:
                case PIXEL_ARGB4444:
                case PIXEL_RGB565: {
                    // The reference converts a compressed mip through Blit (FUN_006af990) rather
                    // than a decoder of its own, and this is that call.
                    //
                    // THE ALLOW-LIST IS NOT CAUTION, it is a correctness requirement. Blit's DXT
                    // converters are mostly still WHOA_UNIMPLEMENTED stubs, and InitBlit registers
                    // them, so the table entry is not null and Blit would call the stub, do
                    // nothing, and answer 1 -- leaving Lock2 to report a decoded mip and the
                    // caller to upload an unwritten buffer. Answering 0 for a pair with no real
                    // converter is a clean failure the caller already handles, so the pairs that
                    // work are named explicitly and the rest still answer 0. Add to the list as
                    // each blitter lands; the seven addresses are recorded in Blit.cpp.
                    uint32_t size;
                    uint32_t dstStride;

                    if (!this->GetMipSize(format, mipLevel, &size, &dstStride)) {
                        return 0;
                    }

                    auto preferred = static_cast<uint32_t>(this->m_header.preferredFormat);

                    if (preferred >= NUM_PIXEL_FORMATS) {
                        return 0;
                    }

                    BlitFormat srcFmt = s_pixelToBlitFormat[preferred];
                    BlitFormat dstFmt = s_pixelToBlitFormat[format];

                    // Every DXT pair the reference converts is implemented now, so the list is
                    // no longer a list: what it still excludes is the pairs the reference has no
                    // converter for either, which Blit itself answers 0 for.
                    bool haveConverter = srcFmt == BlitFormat_Dxt1
                                      || srcFmt == BlitFormat_Dxt3
                                      || srcFmt == BlitFormat_Dxt5;

                    if (!haveConverter) {
                        return 0;
                    }

                    uint32_t width = this->m_header.width >> mipLevel;
                    uint32_t height = this->m_header.height >> mipLevel;

                    if (width < 1) {
                        width = 1;
                    }

                    if (height < 1) {
                        height = 1;
                    }

                    // A row of BLOCKS, which is why it goes through the format-aware helper.
                    uint32_t srcStride = CGxDevice::TexFormatStride(
                        static_cast<EGxTexFormat>(srcFmt), width, height);

                    C2iVector extent = {
                        static_cast<int32_t>(width),
                        static_cast<int32_t>(height)
                    };

                    return Blit(extent, BlitAlpha_0, mipData, srcStride, srcFmt,
                                data, dstStride, dstFmt);
                }

                case PIXEL_ARGB2565:
                    return 0;

                default:
                    return 0;
            }

        case COLOR_3:
            memcpy(data, mipData, mipSize);
            return 1;

        default:
            return 0;
    }
}

// ref: FUN_006affd0
// Decode every level from `mipLevel` down into `images`, allocating the chain when none is
// given. With `inPlace` set, a DXT image wanted as DXT (or an uncompressed one) is not copied
// at all: the chain points into the loaded file, which the BLP then gives up.
int32_t CBLPFile::LockChain2(const char* fileName, PIXEL_FORMAT format, MipBits*& images, uint32_t mipLevel, int32_t inPlace) {
    if (mipLevel && (!(this->m_header.hasMips & 0xF) || mipLevel >= this->m_numLevels)) {
        return 0;
    }

    uint32_t width = this->m_header.width >> mipLevel;

    if (width < 2) {
        width = 1;
    }

    uint32_t height = this->m_header.height >> mipLevel;

    if (height < 2) {
        height = 1;
    }

    if (!images) {
        images = reinterpret_cast<MipBits*>(AllocMipChain(format, width, height, __FILE__, __LINE__));

        if (!images) {
            return 0;
        }
    } else {
        bool decodes = this->m_header.colorEncoding == COLOR_DXT
            && (format == PIXEL_ARGB4444 || format == PIXEL_RGB565 || format == PIXEL_ARGB1555 || format == PIXEL_ARGB8888);

        if (inPlace && (this->m_header.colorEncoding == COLOR_3 || (this->m_header.colorEncoding == COLOR_DXT && !decodes))) {
            for (uint32_t i = 0; this->m_header.mipSizes[i]; i++) {
                images->mip[i] = reinterpret_cast<C4Pixel*>(static_cast<char*>(this->m_inMemoryImage) + this->m_header.mipOffsets[i]);
            }

            this->m_inMemoryImage = nullptr;
            return 1;
        }

        BuildMipLevelPointers(format, width, height, reinterpret_cast<void**>(images));
    }

    for (uint32_t level = mipLevel, i = 0; level < this->m_numLevels; level++, i++) {
        uint32_t stride;

        if (!this->Lock2(fileName, format, level, reinterpret_cast<unsigned char*>(images->mip[i]), stride)) {
            return 0;
        }
    }

    this->m_inMemoryImage = nullptr;

    return 1;
}

// ref: FUN_006aff10
// Read a .blp whole into the shared load buffer and hand that buffer to Source. The buffer is
// static and reused, which is why Source takes the pointer without taking ownership.
int32_t CBLPFile::Open(const char* filename, int32_t a3) {
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(filename);
    STORM_VALIDATE_END;

    this->m_inMemoryImage = nullptr;

    if (this->m_images) {
        SMemFree(this->m_images, __FILE__, __LINE__, 0);
    }

    size_t v8 = a3 != 0;

    this->m_images = nullptr;

    SFile* fileptr;

    if (!SFile::OpenEx(nullptr, filename, v8, &fileptr)) {
        return 0;
    }

    int32_t blpSize = SFile::GetFileSize(fileptr, 0);
    CBLPFile::s_blpFileLoadBuffer.SetCount(blpSize);

    size_t bytesRead;

    SFile::Read(
        fileptr,
        CBLPFile::s_blpFileLoadBuffer.m_data,
        CBLPFile::s_blpFileLoadBuffer.Count(),
        &bytesRead,
        0,
        0
    );

    SFile::Close(fileptr);

    return this->Source(CBLPFile::s_blpFileLoadBuffer.m_data);
}

// ref: FUN_006ae900
// Adopt a BLP that is already in memory: drop whatever was held, take the pointer WITHOUT
// taking ownership (m_inMemoryNeedsFree stays 0), copy the header out of it and work out how
// many mip levels it has.
//
// Pins the tail of the object: m_images at +0x00, and m_inMemoryImage, m_inMemoryNeedsFree and
// m_numLevels at +0x498, +0x49c and +0x4a0 -- the header copy is 0x125 dwords, which is what
// puts them there.
int32_t CBLPFile::Source(void* fileBits) {
    this->m_inMemoryImage = nullptr;

    if (this->m_images) {
        SMemFree(this->m_images, __FILE__, __LINE__, 0);
    }

    this->m_images = nullptr;
    this->m_inMemoryNeedsFree = 0;
    this->m_inMemoryImage = fileBits;

    memcpy(&this->m_header, fileBits, sizeof(this->m_header));

    if (this->m_header.magic != 0x32504C42 || this->m_header.formatVersion != 1) {
        return 0;
    }

    if (this->m_header.hasMips & 0xF) {
        this->m_numLevels = CalcLevelCount(this->m_header.width, this->m_header.height);
    } else {
        this->m_numLevels = 1;
    }

    return 1;
}

// ref: FUN_006af340
// Palette to ARGB2565: RGB565 colour with TWO bits of alpha, and the alpha does not share the
// 16-bit texel -- it goes in a plane of its own after all the colour data, four texels to a byte.
// That is what makes this format the odd one out, and it is visible in GetMipSize, which allots
// `alphaBytes + pixels * 2` for it where every other 16-bit format gets `pixels * 2`.
//
// The colour pass is the 565 one exactly -- 5/6/5, so rounding terms 0x40000, 0x20000, 0x40000,
// shifts 19, 18, 19, and residual masks 0xFFF80000, 0xFFFC0000, 0xFFF80000 -- because 565 and
// 2565 differ only in where the alpha ends up.
void CBLPFile::DecompPalARGB2565DitherFS(uint16_t* out, const unsigned char* in, uint32_t width, uint32_t height) {
    const BlpPalPixel* palette = this->m_header.extended.palette;
    uint16_t* row = out;

    memset(s_ditherErrors2565, 0, (width * 3 + 6) * 4);

    for (uint32_t y = 0; y < height; y++) {
        int32_t* next = &s_ditherErrors2565[((y - 1) & 1) * 0xC06];

        next[2] = 0;
        next[1] = 0;
        next[0] = 0;
        next[5] = 0;
        next[4] = 0;
        next[3] = 0;

        int32_t* below = next + 1;
        int32_t* cur = &s_ditherErrors2565[(y & 1) * 0xC06 + 6];

        for (uint32_t x = 0; x < width; x++) {
            const BlpPalPixel& color = palette[in[x]];

            int32_t r = color.r * 0x10000 + (cur[-3] >> 4);
            int32_t g = color.g * 0x10000 + (cur[-2] >> 4);
            int32_t b = color.b * 0x10000 + (cur[-1] >> 4);

            uint32_t br = static_cast<uint32_t>(b) + 0x40000;
            uint32_t gr = static_cast<uint32_t>(g) + 0x20000;
            uint32_t rr = static_cast<uint32_t>(r) + 0x40000;

            int32_t r5 = static_cast<int32_t>(rr) >> 19;
            int32_t g6 = static_cast<int32_t>(gr) >> 18;
            int32_t b5 = static_cast<int32_t>(br) >> 19;

            if (r5 < 0) {
                r5 = 0;
            } else if (r5 > 0x1F) {
                r5 = 0x1F;
            }

            if (g6 < 0) {
                g6 = 0;
            } else if (g6 > 0x3F) {
                g6 = 0x3F;
            }

            if (b5 < 0) {
                b5 = 0;
            } else if (b5 > 0x1F) {
                b5 = 0x1F;
            }

            r = static_cast<int32_t>(static_cast<uint32_t>(r) - (rr & 0xFFF80000));
            row[x] = static_cast<uint16_t>(((r5 << 6 | g6) << 5) | b5);
            g = static_cast<int32_t>(static_cast<uint32_t>(g) - (gr & 0xFFFC0000));
            b = static_cast<int32_t>(static_cast<uint32_t>(b) - (br & 0xFFF80000));

            cur[0] += r * 7;
            cur[1] += g * 7;
            cur[2] += b * 7;
            below[-1] += r * 5;
            below[0] += g * 5;
            below[1] += b * 5;
            below[2] += r * 3;
            below[3] += g * 3;
            below[4] += b * 3;
            below[6] = g;
            below[7] = b;
            below[5] = r;

            cur += 3;
            below += 3;
        }

        in += width;
        row += width;
    }

    // `row` has walked past every texel, so it is the alpha plane.
    unsigned char* alpha = reinterpret_cast<unsigned char*>(row);
    char alphaSize = this->m_header.alphaSize;
    uint32_t count = width * height;

    // NOTE on all three loops: the value is OR-ed in without being masked to its two bits, which
    // is the reference's own arithmetic (byte registers, `addb %dl,%dl` then `shlb`) and is not a
    // transcription slip. It is safe for a reason worth stating, because it looks wrong: a shift
    // left can only spill into HIGHER fields of the byte, and the fields are written in rising
    // order, so anything that spills is overwritten by the write that owns it. The only bits that
    // keep a spill are those of a final partly-filled byte, which is past the end of the image.
    if (alphaSize == 1) {
        for (uint32_t i = 0; i < count; i++) {
            unsigned char* dst = &alpha[i >> 2];
            unsigned char bits = static_cast<unsigned char>(in[i >> 3] >> (i & 7));

            // One bit becomes 2 rather than 3 -- the reference doubles it and stops there, so a
            // 1-bit opaque texel lands two thirds of the way up the two-bit range.
            *dst = static_cast<unsigned char>(static_cast<unsigned char>(bits + bits)
                                             << s_alpha2Shift[i & 3])
                 | static_cast<unsigned char>(~s_alpha2Mask[i & 3] & *dst);
        }
    } else if (alphaSize == 4) {
        for (uint32_t i = 0; i < count; i++) {
            unsigned char* dst = &alpha[i >> 2];
            unsigned char bits =
                static_cast<unsigned char>(in[i >> 1] >> s_alpha2From4[i & 1]);

            *dst = static_cast<unsigned char>(bits << s_alpha2Shift[i & 3])
                 | static_cast<unsigned char>(~s_alpha2Mask[i & 3] & *dst);
        }
    } else if (alphaSize == 8) {
        for (uint32_t i = 0; i < count; i++) {
            unsigned char* dst = &alpha[i >> 2];
            unsigned char bits = static_cast<unsigned char>(in[i] >> 6);

            *dst = static_cast<unsigned char>(bits << s_alpha2Shift[i & 3])
                 | static_cast<unsigned char>(~s_alpha2Mask[i & 3] & *dst);
        }
    }
}

// ref: FUN_006af6e0
int32_t CBLPFile::Unlock(uint32_t mipLevel) {
    if (this->m_lockDecompMem) {
        SMemFree(this->m_lockDecompMem, __FILE__, __LINE__, 0);
    }

    if (mipLevel && (!(this->m_header.hasMips & 0xF) || mipLevel >= this->m_numLevels)) {
        return 0;
    }

    return 1;
}
