#include "gx/texture/TgaFile.hpp"
#include <storm/String.hpp>
#include <cstdio>
#include "util/SFile.hpp"
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <cstring>

// ref: FUN_006aafb0
// Everything in front of the pixel data: the 18-byte header, the optional ID field and the
// colour map, in that order, because each is positional -- the file offset after the header is
// only right once the ID field has been consumed. Closes first, so reopening an already-open
// TgaFile does not leak the previous file's buffers.
//
// The two fixups at the end are the interesting part. A header can lie about its alpha, and the
// reference CORRECTS it in place and carries on with a warning rather than rejecting the file:
// 24 bits per pixel cannot also carry 8 bits of alpha, and 32 always does. Each sets its own
// Storm error code, which a caller can read but nothing has to.
//
// No allocation here is null-checked, and that is transcribed rather than hardened: Storm's
// allocator does not hand back null, and adding a branch the reference does not have would
// change the call sequence for a case that cannot arise.
int32_t TgaFile::Open(const char* fileName, int32_t mustExist) {
    if (!fileName || !*fileName) {
        // 0x57 is ERROR_INVALID_PARAMETER.
        SErrSetLastError(0x57);

        return 0;
    }

    this->Close();

    if (!SFile::OpenEx(nullptr, fileName, mustExist != 0, &this->m_file)) {
        return 0;
    }

    if (!SFile::Read(this->m_file, &this->m_header, sizeof(TgaHeader), nullptr, nullptr, nullptr)) {
        return 0;
    }

    if (!this->m_header.idLength) {
        this->m_idField = nullptr;
    } else {
        this->m_idField = SMemAlloc(this->m_header.idLength, __FILE__, __LINE__, 0);

        if (!SFile::Read(this->m_file, this->m_idField, this->m_header.idLength,
                        nullptr, nullptr, nullptr)) {
            return 0;
        }
    }

    if (!this->m_header.colorMapType) {
        this->m_colorMap = nullptr;
    } else {
        this->m_colorMap = SMemAlloc(this->ColorMapBytes(), __FILE__, __LINE__, 0);

        if (!SFile::Read(this->m_file, this->m_colorMap, this->ColorMapBytes(),
                        nullptr, nullptr, nullptr)) {
            return 0;
        }
    }

    if (this->m_header.pixelDepth == 24 && (this->m_header.imageDescriptor & 0xF) == 8) {
        this->m_header.imageDescriptor &= 0xF0;
        SErrSetLastError(0x8720012E);
    }

    if (this->m_header.pixelDepth == 32 && (this->m_header.imageDescriptor & 0xF) == 0) {
        this->m_header.imageDescriptor = (this->m_header.imageDescriptor & 0xF8) | 8;
        SErrSetLastError(0x8720012F);
    }

    return 1;
}

// ref: FUN_006aa350
int32_t TgaFile::ColorMapEntryBytes() const {
    uint32_t channelBits = this->m_header.colorMapEntrySize / 3;

    if (channelBits > 7) {
        channelBits = 8;
    }

    return static_cast<int32_t>(channelBits * 3) >> 3;
}

// ref: FUN_006aa380
int32_t TgaFile::ColorMapBytes() const {
    return this->ColorMapEntryBytes() * this->m_header.colorMapLength;
}

// Only 24 bits of colour per pixel, besides any alpha bits, are accepted.
// ref: FUN_006aa3b0
int32_t TgaFile::ValidateColorDepth() const {
    if (static_cast<uint32_t>(this->m_header.pixelDepth) - (this->m_header.imageDescriptor & 0xF) == 24) {
        return 1;
    }

    SErrSetLastError((this->m_header.pixelDepth != 16) + 0xF720007C);

    return 0;
}

// ref: FUN_006aa3e0
int32_t TgaFile::ImageDataOffset() const {
    return this->ColorMapBytes() + this->m_header.idLength + sizeof(TgaHeader);
}

