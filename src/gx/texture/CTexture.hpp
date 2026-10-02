#ifndef GX_TEXTURE_C_TEXTURE_HPP
#define GX_TEXTURE_C_TEXTURE_HPP

#include "async/AsyncFile.hpp"
#include "gx/Types.hpp"
#include "gx/texture/CGxTex.hpp"
#include "util/CStatus.hpp"
#include <cstdint>
#include <common/Handle.hpp>
#include <storm/Hash.hpp>
#include <storm/List.hpp>

class CTextureAtlas;

// The texture cache key. The reference's is three words: the name, the texture flags, and a word
// whose bit 0 says "match on the name alone" -- TextureCacheGetProcedural sets it, every other
// lookup and insert clears it. The key compare is inlined into the cache's Ptr (FUN_004b6d90).
class HASHKEY_TEXTUREFILE {
    public:
        // Member variables
        char* m_filename;
        CGxTexFlags m_texFlags;
        uint32_t m_anyFlags = 0;

        // Member functions
        bool operator==(const HASHKEY_TEXTUREFILE&);
};

// Reference layout (0x170 bytes, 32-bit):
//   +0x00 CHandleObject, +0x08 TSHashObject (key at +0x1c), +0x28 flags, +0x2a bestMip,
//   +0x2b alphaBits, +0x2c loadStatus, +0x40 asyncObject, +0x44 gxTex, +0x48 gxTexTarget,
//   +0x4c gxWidth, +0x4e gxHeight, +0x50 gxTexFormat, +0x54 dataFormat, +0x58 gxTexFlags,
//   +0x5c atlas, +0x60 atlasBlockIndex, +0x64 m_link (the list of every live texture,
//   reference 0x00ac3348), +0x6c filename.
//
// flags: 0x1 opaque, 0x2 the file was opened with the caller's open flag, 0x4 atlased,
// 0x20 a streaming read that must not be cancelled.
class CTexture : public CHandleObject, public TSHashObject<CTexture, HASHKEY_TEXTUREFILE> {
    public:
        // Static variables
        static EGxTexFilter s_filterMode;   // reference DAT_00ac3298
        static int32_t s_maxAnisotropy;     // reference DAT_00ac329c

        // Member variables
        uint16_t flags = 0;
        uint8_t bestMip = 0;
        uint8_t alphaBits = 0;
        CStatus loadStatus;
        CAsyncObject* asyncObject = nullptr;
        CGxTex* gxTex = nullptr;
        EGxTexTarget gxTexTarget = GxTex_2d;
        uint16_t gxWidth = 0;
        uint16_t gxHeight = 0;
        EGxTexFormat gxTexFormat = GxTex_Unknown;
        EGxTexFormat dataFormat = GxTex_Unknown;
        CGxTexFlags gxTexFlags = CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1);
        CTextureAtlas* atlas = nullptr;
        int32_t atlasBlockIndex = 0;
        TSLink<CTexture> m_link;
        char filename[260];

        // Member functions
        CTexture();
        ~CTexture();
};

#endif
