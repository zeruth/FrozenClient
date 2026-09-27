#include "gx/texture/TgaFile.hpp"
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