// Widens every pixel by one alpha byte: from `alpha` when given, else opaque.
// ref: FUN_006aa420
void TgaFile::AddAlphaChannel(uint8_t* dst, const uint8_t* src, const uint8_t* alpha) {
    int32_t count = this->m_header.height * this->m_header.width;

    if (!alpha) {
        for (; count != 0; count--) {
            memmove(dst, src, (this->m_header.pixelDepth + 7) >> 3);
            int32_t pixelBytes = (this->m_header.pixelDepth + 7) >> 3;
            src += pixelBytes;
            dst[pixelBytes] = 0xFF;
            dst += pixelBytes + 1;
        }
    } else {
        for (; count != 0; count--) {
            memmove(dst, src, (this->m_header.pixelDepth + 7) >> 3);
            int32_t pixelBytes = (this->m_header.pixelDepth + 7) >> 3;
            src += pixelBytes;
            dst[pixelBytes] = *alpha;
            alpha++;
            dst += pixelBytes + 1;
        }
    }

    auto descriptor = this->m_header.imageDescriptor;
    this->m_header.imageDescriptor = (descriptor & 0xF8) | 8;
    this->m_header.pixelDepth += 8 - (descriptor & 0xF);

    this->m_imageBytes = ((this->m_header.pixelDepth + 7) >> 3) * this->m_header.height * this->m_header.width;
}

// Expands run-length packets into raw pixels, then turns the image type into its unpacked form.
// ref: FUN_006aa630
int32_t TgaFile::DecodeRle(const uint8_t* src, uint8_t* dst) {
    int32_t remaining = this->m_header.height * this->m_header.width;
    size_t pixelBytes = (this->m_header.pixelDepth + 7) >> 3;

    do {
        if (remaining == 0) {
            this->m_header.imageType -= 8;
            return 1;
        }

        uint8_t packet = *src++;
        int32_t count;
        size_t size;

        if (packet & 0x80) {
            count = (packet & 0x7F) + 1;

            for (int32_t i = count; i != 0; i--) {
                memcpy(dst, src, pixelBytes);
                dst += pixelBytes;
            }

            size = pixelBytes;
        } else {
            count = packet + 1;
            size = count * pixelBytes;
            memcpy(dst, src, size);
            dst += size;
        }

        remaining -= count;
        src += size;
    } while (remaining >= 0);

    SErrSetLastError(0xF7200077);

    return 0;
}

// ref: FUN_006aa820
uint8_t* TgaFile::GetImage() const {
    if (!this->m_image) {
        SErrSetLastError(0xF720007F);
        return nullptr;
    }

    return this->m_image;
}

// ref: FUN_006aa840
// Two different refusals, and they carry different Storm error codes, which is the only way a
// caller can tell "nothing loaded" from "loaded but not 32-bit". The first code is the same one
// GetImage uses for an absent image.
uint8_t* TgaFile::GetImage32() const {
    if (!this->m_image) {
        SErrSetLastError(0xF720007F);

        return nullptr;
    }

    if (this->m_header.pixelDepth != 32) {
        SErrSetLastError(0xF720007D);

        return nullptr;
    }

    return this->m_image;
}

// ref: FUN_006aa700
// Bit 5 of the image descriptor is TGA's vertical origin: set means the rows are stored top
// down. This makes that bit agree with `topDown`, reversing the row order if it does not
// already, and returns 1 for "the image is now the way you asked" -- including the case where
// it already was and nothing happened.
//
// Image types 9, 10 and 11 are the RLE-compressed ones, and those are refused rather than
// flipped: the rows are not addressable until DecodeRle has run, so there is nothing to reverse.
//
// The copy goes forwards through the source and backwards through the destination one row at a
// time, into a fresh buffer, rather than swapping in place -- so a row's bytes keep their order
// and only the rows move.
int32_t TgaFile::SetTopDown(int32_t topDown) {
    bool isTopDown = (this->m_header.imageDescriptor & 0x20) != 0;

    if (isTopDown == (topDown != 0)) {
        return 1;
    }

    if (this->m_header.imageType > 8 && this->m_header.imageType < 12) {
        SErrSetLastError(0xF7200083);

        return 0;
    }

    uint32_t bytesPerPixel = (this->m_header.pixelDepth + 7) >> 3;
    uint32_t rowBytes = bytesPerPixel * this->m_header.width;
    uint32_t height = this->m_header.height;

    auto flipped = static_cast<uint8_t*>(SMemAlloc(rowBytes * height, __FILE__, __LINE__, 0));

    const uint8_t* src = this->m_image;
    uint8_t* dst = flipped + (height - 1) * rowBytes;

    for (uint32_t row = 0; row < height; row++) {
        memcpy(dst, src, rowBytes);

        src += rowBytes;
        dst -= rowBytes;
    }

    SMemFree(this->m_image, __FILE__, __LINE__, 0);

    if (topDown) {
        this->m_header.imageDescriptor |= 0x20;
    } else {
        this->m_header.imageDescriptor &= ~0x20;
    }

    this->m_image = flipped;

    return 1;
}

