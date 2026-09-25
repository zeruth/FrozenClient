#include "gx/blp/CBLPFile.hpp"
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

int32_t CBLPFile::Lock2(const char* fileName, PIXEL_FORMAT format, uint32_t mipLevel, unsigned char* data, uint32_t& stride) {
    STORM_ASSERT(this->m_inMemoryImage);

    if (mipLevel && (!(this->m_header.hasMips & 0xF) || mipLevel >= this->m_numLevels)) {
        return 0;
    }

    unsigned char* mipData = static_cast<unsigned char*>(this->m_inMemoryImage) + this->m_header.mipOffsets[mipLevel];
    size_t mipSize = this->m_header.mipSizes[mipLevel];

    switch (this->m_header.colorEncoding) {
        case COLOR_PAL: {
            // Palettized images are expanded to ARGB8888: one palette index per texel followed
            // by an alpha plane of 0, 1, 4, or 8 bits per texel.
            if (format != PIXEL_ARGB8888) {
                // TODO conversion to 16 bit formats
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

            uint32_t texelCount = width * height;
            const unsigned char* indices = mipData;
            const unsigned char* alpha = mipData + texelCount;
            const BlpPalPixel* palette = this->m_header.extended.palette;

            if (mipSize < texelCount) {
                return 0;
            }

            for (uint32_t i = 0; i < texelCount; i++) {
                const BlpPalPixel& color = palette[indices[i]];

                unsigned char a;

                switch (this->m_header.alphaSize) {
                    case 1:
                        a = (alpha[i >> 3] >> (i & 7)) & 1 ? 0xFF : 0x00;
                        break;

                    case 4:
                        a = ((alpha[i >> 1] >> ((i & 1) * 4)) & 0xF) * 0x11;
                        break;

                    case 8:
                        a = alpha[i];
                        break;

                    default:
                        a = 0xFF;
                        break;
                }

                data[i * 4 + 0] = color.b;
                data[i * 4 + 1] = color.g;
                data[i * 4 + 2] = color.r;
                data[i * 4 + 3] = a;
            }

            return 1;
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
                case PIXEL_RGB565:
                    // TODO
                    return 0;

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

int32_t CBLPFile::LockChain2(const char* fileName, PIXEL_FORMAT format, MipBits*& images, uint32_t mipLevel, int32_t a6) {
    if (mipLevel && (!(this->m_header.hasMips & 0xF) || mipLevel >= this->m_numLevels)) {
        return 0;
    }

    if (images) {
        if (a6 && (this->m_header.colorEncoding == COLOR_DXT || this->m_header.colorEncoding == COLOR_3)) {
            if (this->m_header.colorEncoding == COLOR_3 || (format != PIXEL_ARGB4444 && format != PIXEL_RGB565 && format != PIXEL_ARGB1555 && format != PIXEL_ARGB8888)) {
                uint32_t* offset = this->m_header.mipOffsets;

                for (int32_t i = 0; *offset; offset++, i++) {
                    void* address = static_cast<char*>(this->m_inMemoryImage) + *offset;
                    MipBits* image = static_cast<MipBits*>(address);
                    reinterpret_cast<MipBits**>(images)[i] = image;
                }

                this->m_inMemoryImage = nullptr;
                return 1;
            }
        }

        uint32_t v13 = this->m_header.height >> mipLevel;

        if (v13 <= 1) {
            v13 = 1;
        }

        uint32_t v14 = this->m_header.width >> mipLevel;

        if (v14 <= 1) {
            v14 = 1;
        }

        MippedImgSet(images, format, v14, v13);
    } else {
        uint32_t v9 = this->m_header.height >> mipLevel;

        if (v9 <= 1) {
            v9 = 1;
        }

        uint32_t v10 = this->m_header.width >> mipLevel;

        if (v10 <= 1) {
            v10 = 1;
        }

        images = MippedImgAllocA(format, v10, v9, __FILE__, __LINE__);

        if (!images) {
            return 0;
        }
    }

    MipBits** ptr = reinterpret_cast<MipBits**>(images);

    for (int32_t level = mipLevel, i = 0; level < this->m_numLevels; level++, i++) {
        if (!this->Lock2(fileName, format, level, reinterpret_cast<unsigned char*>(ptr[i]), mipLevel)) {
            return 0;
        }
    }

    this->m_inMemoryImage = nullptr;

    return 1;
}

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
