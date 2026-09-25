#ifndef GX_TEXTURE_TGA_FILE_HPP
#define GX_TEXTURE_TGA_FILE_HPP

#include <cstdint>

#pragma pack(push, 1)

// The standard 18-byte TGA file header.
struct TgaHeader {
    uint8_t idLength;
    uint8_t colorMapType;
    uint8_t imageType;
    uint16_t colorMapFirst;
    uint16_t colorMapLength;
    uint8_t colorMapEntrySize;
    uint16_t xOrigin;
    uint16_t yOrigin;
    uint16_t width;
    uint16_t height;
    uint8_t pixelDepth;
    uint8_t imageDescriptor;            // low four bits: alpha bits per pixel
};

#pragma pack(pop)

static_assert(sizeof(TgaHeader) == 0x12, "TgaHeader is 18 bytes");

// The reference's TGA image reader. The loader that fills it is not ported; only the fields its
// ported helpers read are laid out, and the offsets in the comments are the reference's.
class TgaFile {
    public:
    // Member variables
    uint32_t m_unk00;
    uint32_t m_unk04;
    TgaHeader m_header;                 // +0x08
    uint8_t m_unk1A[0x3C - 0x1A];
    uint32_t m_imageBytes;              // +0x3C

    // Member functions
    int32_t ColorMapEntryBytes() const;
    int32_t ColorMapBytes() const;
    int32_t ValidateColorDepth() const;
    int32_t ImageDataOffset() const;
    void AddAlphaChannel(uint8_t* dst, const uint8_t* src, const uint8_t* alpha);
};

#endif