// ref: FUN_006aaf40
void TgaFile::Close() {
    if (this->m_image) {
        SMemFree(this->m_image, __FILE__, __LINE__, 0);
    }

    this->m_image = nullptr;

    if (this->m_file) {
        SFile::Close(this->m_file);
    }

    this->m_file = nullptr;

    if (this->m_idField) {
        SMemFree(this->m_idField, __FILE__, __LINE__, 0);
    }

    this->m_idField = nullptr;
    this->m_header.idLength = 0;

    if (this->m_colorMap) {
        SMemFree(this->m_colorMap, __FILE__, __LINE__, 0);
    }

    this->m_colorMap = nullptr;
}

// ref: FUN_006aa520
int32_t TgaFile::ReadUncompressed(uint32_t flags) {
    uint32_t addAlpha = (flags & 1) && (this->m_header.imageDescriptor & 0xF) == 0 ? 1 : 0;

    if (SFile::SetFilePointer(this->m_file, this->ImageDataOffset(), nullptr, 0) == 0xFFFFFFFF) {
        return 0;
    }

    uint32_t pixels = this->m_header.width * this->m_header.height;
    uint32_t bytes = ((this->m_header.pixelDepth + 7) >> 3) * pixels;

    this->m_image = static_cast<uint8_t*>(SMemAlloc(pixels * addAlpha + bytes, __FILE__, __LINE__, 0));

    if (!this->m_image) {
        return 0;
    }

    if (!SFile::Read(this->m_file, this->m_image + pixels * addAlpha, bytes, nullptr, nullptr, nullptr)) {
        return 0;
    }

    if (addAlpha) {
        this->AddAlphaChannel(this->m_image, this->m_image + pixels, nullptr);
    }

    return 1;
}

// ref: FUN_006ab220
// The packed data is read whole into a scratch buffer and decoded into the image, leaving room
// in front when an alpha byte is to be added. The reference frees the scratch buffer only on
// the paths that reach the decode.
int32_t TgaFile::ReadRle(uint32_t flags) {
    uint32_t addAlpha = (flags & 1) && (this->m_header.imageDescriptor & 0xF) == 0 ? 1 : 0;
    uint32_t pixels = this->m_header.width * this->m_header.height;

    this->m_image = static_cast<uint8_t*>(SMemAlloc(
        pixels * addAlpha + ((this->m_header.pixelDepth + 7) >> 3) * pixels, __FILE__, __LINE__, 0));

    if (!this->m_image) {
        return 0;
    }

    int32_t dataBytes = static_cast<int32_t>(SFile::GetFileSize(this->m_file, nullptr))
        - (this->m_header.idLength + static_cast<int32_t>(sizeof(TgaHeader)) + this->ColorMapBytes());

    if (dataBytes == -1) {
        return 0;
    }

    auto data = static_cast<uint8_t*>(SMemAlloc(dataBytes, __FILE__, __LINE__, 0));

    if (!data) {
        return 0;
    }

    if (SFile::SetFilePointer(this->m_file, this->ImageDataOffset(), nullptr, 0) == 0xFFFFFFFF) {
        return 0;
    }

    if (!SFile::Read(this->m_file, data, dataBytes, nullptr, nullptr, nullptr)) {
        return 0;
    }

    int32_t decoded = this->DecodeRle(data, this->m_image + pixels * addAlpha);
    SMemFree(data, __FILE__, __LINE__, 0);

    if (!decoded) {
        return 0;
    }

    if (addAlpha) {
        this->AddAlphaChannel(this->m_image, this->m_image + pixels, nullptr);
    }

    return 1;
}

