#include "gx/texture/CTextureAtlas.hpp"
#include "gx/Texture.hpp"
#include "util/CStatus.hpp"
#include <new>
#include <storm/Memory.hpp>
#include <storm/String.hpp>

STORM_EXPLICIT_LIST(CTextureAtlas, m_link) s_textureAtlasList;

// The texels a page hands the device when it has nothing to upload (reference 0x00b4a1e8). The
// reference's is a single word and the device reads past it at a stride of zero; this one is a
// full row of the widest page, 512 texels of four bytes, so the same read stays in bounds.
static uint8_t s_atlasEmptyTexels[512 * 4];

// ref: FUN_004b6700
CTextureAtlas::CTextureAtlas() {
    this->m_texFlags = CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1);
    this->m_blockLimit = 64;
    this->m_textureCount = 0;
    this->m_flags = 0;
    this->m_blockWidth = 0;
    this->m_blockHeight = 0;
    this->m_blockCapacity = 0;
    this->m_blocksUsed = 0;
    this->m_freeBlock = -1;
    this->m_gxTex = nullptr;
    this->m_currentBlock = -1;

    for (uint32_t i = 0; i < 64; i++) {
        this->m_blocks[i] = nullptr;
    }
}

// ref: FUN_004b76e0
// The page's device texture goes back to the released-texture cache; the page leaves the list.
CTextureAtlas::~CTextureAtlas() {
    if (this->m_gxTex) {
        TextureFreeGxTex(this->m_gxTex, "ATLAS");
    }

    this->m_link.Unlink();
}

// ref: FUN_004b6e90
// A page of 8 x 8 blocks of the given size, format and flags, its device texture filled by
// UpdateTexture.
CTextureAtlas* CTextureAtlas::Create(uint32_t blockWidth, uint32_t blockHeight, EGxTexFormat format, EGxTexFormat dataFormat, CGxTexFlags flags) {
    auto m = SMemAlloc(sizeof(CTextureAtlas), __FILE__, __LINE__, 0x0);
    auto atlas = m ? new (m) CTextureAtlas() : nullptr;

    atlas->m_blockHeight = blockHeight;
    atlas->m_blockWidth = blockWidth;
    atlas->m_blocksUsed = 0;
    atlas->m_freeBlock = -1;
    atlas->m_blockCapacity = 64;

    atlas->m_gxTex = TextureAllocGxTex(GxTex_2d, blockWidth * 8, blockHeight * 8, 0, format, flags, atlas, &CTextureAtlas::UpdateTexture, dataFormat);

    atlas->m_dataFormat = dataFormat;
    atlas->m_format = format;
    atlas->m_texFlags = flags;

    return atlas;
}

// ref: FUN_004b7770
// Find a block for a texture: a square one no larger than 64 x 64 goes in the fullest page of its
// size, format and flags that still has room, or in a new page. The page's flags carry bit 15, which
// tells the device a block's texels are latched for the block alone. Null when the texture cannot
// be atlased.
CTextureAtlas* CTextureAtlas::Allocate(CTexture* texture) {
    uint32_t width = texture->gxWidth;
    uint32_t height = texture->gxHeight;

    if (width != height || width > 64 || height > 64) {
        return nullptr;
    }

    CGxTexFlags flags = texture->gxTexFlags;
    flags.m_bit15 = 1;

    CTextureAtlas* best = nullptr;

    for (auto atlas = s_textureAtlasList.Head(); atlas; atlas = s_textureAtlasList.Next(atlas)) {
        if (atlas->m_format == texture->gxTexFormat
            && atlas->m_texFlags == flags
            && atlas->m_dataFormat == texture->dataFormat
            && atlas->m_blockWidth == width
            && atlas->m_blockHeight == height
            && atlas->m_blocksUsed < atlas->m_blockCapacity
            && (!best || best->m_blocksUsed < atlas->m_blocksUsed)) {
            best = atlas;
        }
    }

    if (!best) {
        best = CTextureAtlas::Create(width, height, texture->gxTexFormat, texture->dataFormat, flags);
        s_textureAtlasList.LinkToTail(best);
    }

    texture->atlasBlockIndex = -1;

    if (best->m_freeBlock == -1) {
        for (int32_t i = 0; i < 64; i++) {
            if (!best->m_blocks[i]) {
                texture->atlasBlockIndex = i;
                break;
            }
        }
    } else {
        texture->atlasBlockIndex = best->m_freeBlock;
        best->m_freeBlock = -1;
    }

    best->m_blocksUsed++;
    best->m_textureCount++;
    best->m_blocks[texture->atlasBlockIndex] = texture;

    return best;
}

