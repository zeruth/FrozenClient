#include "gx/texture/TextureBlob.hpp"
#include "gx/Texture.hpp"
#include "util/SFile.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>

STORM_EXPLICIT_LIST(CTextureBlob, m_link) s_textureBlobList;

// Reference 0x00b4a2d0: every blob texture by name, across all the blobs loaded. The first blob to
// name a texture keeps it.
static TSHashTable<CTextureBlobTexture, HASHKEY_STRI> s_textureBlobTable;

// ref: FUN_004bfe70
// A level's texels: the levels follow one another from the record's offset, at one byte a texel for
// DXT3 and DXT5 and half that for DXT1, never less than a block's eight bytes.
const void* CTextureBlobTexture::GetLevel(uint32_t mipLevel) {
    auto record = this->m_record;

    bool dxt1 = (record->format & 0xF) == 0;
    bool halve = dxt1;

    uint32_t width = record->width;
    uint32_t height = record->height;

    const uint8_t* texels = this->m_blob->m_texels + 8 + record->dataOffset;

    for (; mipLevel; mipLevel--) {
        uint32_t size = width * height;

        if (halve) {
            size >>= 1;
        }

        halve = halve && dxt1;

        if (size < 8) {
            size = 8;
        }

        height >>= 1;
        width >>= 1;

        texels += size;
    }

    return texels;
}

// ref: FUN_004bff30
CTextureBlob::~CTextureBlob() {
    this->m_name[0] = '\0';

    if (this->m_data) {
        SMemFree(this->m_data, __FILE__, __LINE__, 0);
    }

    this->m_data = nullptr;
    this->m_records = nullptr;
    this->m_recordCount = 0;
    this->m_names = nullptr;
    this->m_texels = nullptr;

    this->m_link.Unlink();
}

// ref: FUN_004c0e60
// Register one of the blob's textures. A name an earlier blob already registered stays with that
// blob, and the record is marked so that removing this blob leaves it alone.
void CTextureBlob::AddEntry(TextureBlobRecord* record) {
    auto name = this->m_names + record->nameOffset;

    if (s_textureBlobTable.Ptr(name)) {
        record->flags |= 0x80;
        return;
    }

    auto entry = s_textureBlobTable.New(name, 0, 0);

    entry->m_blob = this;
    entry->m_record = record;
}

// ref: FUN_004c0f20
void CTextureBlob::AddEntries() {
    for (uint32_t i = 0; i < this->m_recordCount; i++) {
        this->AddEntry(&this->m_records[i]);
    }
}

// ref: FUN_004c0330
// Take the blob's textures out of the table, and clear the marks AddEntry left.
void CTextureBlob::RemoveEntries() {
    for (uint32_t i = 0; i < this->m_recordCount; i++) {
        auto record = &this->m_records[i];

        if (!(record->flags & 0x80)) {
            auto entry = s_textureBlobTable.Ptr(this->m_names + record->nameOffset);

            s_textureBlobTable.Delete(entry);
        }

        record->flags &= 0x7F;
    }
}

// ref: FUN_004c0210
static CTextureBlob* TextureBlobFindBlob(const char* fileName) {
    for (auto blob = s_textureBlobList.Head(); blob; blob = s_textureBlobList.Next(blob)) {
        if (!SStrCmpI(fileName, blob->m_name, STORM_MAX_STR)) {
            return blob;
        }
    }

    return nullptr;
}

// ref: FUN_004c1030
// Register every loaded blob's textures afresh, after one went away: names it held may now belong
// to another blob.
static void TextureBlobRebuild() {
    for (auto blob = s_textureBlobList.Head(); blob; blob = s_textureBlobList.Next(blob)) {
        blob->RemoveEntries();
    }

    for (auto blob = s_textureBlobList.Head(); blob; blob = s_textureBlobList.Next(blob)) {
        blob->AddEntries();
    }
}

// ref: FUN_004c02f0
// The blob texture for a name, when any blob is loaded.
CTextureBlobTexture* TextureBlobFind(const char* fileName) {
    if (!s_textureBlobList.Head()) {
        return nullptr;
    }

    return s_textureBlobTable.Ptr(fileName);
}

// ref: FUN_004c03c0
// A blob texture's device callback. Its texels never change, so Lock has nothing to read: Latch
// hands over the level straight out of the blob.
//
// DIVERGED in one guard: the reference looks the blob texture up only while some blob is loaded,
// and then uses the result unchecked, so a device asking after every blob was unloaded reads
// through an uninitialised register. Here a texture the lookup misses latches no texels, which the
// device skips.
static void TextureBlobUpdateTexture(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t face, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    auto texture = static_cast<CTexture*>(userArg);

    auto blobTexture = TextureBlobFind(texture->filename);

    if (cmd != GxTex_Latch) {
        return;
    }

    texelStrideInBytes = GxCalcTexelStrideInBytes(texture->dataFormat, w);

    if (!blobTexture) {
        texels = nullptr;
        return;
    }

    auto level = static_cast<const uint8_t*>(blobTexture->GetLevel(mipLevel));
    texels = level;

    if (texture->gxTexTarget == GxTex_CubeMap) {
        static uint32_t s_cubeFaceOrder[6] = { 0, 2, 4, 5, 3, 1 };

        texels = level + texelStrideInBytes * s_cubeFaceOrder[face];
        texelStrideInBytes *= 6;
    }
}

