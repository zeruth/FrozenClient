#ifndef GX_TEXTURE_C_TEXTURE_ATLAS_HPP
#define GX_TEXTURE_C_TEXTURE_ATLAS_HPP

#include "gx/Types.hpp"
#include "gx/texture/CGxTex.hpp"
#include <cstdint>
#include <storm/List.hpp>

class CTexture;

// A page that small square textures share: an 8 x 8 grid of equal blocks in one device texture.
// Textures created with the atlas flag (create flag 0x4, while TextureSetAtlasEnable is on) that
// are square and no larger than 64 x 64 are placed in a page of their own size, format and flags,
// and draw through the page with a texture-coordinate offset and a scale of 1/8
// (TextureGetAtlasCoords).
//
// Reference layout (0x138 bytes, 32-bit): +0x00 count of textures placed (int16), +0x02 flags
// (bit 0: the device lost the page and every block must be reloaded), +0x04 / +0x08 block width
// and height, +0x0c block capacity (64), +0x10 blocks in use, +0x14 the last block freed (-1 for
// none), +0x18 the page's CGxTex, +0x1c format, +0x20 texture flags, +0x24 data format, +0x28 64,
// +0x2c the 64 block owners, +0x12c the block being uploaded (-1 for none), +0x130 the link in the
// list of pages (reference 0x00ac3370).
class CTextureAtlas {
    public:
        // Static functions
        static CTextureAtlas* Create(uint32_t blockWidth, uint32_t blockHeight, EGxTexFormat format, EGxTexFormat dataFormat, CGxTexFlags flags);
        static CTextureAtlas* Allocate(CTexture* texture);
        static void Free(CTextureAtlas* atlas, CTexture* texture);
        static void UpdateTexture(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t face, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels);

        // Member variables
        int16_t m_textureCount;
        uint16_t m_flags;
        uint32_t m_blockWidth;
        uint32_t m_blockHeight;
        int32_t m_blockCapacity;
        int32_t m_blocksUsed;
        int32_t m_freeBlock;
        CGxTex* m_gxTex;
        EGxTexFormat m_format;
        CGxTexFlags m_texFlags;
        EGxTexFormat m_dataFormat;
        int32_t m_blockLimit;
        CTexture* m_blocks[64];
        int32_t m_currentBlock;
        TSLink<CTextureAtlas> m_link;

        // Member functions
        CTextureAtlas();
        ~CTextureAtlas();
        void Reload();
        void UpdateBlock(CTexture* texture);
};

// Every page, in the order they were made (reference 0x00ac3370).
extern STORM_EXPLICIT_LIST(CTextureAtlas, m_link) s_textureAtlasList;

#endif
