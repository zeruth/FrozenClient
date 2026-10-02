#ifndef GX_TEXTURE_TEXTURE_BLOB_HPP
#define GX_TEXTURE_TEXTURE_BLOB_HPP

#include "gx/Types.hpp"
#include <cstdint>
#include <storm/Hash.hpp>
#include <storm/List.hpp>

class CTexture;
class CTextureBlob;

// One texture in a blob (12 bytes in the file): where its name and its texels are, its size in
// texels, a byte whose bit 7 the loader sets when an earlier blob already named the texture (and
// whose value 1 otherwise means the texels are a single level), and a byte packing the alpha depth
// (high nibble) over the encoding (low nibble: 0 DXT1, 1 DXT3, 2 DXT5).
struct TextureBlobRecord {
    uint32_t nameOffset;
    uint32_t dataOffset;
    uint8_t width;
    uint8_t height;
    uint8_t flags;
    uint8_t format;
};

// A texture a blob carries, by name (reference CTextureBlobTexture, 0x20 bytes: the hash object,
// the blob, and the record).
class CTextureBlobTexture : public TSHashObject<CTextureBlobTexture, HASHKEY_STRI> {
    public:
        // Member variables
        CTextureBlob* m_blob = nullptr;
        TextureBlobRecord* m_record = nullptr;

        // Member functions
        const void* GetLevel(uint32_t mipLevel);
};

// A texture blob (a .tex file): small DXT copies of many textures, so a texture can draw at low
// detail the moment it is created while its real file is still being read. world\liquid.tex is
// loaded at start in streaming mode, and every map loads its own World\Maps\<map>\<map>.tex.
//
// The file is chunks of {tag, size, data}: one the loader skips, the records, the names, and the
// texels. Reference layout (0x120 bytes): +0x000 the file name, +0x104 the file, +0x108 the
// records, +0x10c how many, +0x110 the names, +0x114 the texel chunk, +0x118 the link in the list
// of blobs (reference 0x00ac3754).
class CTextureBlob {
    public:
        // Member variables
        char m_name[260] = {};
        uint8_t* m_data = nullptr;
        TextureBlobRecord* m_records = nullptr;
        uint32_t m_recordCount = 0;
        const char* m_names = nullptr;
        const uint8_t* m_texels = nullptr;
        TSLink<CTextureBlob> m_link;

        // Member functions
        ~CTextureBlob();
        void AddEntry(TextureBlobRecord* record);
        void AddEntries();
        void RemoveEntries();
};

// Every blob loaded, in load order (reference 0x00ac3754).
extern STORM_EXPLICIT_LIST(CTextureBlob, m_link) s_textureBlobList;

void TextureBlobCreateGxTex(CTexture* texture, CTextureBlobTexture* blobTexture);

void TextureBlobDestroy();

CTextureBlobTexture* TextureBlobFind(const char* fileName);

int32_t TextureBlobLoad(const char* fileName);

int32_t TextureBlobUnload(const char* fileName);

#endif