// ref: FUN_004b50c0
// The device format a BLP pixel format is uploaded as when it is uploaded unconverted.
static EGxTexFormat PixelFormatToGxTexFormat(PIXEL_FORMAT format) {
    static EGxTexFormat s_formats[NUM_PIXEL_FORMATS] = {
        GxTex_Dxt1, GxTex_Dxt3, GxTex_Argb8888, GxTex_Argb1555, GxTex_Argb4444,
        GxTex_Rgb565, GxTex_Unknown, GxTex_Dxt5, GxTex_Unknown, GxTex_Unknown
    };

    return s_formats[format];
}

// ref: FUN_004c0710
// Give a texture the blob's copy as its device texture, until its own file arrives. A copy of one
// level cannot be mip-filtered, so the filter steps down to its nearest single-level form.
void TextureBlobCreateGxTex(CTexture* texture, CTextureBlobTexture* blobTexture) {
    auto record = blobTexture->m_record;

    texture->alphaBits = record->format >> 4;

    if (texture->alphaBits == 0) {
        texture->flags |= 0x1;
    }

    uint32_t width = record->width;
    uint32_t height = record->height;

    texture->bestMip = 0;

    PIXEL_FORMAT preferred = PIXEL_DXT1;

    if ((record->format & 0xF) == 1) {
        preferred = PIXEL_DXT3;
    } else if ((record->format & 0xF) == 2) {
        preferred = PIXEL_DXT5;
    }

    PIXEL_FORMAT pixFormat;
    EGxTexFormat gxTexFormat;
    GetTextureFormats(&pixFormat, &gxTexFormat, preferred, texture->alphaBits);

    EGxTexTarget target = GxTex_2d;

    if (width == height * 6) {
        width /= 6;
        target = GxTex_CubeMap;
    }

    texture->gxTexFormat = gxTexFormat;
    texture->dataFormat = PixelFormatToGxTexFormat(preferred);
    texture->gxTexTarget = target;
    texture->gxWidth = width;
    texture->gxHeight = height;

    uint32_t filter = texture->gxTexFlags.m_filter;

    if (filter != GxTex_Nearest && filter != GxTex_Linear && record->flags == 1) {
        texture->gxTexFlags.m_filter = filter == GxTex_NearestMipNearest ? GxTex_Nearest : GxTex_Linear;
    }

    texture->gxTex = TextureAllocGxTex(target, width, height, 0, gxTexFormat, texture->gxTexFlags, texture, &TextureBlobUpdateTexture, texture->dataFormat);

    if (!texture->gxTex) {
        texture->loadStatus.Add(
            STATUS_FATAL,
            "BLP Texture failure: \"%s\" allocating %dx%d texture failed.\n",
            texture->filename,
            width,
            record->height
        );

        return;
    }

    GxTexUpdate(texture->gxTex, 0, 0, width, record->height, 1);
}

// ref: FUN_004c0f50
// Load a blob once, and register its textures.
int32_t TextureBlobLoad(const char* fileName) {
    if (TextureBlobFindBlob(fileName)) {
        return 1;
    }

    auto file = TextureOpenFile(fileName, 0);

    if (!file) {
        return 0;
    }

    auto blob = s_textureBlobList.NewNode(STORM_LIST_TAIL, 0, 0);

    SStrCopy(blob->m_name, fileName, STORM_MAX_STR);

    auto size = SFile::GetFileSize(file, nullptr);
    blob->m_data = static_cast<uint8_t*>(SMemAlloc(size, __FILE__, __LINE__, 0));

    SFile::Read(file, blob->m_data, size, nullptr, nullptr, nullptr);
    SFile::Close(file);

    auto chunk = blob->m_data + 8 + *reinterpret_cast<uint32_t*>(blob->m_data + 4);

    blob->m_records = reinterpret_cast<TextureBlobRecord*>(chunk + 8);
    blob->m_recordCount = *reinterpret_cast<uint32_t*>(chunk + 4) / sizeof(TextureBlobRecord);

    chunk = chunk + 8 + *reinterpret_cast<uint32_t*>(chunk + 4);

    blob->m_names = reinterpret_cast<const char*>(chunk + 8);
    blob->m_texels = chunk + 8 + *reinterpret_cast<uint32_t*>(chunk + 4);

    blob->AddEntries();

    return 1;
}

// ref: FUN_004c10c0
// Unload a blob by name; the others' textures are registered afresh.
int32_t TextureBlobUnload(const char* fileName) {
    for (auto blob = s_textureBlobList.Head(); blob; blob = s_textureBlobList.Next(blob)) {
        if (SStrCmpI(fileName, blob->m_name, STORM_MAX_STR)) {
            continue;
        }

        blob->RemoveEntries();
        s_textureBlobList.DeleteNode(blob);

        TextureBlobRebuild();

        return 1;
    }

    return 0;
}

// ref: FUN_004c06c0
// Unload every blob (texture shutdown).
void TextureBlobDestroy() {
    for (auto blob = s_textureBlobList.Head(); blob; ) {
        auto next = s_textureBlobList.Next(blob);

        blob->RemoveEntries();
        s_textureBlobList.DeleteNode(blob);

        blob = next;
    }
}