// ref: FUN_006ab0e0
// Looks every index up in the colour map, then becomes an uncompressed true-colour image.
void TgaFile::ExpandColorMap(uint32_t flags) {
    auto indices = this->m_image;
    uint32_t pixels = this->m_header.height * this->m_header.width;
    uint32_t alphaBits = this->m_header.imageDescriptor & 0xF;
    uint32_t depth = alphaBits + 24;
    uint32_t addAlpha = flags & 1;

    auto image = static_cast<uint8_t*>(SMemAlloc((depth >> 3) * pixels + pixels * addAlpha, __FILE__, __LINE__, 0));
    auto dst = image + pixels * addAlpha;
    this->m_image = image;

    auto colorMap = static_cast<const uint8_t*>(this->m_colorMap);

    for (uint32_t i = 0; i < pixels; i++) {
        memcpy(dst, colorMap + this->ColorMapEntryBytes() * (indices[i] - this->m_header.colorMapFirst), this->ColorMapEntryBytes());
        dst += this->ColorMapEntryBytes();
    }

    SMemFree(indices, __FILE__, __LINE__, 0);
    SMemFree(this->m_colorMap, __FILE__, __LINE__, 0);
    this->m_colorMap = nullptr;

    this->m_header.colorMapLength = 0;
    this->m_header.colorMapType = 0;
    this->m_header.imageType = 2;
    this->m_header.pixelDepth = static_cast<uint8_t>(depth);
    this->m_imageBytes = ((alphaBits + 0x1F) >> 3) * this->m_header.width * this->m_header.height;

    if (addAlpha) {
        this->AddAlphaChannel(this->m_image, this->m_image + this->m_header.width * this->m_header.height, nullptr);
    }
}

// ref: FUN_006ab450
int32_t TgaFile::ReadColorMapped(uint32_t flags) {
    if (!this->m_header.colorMapType) {
        SErrSetLastError(0xF7200084);
        return 0;
    }

    int32_t result = this->m_header.imageType < 9 ? this->ReadUncompressed(0) : this->ReadRle(0);

    if (this->m_header.colorMapType && (flags & 2)) {
        this->ExpandColorMap(flags);
    }

    return result;
}

// ref: FUN_006ab4b0
// Black-and-white images (types 3 and 11) and anything unknown are refused, each with its own
// Storm error. An image already read is left as it is.
int32_t TgaFile::ReadImage(uint32_t flags) {
    if (this->m_image) {
        SErrSetLastError(0x57);
        return 1;
    }

    if (!this->m_file) {
        SErrSetLastError(0xF720007E);
        return 0;
    }

    this->m_imageBytes = ((this->m_header.pixelDepth + 7) >> 3) * this->m_header.height * this->m_header.width;

    switch (this->m_header.imageType) {
        case 0:
            SErrSetLastError(0xF7200078);
            return 0;

        case 1:
        case 9:
            return this->ReadColorMapped(flags);

        case 2:
            if (this->ValidateColorDepth()) {
                return this->ReadUncompressed(flags);
            }

            return 0;

        case 3:
        case 11:
            SErrSetLastError(0xF720007A);
            return 0;

        case 10:
            if (this->ValidateColorDepth()) {
                return this->ReadRle(flags);
            }

            return 0;

        default:
            SErrSetLastError(0xF720007B);
            return 0;
    }
}

// ref: FUN_006aa870
// A header that claims alpha bits other than 8 is corrected to its colour depth less 8 first,
// with a warning, and is then refused unless that came to 8.
int32_t TgaFile::RemoveAlphaChannel() {
    if (!this->m_image) {
        SErrSetLastError(0xF720007F);
        return 0;
    }

    if ((this->m_header.imageDescriptor & 0xF) == 0) {
        if (this->m_header.pixelDepth == 24) {
            return 1;
        }

        SErrSetLastError(0x8720012D);
        this->m_header.imageDescriptor = (this->m_header.imageDescriptor & 0xF0) | ((this->m_header.pixelDepth - 8) & 0xF);
    }

    if ((this->m_header.imageDescriptor & 0xF) != 8) {
        SErrSetLastError(0xF7200082);
        return 0;
    }

    this->m_header.pixelDepth -= 8;
    this->m_header.imageDescriptor &= 0xF0;

    auto dst = this->m_image;
    auto src = this->m_image;

    for (uint32_t count = this->m_header.height * this->m_header.width; count != 0; count--) {
        size_t bytes = (this->m_header.pixelDepth + 7) >> 3;
        memmove(dst, src, bytes);
        dst += bytes;
        src += bytes + 1;
    }

    return 1;
}

// ref: FUN_006ab390
int32_t TgaFile::AddAlpha(const uint8_t* alpha) {
    if (!this->m_image) {
        SErrSetLastError(0xF720007F);
        return 0;
    }

    if (this->m_header.imageType > 8 && this->m_header.imageType < 12) {
        SErrSetLastError(0xF7200083);
        return 0;
    }

    if (this->m_header.imageDescriptor & 0xF) {
        this->RemoveAlphaChannel();
    }

    auto image = static_cast<uint8_t*>(SMemAlloc(
        (((this->m_header.pixelDepth + 7) >> 3) + 1) * this->m_header.height * this->m_header.width, __FILE__, __LINE__, 0));

    if (!image) {
        return 0;
    }

    this->AddAlphaChannel(image, this->m_image, alpha);
    SMemFree(this->m_image, __FILE__, __LINE__, 0);
    this->m_image = image;

    return 1;
}

