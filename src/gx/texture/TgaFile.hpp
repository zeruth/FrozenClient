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

// The 26-byte TGA 2.0 footer: the extension and developer area offsets, then the signature.
struct TgaFooter {
    uint32_t extensionOffset;
    uint32_t developerOffset;
    char signature[18];
};

#pragma pack(pop)

static_assert(sizeof(TgaHeader) == 0x12, "TgaHeader is 18 bytes");
static_assert(sizeof(TgaFooter) == 0x1a, "TgaFooter is 26 bytes");

// The reference's TGA image reader. The offsets in the comments are the reference's.
//
// Open is what fills it, and porting it (2026-09-26) is what named the two owned buffers: the
// optional ID field and the colour map, both allocated there and both freed by Close, which is
// the cross-check that they are owned rather than borrowed. The two unnamed gaps that remain are
// genuinely unread -- nothing ported touches them.
class TgaFile {
    public:
    // Member variables
    SFile* m_file;                      // +0x00
    uint8_t* m_image;                   // +0x04
    TgaHeader m_header;                 // +0x08
    uint8_t m_unk1A[0x1C - 0x1A];
    // +0x1C: the header's optional ID field, `idLength` bytes of it, read straight after the
    // header. Nothing interprets it; it is read so the file position lands on the colour map.
    void* m_idField;
    // +0x20: the footer a written file ends with (SetImage fills it).
    TgaFooter m_footer;
    uint8_t m_unk3A[0x3C - 0x3A];
    uint32_t m_imageBytes;              // +0x3C
    // +0x40: the colour map, ColorMapBytes() of it, present only when colorMapType is non-zero.
    void* m_colorMap;

    // Member functions
    // Open a .tga and read everything in front of the pixels. The image itself is NOT read
    // here -- GetImage and DecodeRle do that on demand.
    int32_t Open(const char* fileName, int32_t mustExist);

    int32_t ColorMapEntryBytes() const;
    int32_t ColorMapBytes() const;
    int32_t ValidateColorDepth() const;
    int32_t ImageDataOffset() const;
    void AddAlphaChannel(uint8_t* dst, const uint8_t* src, const uint8_t* alpha);
    int32_t DecodeRle(const uint8_t* src, uint8_t* dst);
    uint8_t* GetImage() const;
    // The image, but only if it is 32 bits per pixel. A caller that can only consume Argb8888
    // asks through this instead of checking the header itself.
    uint8_t* GetImage32() const;
    // Make the stored rows run top-down (non-zero) or bottom-up (zero), reversing them if they
    // do not already. No-op when the image is already the way round that was asked for.
    int32_t SetTopDown(int32_t topDown);
    void Close();
    // Read the pixels, by image type. Flag 1 widens to 32 bits with an opaque alpha byte when
    // the file has none; flag 2 expands a colour-mapped image into true colour.
    int32_t ReadImage(uint32_t flags);
    int32_t ReadColorMapped(uint32_t flags);
    int32_t ReadUncompressed(uint32_t flags);
    int32_t ReadRle(uint32_t flags);
    void ExpandColorMap(uint32_t flags);
    // Drop an 8-bit alpha channel, leaving 24-bit pixels.
    int32_t RemoveAlphaChannel();
    // Replace whatever alpha there is with `alpha` (one byte per pixel), or opaque when null.
    int32_t AddAlpha(const uint8_t* alpha);

    // Take a copy of a 24 or 32-bit image to write, with 0 or 8 alpha bits, and its row and
    // column order. ref: FUN_006aa950
    int32_t SetImage(const void* image, uint16_t width, uint16_t height, uint8_t depth, uint8_t alphaBits,
                     int32_t topDown, int32_t rightToLeft);
    // How many pixels from `pixel` on are the same as it, at most `count` and 128.
    // ref: FUN_006aaa70
    int32_t RunLength(const uint8_t* pixel, int32_t count) const;
    // One row run-length encoded from *src into *dst, both advanced; 0 when the encoding has grown
    // to the uncompressed size. ref: FUN_006aab70
    int32_t EncodeRow(const uint8_t** src, uint8_t** dst);
    // Run-length encode the image; an image that will not shrink stays as it is.
    // ref: FUN_006aace0
    int32_t CompressRle();
    // Write the header, ID, colour map, image and footer to a file, deleting it on a failed
    // write. ref: FUN_006aade0
    int32_t Write(const char* fileName);
};

// Box-filters a 32-bit image down: each output pixel is its block's colour weighted by alpha
// and the block's mean alpha, or the plain mean colour with zero alpha when the block is clear.
void TgaDownsample(uint32_t* dst, uint32_t dstWidth, uint32_t dstHeight, const uint8_t* src, uint32_t srcWidth, uint32_t srcHeight);

#endif
