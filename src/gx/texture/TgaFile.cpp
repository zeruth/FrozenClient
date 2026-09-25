#include "gx/texture/TgaFile.hpp"
#include "util/SFile.hpp"
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <cstring>

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

    if (this->m_unk1C) {
        SMemFree(this->m_unk1C, __FILE__, __LINE__, 0);
    }

    this->m_unk1C = nullptr;
    this->m_header.idLength = 0;

    if (this->m_unk40) {
        SMemFree(this->m_unk40, __FILE__, __LINE__, 0);
    }

    this->m_unk40 = nullptr;
}