// ref: FUN_006ab860
void TgaDownsample(uint32_t* dst, uint32_t dstWidth, uint32_t dstHeight, const uint8_t* src, uint32_t srcWidth, uint32_t srcHeight) {
    uint32_t blockWidth = srcWidth / dstWidth;
    uint32_t blockHeight = srcHeight / dstHeight;

    for (uint32_t y = 0; y < dstHeight; y++) {
        for (uint32_t x = 0; x < dstWidth; x++) {
            int64_t sumR = 0, sumG = 0, sumB = 0;
            int64_t sumRA = 0, sumGA = 0, sumBA = 0;
            int64_t sumA = 0;
            int64_t count = 0;

            auto row = src;

            for (uint32_t by = 0; by < blockHeight; by++) {
                auto pixel = row;

                for (uint32_t bx = 0; bx < blockWidth; bx++) {
                    uint32_t b = pixel[0];
                    uint32_t g = pixel[1];
                    uint32_t r = pixel[2];
                    uint32_t a = pixel[3];

                    sumA += a;
                    sumRA += r * a;
                    sumGA += g * a;
                    sumBA += b * a;
                    count++;
                    sumR += r;
                    sumG += g;
                    sumB += b;

                    pixel += 4;
                }

                row += srcWidth * 4;
            }

            uint32_t value;

            if (sumA == 0) {
                value = (static_cast<uint8_t>(sumR / count) << 16)
                      | (static_cast<uint8_t>(sumG / count) << 8)
                      | static_cast<uint8_t>(sumB / count);
            } else {
                value = (static_cast<uint32_t>(static_cast<uint8_t>(sumA / static_cast<int64_t>(blockHeight * blockWidth))) << 24)
                      | (static_cast<uint8_t>(sumRA / sumA) << 16)
                      | (static_cast<uint8_t>(sumGA / sumA) << 8)
                      | static_cast<uint8_t>(sumBA / sumA);
            }

            *dst++ = value;
            src += blockWidth * 4;
        }

        src += (blockHeight - 1) * srcWidth * 4;
    }
}

// ref: FUN_006aa950
int32_t TgaFile::SetImage(const void* image, uint16_t width, uint16_t height, uint8_t depth, uint8_t alphaBits,
                          int32_t topDown, int32_t rightToLeft) {
    if (!image || (depth != 32 && depth != 24) || (alphaBits != 0 && alphaBits != 8)) {
        SErrSetLastError(0x57);

        return 0;
    }

    uint8_t descriptor = this->m_header.imageDescriptor;
    memset(&this->m_header, 0, sizeof(this->m_header));

    this->m_header.height = height;
    this->m_header.imageDescriptor = static_cast<uint8_t>(
        ((topDown ? 2 : 0) | (rightToLeft ? 1 : 0)) << 4 | (descriptor & 0xc0) | (alphaBits & 0xf)
    );
    this->m_header.pixelDepth = depth;
    this->m_header.width = width;
    this->m_imageBytes = ((depth + 7) >> 3) * static_cast<uint32_t>(height) * static_cast<uint32_t>(width);
    this->m_header.imageType = 2;

    if (this->m_image) {
        SMemFree(this->m_image, __FILE__, __LINE__, 0x0);
    }

    this->m_image = static_cast<uint8_t*>(SMemAlloc(this->m_imageBytes, __FILE__, __LINE__, 0x0));

    if (!this->m_image) {
        return 0;
    }

    memcpy(this->m_image, image, this->m_imageBytes);

    this->m_footer.extensionOffset = 0;
    this->m_footer.developerOffset = 0;
    SStrCopy(this->m_footer.signature, "TRUEVISION-XFILE.", sizeof(this->m_footer.signature));

    return 1;
}

// ref: FUN_006aaa70
int32_t TgaFile::RunLength(const uint8_t* pixel, int32_t count) const {
    uint32_t pixelBytes = (this->m_header.pixelDepth + 7) >> 3;

    uint8_t first[4] = { 0, 0, 0, 0 };
    memcpy(first, pixel, pixelBytes);

    if (count > 0x80) {
        count = 0x80;
    }

    int32_t run = 0;

    while (count > 0) {
        count--;

        if (memcmp(pixel, first, pixelBytes) != 0) {
            return run;
        }

        pixel += pixelBytes;
        run++;
    }

    return run;
}

