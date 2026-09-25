#ifndef GX_TEXTURE_TGA_FILE_HPP
#define GX_TEXTURE_TGA_FILE_HPP

#include <cstdint>

class SFile;

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
    SFile* m_file;                      // +0x00
    uint8_t* m_image;                   // +0x04
    TgaHeader m_header;                 // +0x08
    uint8_t m_unk1A[0x1C - 0x1A];
    void* m_unk1C;                      // +0x1C
    uint8_t m_unk20[0x3C - 0x20];
    uint32_t m_imageBytes;              // +0x3C
    void* m_unk40;                      // +0x40

    // Member functions
    int32_t ColorMapEntryBytes() const;
    int32_t ColorMapBytes() const;
    int32_t ValidateColorDepth() const;
    int32_t ImageDataOffset() const;
    void AddAlphaChannel(uint8_t* dst, const uint8_t* src, const uint8_t* alpha);
    int32_t DecodeRle(const uint8_t* src, uint8_t* dst);
    uint8_t* GetImage() const;
    void Close();
};

#endif