// ref: FUN_004b8720
// Give a texture's block back. The last texture out takes the page with it.
void CTextureAtlas::Free(CTextureAtlas* atlas, CTexture* texture) {
    atlas->m_blocks[texture->atlasBlockIndex] = nullptr;

    atlas->m_textureCount--;
    atlas->m_blocksUsed--;
    atlas->m_freeBlock = texture->atlasBlockIndex;

    if (atlas->m_textureCount < 1) {
        atlas->~CTextureAtlas();
        SMemFree(atlas, "delete", -1, 0x0);
    }

    texture->atlasBlockIndex = -1;
}

// ref: FUN_004b5930
// The page's device callback. Only the block being uploaded has texels, and they are the shared
// chain's, at the block's width. Asked for texels with no block in hand -- the device recreating a
// lost page -- it hands over an empty row and marks the page for TextureGetGxTex to reload.
void CTextureAtlas::UpdateTexture(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t face, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    auto atlas = static_cast<CTextureAtlas*>(userArg);

    if (cmd == GxTexCommands_Last) {
        return;
    }

    if (atlas->m_currentBlock == -1) {
        atlas->m_flags |= 0x1;
        texelStrideInBytes = 0;
        texels = s_atlasEmptyTexels;
        return;
    }

    auto texture = atlas->m_blocks[atlas->m_currentBlock];

    if (cmd != GxTex_Latch || !Texture::s_mipBitsValid) {
        return;
    }

    if (atlas->m_format >= GxTex_Dxt1 && atlas->m_format <= GxTex_Dxt5) {
        uint32_t blockBytes = GxCalcTexelStrideInBytes(atlas->m_dataFormat, 4);
        uint32_t stride = blockBytes * (atlas->m_blockWidth >> 2);
        uint32_t minimum = GxCalcTexelStrideInBytes(texture->dataFormat, 4);

        texelStrideInBytes = stride < minimum ? minimum : stride;
    } else {
        texelStrideInBytes = GxCalcTexelStrideInBytes(texture->dataFormat, atlas->m_blockWidth);
    }

    texels = Texture::s_mipBits->mip[mipLevel];
}

// ref: FUN_004b50d0
// Upload one texture's block: the device is asked for exactly that rectangle, and the callback
// answers with the shared chain while m_currentBlock names the block.
void CTextureAtlas::UpdateBlock(CTexture* texture) {
    this->m_currentBlock = texture->atlasBlockIndex;

    int32_t minX = this->m_blockWidth * (this->m_currentBlock & 0x7);
    int32_t minY = this->m_blockHeight * ((this->m_currentBlock >> 3) & 0x7);

    GxTexUpdate(this->m_gxTex, minX, minY, this->m_blockWidth + minX, this->m_blockHeight + minY, 1);

    this->m_currentBlock = -1;
}

// ref: FUN_004b63b0
// Read every block's texture again and upload it, after the device lost the page.
void CTextureAtlas::Reload() {
    for (uint32_t i = 0; i < 64; i++) {
        auto texture = this->m_blocks[i];

        if (!texture) {
            continue;
        }

        int32_t openFlag = texture->flags & 0x2;

        Texture::s_mipBitsValid = 1;

        char* fileExt = texture->filename + SStrLen(texture->filename);

        fileExt[0] = 0x2E; // .blp
        fileExt[1] = 0x62;
        fileExt[2] = 0x6C;
        fileExt[3] = 0x70;
        fileExt[4] = 0;

        char substitute[STORM_MAX_PATH];
        const char* name = texture->filename;

        if (FindSubstitution(substitute, texture->filename)) {
            name = substitute;
        }

        int32_t loaded = GetBlpMips(nullptr, name, openFlag, &Texture::s_mipBits, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        *fileExt = '\0';

        if (!loaded) {
            GetGlobalStatusObj().Add(STATUS_ERROR, "Texture %s not loaded.\n", texture->filename);
        }

        this->UpdateBlock(texture);

        Texture::s_mipBitsValid = 0;
    }

    this->m_flags &= ~0x1;
}

// ref: FUN_004b65e0
// Reload every page (the base mip level or the display mode changed).
void TextureReloadAtlases() {
    for (auto atlas = s_textureAtlasList.Head(); atlas; atlas = s_textureAtlasList.Next(atlas)) {
        atlas->Reload();
    }
}