// ref: FUN_006aab70
int32_t TgaFile::EncodeRow(const uint8_t** src, uint8_t** dst) {
    const uint8_t* in = *src;
    uint8_t* out = *dst;
    int32_t remaining = this->m_header.width;
    uint8_t* literal = nullptr;

    uint32_t pixelBytes = (this->m_header.pixelDepth + 7) >> 3;
    uint32_t limit = static_cast<uint32_t>(this->m_header.height) * this->m_header.width * pixelBytes;

    while (remaining) {
        int32_t run = this->RunLength(in, remaining);

        if (run < 2) {
            // A literal packet: open one, or grow the open one until it holds 128 pixels.
            uint8_t* pixelOut;

            if (!literal || *literal == 0x7f) {
                *out = 0;
                literal = out;
                pixelOut = out + 1;
                this->m_imageBytes++;
            } else {
                (*literal)++;
                pixelOut = out;
            }

            this->m_imageBytes += pixelBytes;

            if (limit <= this->m_imageBytes) {
                return 0;
            }

            memcpy(pixelOut, in, pixelBytes);

            out = pixelOut + pixelBytes;
            in += pixelBytes;
            remaining--;
        } else {
            this->m_imageBytes += pixelBytes + 1;
            literal = nullptr;

            if (limit <= this->m_imageBytes) {
                return 0;
            }

            *out = static_cast<uint8_t>((run - 1) | 0x80);
            memcpy(out + 1, in, pixelBytes);

            out += 1 + pixelBytes;
            in += pixelBytes * run;
            remaining -= run;
        }
    }

    *src = in;
    *dst = out;

    return 1;
}

// ref: FUN_006aace0
int32_t TgaFile::CompressRle() {
    const uint8_t* src = this->m_image;

    if (!src) {
        SErrSetLastError(0xf7200081);

        return 0;
    }

    if (this->m_header.imageType > 8) {
        SErrSetLastError(0xf7200083);

        return 0;
    }

    auto encoded = static_cast<uint8_t*>(SMemAlloc(this->m_imageBytes, __FILE__, __LINE__, 0x0));

    if (!encoded) {
        return 0;
    }

    this->m_imageBytes = 0;
    uint8_t* dst = encoded;

    for (uint32_t row = this->m_header.height; row; row--) {
        if (!this->EncodeRow(&src, &dst)) {
            // It will not shrink: keep the image uncompressed.
            SMemFree(encoded, __FILE__, __LINE__, 0x0);
            this->m_imageBytes = ((this->m_header.pixelDepth + 7) >> 3) * this->m_header.width * this->m_header.height;

            return 1;
        }
    }

    this->m_header.imageType += 8;

    SMemFree(this->m_image, __FILE__, __LINE__, 0x0);
    this->m_image = encoded;

    return 1;
}

// ref: FUN_006aade0
// DIVERGED in the file calls only: the reference goes through its own Os file wrappers
// (FUN_00461fa0 / 461b90 / 461b00 / 461ce0); frozen uses the C library.
int32_t TgaFile::Write(const char* fileName) {
    if (!fileName) {
        SErrSetLastError(0x57);

        return 0;
    }

    if (!this->m_image) {
        SErrSetLastError(0xf7200081);

        return 0;
    }

    FILE* file = fopen(fileName, "wb");

    if (!file) {
        return 0;
    }

    int32_t result = 0;

    do {
        if (fwrite(&this->m_header, 1, sizeof(this->m_header), file) != sizeof(this->m_header)) {
            break;
        }

        if (this->m_header.idLength
            && fwrite(this->m_idField, 1, this->m_header.idLength, file) != this->m_header.idLength) {
            break;
        }

        if (this->m_header.colorMapType) {
            auto bytes = static_cast<size_t>(this->ColorMapBytes());

            if (fwrite(this->m_colorMap, 1, bytes, file) != bytes) {
                break;
            }
        }

        if (fwrite(this->m_image, 1, this->m_imageBytes, file) != this->m_imageBytes) {
            break;
        }

        if (fwrite(&this->m_footer, 1, sizeof(this->m_footer), file) != sizeof(this->m_footer)) {
            break;
        }

        result = 1;
    } while (false);

    fclose(file);

    if (!result) {
        remove(fileName);
    }

    return result;
}
