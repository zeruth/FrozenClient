#include "gx/Texture.hpp"
#include "async/AsyncFileRead.hpp"
#include "event/Event.hpp"
#include "gx/Blp.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/blp/CBLPFile.hpp"
#include "gx/texture/CTextureAtlas.hpp"
#include "gx/texture/TextureBlob.hpp"
#include "gx/texture/TgaFile.hpp"
#include "util/CStatus.hpp"
#include <storm/List.hpp>
#include "util/Filesystem.hpp"
#include "util/OsSystem.hpp"
#include "util/SFile.hpp"
#include <algorithm>
#include <common/Time.hpp>
#include <cstring>
#include <new>
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/Vector.hpp>

#define ALIGN_PTR(ptr, align) \
    ((void*)(((uintptr_t)(ptr) + ((uintptr_t)(align) - 1)) & ~((uintptr_t)(align) - 1)))

#define MIPPED_IMG_ALIGN 16

// One released device texture kept for reuse (reference CGxTexCache, 0x14 bytes): the texture,
// when it was released, what it costs, and its link in a size bucket or the spare-node list.
// Destroying a node destroys the texture it still holds -- which is what flushing the cache does.
struct CGxTexCache {
    CGxTex* m_gxTex = nullptr;
    uint32_t m_time = 0;
    uint32_t m_size = 0;
    TSLink<CGxTexCache> m_link;

    ~CGxTexCache() {
        if (this->m_gxTex) {
            GxTexDestroy(this->m_gxTex);
        }
    }
};

// One cached mip chain (reference CMipBitsCache, 0xc bytes). Destroying a node frees the chain.
struct CMipBitsCache {
    MipBits* m_data = nullptr;
    TSLink<CMipBitsCache> m_link;

    ~CMipBitsCache() {
        if (this->m_data) {
            SMemFree(this->m_data, __FILE__, __LINE__, 0);
        }
    }
};

namespace Texture {
    // Reference DAT_00ac32a0, initialised to 1: every BLP is read asynchronously. CreateBlpSync is
    // the path only for a build that clears it.
    int32_t s_createBlpAsync = 1;
    MipBits* s_mipBits;                     // DAT_00b49c90, a 1024 x 1024 ARGB8888 chain
    int32_t s_mipBitsValid;                 // DAT_00b49c94: s_mipBits already holds this upload
    int32_t s_asyncBytesInFlight;           // DAT_00b49ca0: bytes of texture reads started
    int32_t s_gxTexCacheSize;               // DAT_00b49c98: bytes held by the released-texture cache
    int32_t s_gxTexCacheBudget;             // DAT_00b49c9c: what it may hold
    uint32_t s_gxTexCacheTime;              // DAT_00b49c78: the clock, read once per poll
    int32_t s_atlasEnable;                  // DAT_00b49c84
    const char* s_substituteName;           // DAT_00b49c7c
    const char* s_substituteWith;           // DAT_00b49c80
    TSHashTable<CTexture, HASHKEY_TEXTUREFILE> s_textureCache;
    STORM_EXPLICIT_LIST(CTexture, m_link) s_textureList;                    // 0x00ac3348

    // Released device textures by size, width and height of 32..512 as log2(size / 32) -- the
    // bucket of width w and height h is [log2(w / 32) * 6 + log2(h / 32)] -- and the spare nodes.
    STORM_EXPLICIT_LIST(CGxTexCache, m_link) s_gxTexCacheNodes;            // 0x00ac3358
    STORM_EXPLICIT_LIST(CGxTexCache, m_link) s_gxTexCache[6 * 6];          // 0x00b49cd8

    // Texture reads waiting for room under the in-flight budget: the ones a draw asked for again,
    // and the rest (reference 0x00ac337c and 0x00ac3388). Both link through CAsyncObject::link.
    STORM_EXPLICIT_LIST(CAsyncObject, link) s_asyncPriorityList;
    STORM_EXPLICIT_LIST(CAsyncObject, link) s_asyncDeferredList;

    EGxTexFormat s_pixelFormatToGxTexFormat[10] = {
        GxTex_Dxt1,         // PIXEL_DXT1
        GxTex_Dxt3,         // PIXEL_DXT3
        GxTex_Argb8888,     // PIXEL_ARGB8888
        GxTex_Argb1555,     // PIXEL_ARGB1555
        GxTex_Argb4444,     // PIXEL_ARGB4444
        GxTex_Rgb565,       // PIXEL_RGB565
        GxTex_Unknown,      // PIXEL_A8
        GxTex_Dxt5,         // PIXEL_DXT5
        GxTex_Unknown,      // PIXEL_UNSPECIFIED
        GxTex_Unknown       // PIXEL_ARGB2565
    };
}

int32_t s_pixelFormatToMipBitsCache[NUM_PIXEL_FORMATS] = {
    -1,     // PIXEL_DXT1
    -1,     // PIXEL_DXT3
    -1,     // PIXEL_ARGB8888
    0,      // PIXEL_ARGB1555
    0,      // PIXEL_ARGB4444
    0,      // PIXEL_RGB565
    -1,     // PIXEL_A8
    -1,     // PIXEL_DXT5
    -1,     // PIXEL_UNSPECIFIED
    1,      // PIXEL_ARGB2565
};

// Reference 0x00ac3354: 0xff00ff00, opaque green.
static CImVector CRAPPY_GREEN = { 0x00, 0xFF, 0x00, 0xFF };

// The update callback a released texture is left with while it waits in the cache: it answers no
// texels, so a device reset re-creating the texture uploads nothing. The reference installs its
// shared empty function (FUN_005eeb70) here.
static void TextureCacheNullCallback(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t face, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
}

// ref: FUN_004b5130
// The cleanup a texture read gets when its texture goes away mid-read: the bytes leave the
// in-flight total, and the buffer and the request are freed.
void AsyncTextureCleanup(CAsyncObject* object) {
    Texture::s_asyncBytesInFlight -= object->size;

    void* buffer = object->buffer;

    AsyncFileReadDestroyObject(object);

    SMemFree(buffer, __FILE__, __LINE__, 0);
}

// ref: FUN_004b5300
// A texture read that failed outright. The texture keeps its pointer to the request, as in the
// reference.
static void AsyncTextureFailed(void* param) {
    auto texture = static_cast<CTexture*>(param);
    auto object = texture->asyncObject;

    Texture::s_asyncBytesInFlight -= object->size;

    void* buffer = object->buffer;

    AsyncFileReadDestroyObject(object);

    SMemFree(buffer, __FILE__, __LINE__, 0);
}

// ref: FUN_004b64e0
// Start a texture read that was waiting for room: take it off the waiting list, give it its
// buffer, count the bytes in flight and queue it. `a2` is passed through to the queue.
void AsyncTextureStartRead(CAsyncObject* object, int32_t a2) {
    object->link.Unlink();

    object->buffer = SMemAlloc(object->size, __FILE__, __LINE__, 0);
    Texture::s_asyncBytesInFlight += object->size;

    AsyncFileReadObject(object, a2);
}

// ref: FUN_004b6550
// Block until a texture's read has landed. A read still waiting for room is started first, at the
// front of the queue.
void AsyncTextureWait(CTexture* texture) {
    if (!texture->asyncObject) {
        return;
    }

    if (!texture->asyncObject->buffer) {
        AsyncTextureStartRead(texture->asyncObject, 1);
    }

    AsyncFileReadWait(texture->asyncObject);
}

// ref: FUN_004b69e0
// The read queue's poll callback: start as many waiting texture reads as fit under 4 MB in flight,
// the ones asked for again first. The room is unsigned, as in the reference.
static void AsyncTexturePoll() {
    uint32_t room = 0x400000 - Texture::s_asyncBytesInFlight;

    for (auto object = Texture::s_asyncPriorityList.Head(); object; ) {
        auto next = Texture::s_asyncPriorityList.Next(object);

        if (object->size <= room) {
            AsyncTextureStartRead(object, 0);
            room -= object->size;
        }

        object = next;
    }

    for (auto object = Texture::s_asyncDeferredList.Head(); object; ) {
        auto next = Texture::s_asyncDeferredList.Next(object);

        if (object->size <= room) {
            object->link.Unlink();

            object->buffer = SMemAlloc(object->size, __FILE__, __LINE__, 0);
            Texture::s_asyncBytesInFlight += object->size;

            AsyncFileReadObject(object, 0);

            room -= object->size;
        }

        object = next;
    }
}

// ref: FUN_004b7f10
// How many texture reads are still waiting to start, for AsyncFileReadWaitAll.
static int32_t AsyncTexturePendingCount() {
    int32_t count = 0;

    for (auto object = Texture::s_asyncPriorityList.Head(); object; object = Texture::s_asyncPriorityList.Next(object)) {
        count++;
    }

    for (auto object = Texture::s_asyncDeferredList.Head(); object; object = Texture::s_asyncDeferredList.Next(object)) {
        count++;
    }

    return count;
}

// ref: FUN_004b5170
// "Error loading texture file "name": <the last Storm error>" into the caller's status, after which
// the last error is cleared.
int32_t FileError(CStatus* status, const char* kind, const char* fileName) {
    char errorStr[256];
    SErrGetErrorStr(SErrGetLastError(), errorStr, sizeof(errorStr));

    status->Add(STATUS_FATAL, "Error loading %s file \"%s\": %s\n", kind, fileName, errorStr);

    SErrSetLastError(0);

    return 0;
}

// ref: FUN_004b5210
// The one file-name substitution the client keeps (TextureSetSubstitution): when the base name of
// `fileName` is the substituted name, `dest` receives the same path with the replacement's base
// name. Used for the blood splats the violence level swaps.
int32_t FindSubstitution(char* dest, const char* fileName) {
    if (!Texture::s_substituteName) {
        return 0;
    }

    const char* baseName = SStrChrR(fileName, '\\');

    baseName = baseName ? baseName + 1 : fileName;

    if (SStrCmpI(Texture::s_substituteName, baseName, STORM_MAX_STR)) {
        return 0;
    }

    uint32_t pathLength = static_cast<uint32_t>(baseName - fileName);

    SStrCopy(dest, fileName, STORM_MAX_PATH);
    SStrCopy(dest + pathLength, Texture::s_substituteWith, STORM_MAX_PATH - pathLength);

    return 1;
}

// ref: FUN_004b5280
// The image a texture that will not load is drawn with: every level of the shared chain filled
// opaque white.
MipBits* GetDefaultTexture(uint32_t width, uint32_t height) {
    auto images = Texture::s_mipBits;

    BuildMipLevelPointers(PIXEL_ARGB8888, width, height, reinterpret_cast<void**>(images));

    auto level = reinterpret_cast<void**>(images);

    while (width > 1 || height > 1) {
        memset(*level, 0xFF, width * height * 4);

        level++;

        width = (width >> 1) ? width >> 1 : 1;
        height = (height >> 1) ? height >> 1 : 1;
    }

    return images;
}

// ref: FUN_004b5340
// Open a file the way the texture loaders do: a failure that set no error of its own reports
// ERROR_FILE_NOT_FOUND.
SFile* TextureOpenFile(const char* fileName, int32_t openFlag) {
    SFile* file = nullptr;

    SErrSetLastError(0);

    if (!SFile::OpenEx(nullptr, fileName, openFlag != 0, &file)) {
        if (!SErrGetLastError()) {
            SErrSetLastError(2);
        }

        return nullptr;
    }

    return file;
}

// ref: FUN_004b5390
// The UIFaster CVar's bit 0: whether textures created with the atlas flag may share pages.
void TextureSetAtlasEnable(int32_t enable) {
    Texture::s_atlasEnable = enable;
}

// ref: FUN_004b5430
// Give a texture's device texture a new update callback, which also marks the whole texture for
// upload. The shadow ramps use it to regenerate.
void TextureSetUpdateCallback(HTEXTURE handle, TEXTURE_CALLBACK* userFunc, void* userArg) {
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(handle);
    STORM_VALIDATE_END_VOID;

    GxTexSetCallback(TextureGetTexturePtr(handle)->gxTex, userFunc, userArg);
}

// ref: FUN_004b5460
int32_t TextureIsAtlased(HTEXTURE handle) {
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(handle);
    STORM_VALIDATE_END;

    return TextureGetTexturePtr(handle)->atlas != nullptr;
}

// ref: FUN_004b5490
// Where an atlased texture sits in its page, as a texture-coordinate offset and the scale of one
// block (an eighth of the page).
int32_t TextureGetAtlasCoords(HTEXTURE handle, C2Vector* offset, float* scale) {
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(handle);
    STORM_VALIDATE_END;

    auto texture = TextureGetTexturePtr(handle);

    if (!texture->atlas) {
        return 0;
    }

    *scale = 0.125f;

    uint32_t block = texture->atlasBlockIndex;

    offset->x = 0.125f * static_cast<float>(block & 0x7);
    offset->y = static_cast<float>((static_cast<int32_t>(block) >> 3) & 0x7) * *scale;

    return 1;
}

// ref: FUN_004b56f0
const char* TextureGetFilename(HTEXTURE handle) {
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(handle);
    STORM_VALIDATE_END;

    return TextureGetTexturePtr(handle)->filename;
}

// ref: FUN_004b5710
// Whether the texture's data has arrived: no read is outstanding for it.
int32_t TextureIsLoaded(HTEXTURE handle) {
    return handle && !TextureGetTexturePtr(handle)->asyncObject;
}

// ref: FUN_004b5730
// Set (or with two nulls clear) the substitution FindSubstitution applies.
void TextureSetSubstitution(const char* name, const char* replacement) {
    Texture::s_substituteName = name;
    Texture::s_substituteWith = replacement;
}

// ref: FUN_004b5770
// Bytes in one compressed block of a BLP pixel format; zero for an uncompressed one.
uint32_t PixelFormatBlockBytes(PIXEL_FORMAT format) {
    static uint16_t s_blockBytes[NUM_PIXEL_FORMATS] = {
        8,      // PIXEL_DXT1
        16,     // PIXEL_DXT3
        0,      // PIXEL_ARGB8888
        0,      // PIXEL_ARGB1555
        0,      // PIXEL_ARGB4444
        0,      // PIXEL_RGB565
        0,      // PIXEL_A8
        16,     // PIXEL_DXT5
        0,      // PIXEL_UNSPECIFIED
        0       // PIXEL_ARGB2565
    };

    return s_blockBytes[format];
}

// ref: FUN_004b5780
int32_t TextureLoadBlob(const char* fileName) {
    return TextureBlobLoad(fileName);
}

// ref: FUN_004b5790
int32_t TextureUnloadBlob(const char* fileName) {
    return TextureBlobUnload(fileName);
}

// ref: FUN_006ab5c0
// Bits per pixel by BLP pixel format. The switch is what PINS the enum this family of functions
// takes: DXT1 at 4 and DXT3 and DXT5 at 8 are the block-compressed rates, ARGB8888 is 32, the
// three 16-bit formats are 16 and A8 is 8. That only lines up with PIXEL_FORMAT -- read as
// EGxTexFormat the same numbers would make Abgr8888 eight bits wide.
//
// ARGB2565 is absent on purpose and falls to the default: it has no single rate, because its alpha
// lives in a plane of its own. PixelFormatLevelSize handles it before ever asking.
uint32_t PixelFormatBitsPerPixel(PIXEL_FORMAT format) {
    switch (format) {
    case PIXEL_DXT1:
        return 4;
    case PIXEL_DXT3:
    case PIXEL_A8:
    case PIXEL_DXT5:
        return 8;
    case PIXEL_ARGB8888:
        return 32;
    case PIXEL_ARGB1555:
    case PIXEL_ARGB4444:
    case PIXEL_RGB565:
        return 16;
    default:
        return 0;
    }
}

// ref: FUN_006ab620
// One mip level's size in bytes.
//
// The block-compressed formats get a MINIMUM of 4 in each axis, because a DXT level is whole 4x4
// blocks however small the level gets -- and the clamp is applied to the CUBE-MAP STRIP as a strip:
// a level whose width is six times its height keeps that shape, so the height is clamped and the
// width recomputed as six times it, rather than the two being clamped independently.
//
// ARGB2565 is the odd one out and is handled before the bits-per-pixel lookup: two bits of alpha
// per texel in a plane of their own, four texels to a byte, after all the colour data -- which is
// `pixels / 4 + pixels * 2` and is exactly what CBLPFile::GetMipSize allots for it.
uint32_t PixelFormatLevelSize(uint32_t level, uint32_t width, uint32_t height,
                              PIXEL_FORMAT format) {
    width >>= level;

    if (!width) {
        width = 1;
    }

    height >>= level;

    if (!height) {
        height = 1;
    }

    if (format == PIXEL_DXT1 || format == PIXEL_DXT3 || format == PIXEL_DXT5) {
        if (width == height * 6) {
            if (height < 5) {
                height = 4;
            }

            width = height * 6;
        } else {
            if (width < 5) {
                width = 4;
            }

            if (height < 5) {
                height = 4;
            }
        }
    }

    if (format == PIXEL_ARGB2565) {
        uint32_t alphaBytes = (width * height) >> 2;

        if (!alphaBytes) {
            alphaBytes = 1;
        }

        return alphaBytes + width * height * 2;
    }

    // A plain 32-bit multiply then a shift of 3, which is what the disassembly does at 0x6ab6ab.
    // The decompilation renders it as a 64-bit product; that is noise.
    return (PixelFormatBitsPerPixel(format) * width * height) >> 3;
}

// ref: FUN_006ab6c0
// Every level added up. Levels are summed from 0, so this is the whole chain down to 1x1 when
// `levelCount` came from CalcLevelCount.
uint32_t PixelFormatChainSize(uint32_t levelCount, uint32_t width, uint32_t height,
                              PIXEL_FORMAT format) {
    uint32_t total = 0;

    for (uint32_t i = 0; i < levelCount; i++) {
        total += PixelFormatLevelSize(i, width, height, format);
    }

    return total;
}

// ref: FUN_006ab760
// One allocation holding both the pointer table and every level, with the table first.
//
// The ALIGNMENT is the whole point of the difference from BuildMipLevelPointers, which lays out a
// buffer it did not allocate and packs the data straight after the table: this one rounds the data
// start up to a 16-byte boundary, and the 0x10 added to the allocation is what pays for the slack
// that rounding can need. A caller that mixes the two up gets levels at the wrong offsets, so they
// are deliberately separate functions here as they are in the reference.
//
// DIVERGENCE, the same one BuildMipLevelPointers carries: the reference reserves `levelCount * 4`
// for the table because its pointers are four bytes, and this uses sizeof(void*). Both the
// allocation size and the data offset use it, so the layout stays self-consistent.
//
// No null check on the allocation, which is what the reference does and what CreateBlpSync and the
// rest of this file already do -- Storm's allocator does not return null.
void** AllocMipChain(PIXEL_FORMAT format, uint32_t width, uint32_t height, const char* fileName,
                     int32_t lineNo) {
    uint32_t levelCount = CalcLevelCount(width, height);
    uint32_t chainBytes = PixelFormatChainSize(levelCount, width, height, format);

    size_t tableBytes = levelCount * sizeof(void*);

    auto raw = static_cast<char*>(SMemAlloc(chainBytes + 0x10 + tableBytes, fileName, lineNo, 0));
    auto levels = reinterpret_cast<void**>(raw);

    // The first 16-byte boundary at or after the end of the table.
    auto aligned = reinterpret_cast<char*>(
        (reinterpret_cast<uintptr_t>(raw + tableBytes) + 0xF) & ~static_cast<uintptr_t>(0xF));

    size_t offset = aligned - raw;

    for (uint32_t i = 0; i < levelCount; i++) {
        levels[i] = raw + offset;

        offset += PixelFormatLevelSize(i, width, height, format);
    }

    return levels;
}
// ref: FUN_006ab810
// Point each entry of a mip table at its own level, where the table and the level data share ONE
// buffer: the table comes first and the levels follow it back to back.
//
// DIVERGENCE, and whoever ports the allocating caller (FUN_004b78a0) has to match it: the reference
// steps over the table with `levelCount * 4` because its pointers are four bytes. This uses
// sizeof(void*), so on 64-bit the table is twice as wide and the buffer must be sized with the same
// expression or the first level will overlap the last pointer.
//
// Uses CalcLevelCount, which folds a cube-map strip -- NOT CalcLevelCountFlat.
void BuildMipLevelPointers(PIXEL_FORMAT format, uint32_t width, uint32_t height, void** levels) {
    uint32_t levelCount = CalcLevelCount(width, height);
    uint32_t offset = 0;

    auto data = reinterpret_cast<char*>(levels) + levelCount * sizeof(void*);

    for (uint32_t i = 0; i < levelCount; i++) {
        levels[i] = data + offset;

        offset += PixelFormatLevelSize(i, width, height, format);
    }
}
// ref: FUN_004b5510
// Mip levels for an image, halving both axes and flooring each at 1 until both reach 1.
//
// NOT a duplicate of CalcLevelCount below, and worth saying so because the two differ by three
// lines: that one folds a cube-map strip first (`width == 6 * height` means six square faces side
// by side, so it divides the width) and this one does not. They are separate functions in the
// reference, in different modules, with three callers each -- a caller that already knows its
// image is flat uses this one.
uint32_t CalcLevelCountFlat(uint32_t width, uint32_t height) {
    uint32_t levels = 1;

    while (width > 1 || height > 1) {
        width >>= 1;
        levels++;

        if (!width) {
            width = 1;
        }

        height >>= 1;

        if (!height) {
            height = 1;
        }
    }

    return levels;
}

// ref: FUN_006ab700
uint32_t CalcLevelCount(uint32_t width, uint32_t height) {
    uint32_t v2 = width;
    uint32_t v3 = height;
    uint32_t v4 = 1;
    uint32_t v5;
    uint32_t v6;

    if (width == 6 * height) {
        v2 = width / 6;
    }

    while (v2 > 1 || v3 > 1) {
        v5 = v2 >> 1;

        ++v4;

        v6 = v2 >> 1 < 1;
        v2 = 1;

        if (!v6) {
            v2 = v5;
        }

        if ( v3 >> 1 >= 1 ) {
            v3 >>= 1;
        } else {
            v3 = 1;
        }
    }

    return v4;
}

uint32_t CalcLevelOffset(uint32_t level, uint32_t width, uint32_t height, uint32_t fourCC) {
    uint32_t offset = 0;

    for (int32_t i = 0; i < level; i++) {
        offset += CalcLevelSize(i, width, height, fourCC);
    }

    return offset;
}

uint32_t CalcLevelSize(uint32_t level, uint32_t width, uint32_t height, uint32_t fourCC) {
    uint32_t v4 = std::max(width >> level, 1u);
    uint32_t v5 = std::max(height >> level, 1u);

    if (fourCC == 0 || fourCC == 1 || fourCC == 7) {
        if (v4 == 6 * v5) {
            if (v5 <= 4) {
                v5 = 4;
            }

            v4 = 6 * v5;
        } else {
            if (v4 <= 4) {
                v4 = 4;
            }

            if (v5 <= 4) {
                v5 = 4;
            }
        }
    }

    uint32_t size;

    if (fourCC == 9) {
        uint32_t v6 = v5 * v4;
        uint32_t v7 = v5 * v4 >> 2;

        if (v7 < 1) {
            v7 = 1;
        }

        size = v7 + 2 * v6;
    } else {
        uint32_t v9 = GetBitDepth(fourCC);

        size = (v4 * v5 * v9) >> 3;
    }

    return size;
}

void FillInSolidTexture(const CImVector& color, CTexture* texture) {
    // Treat the value of color as a nonsense pointer to ensure the value remains available
    // when GxuUpdateSingleColorTexture is called
    void* userArg = reinterpret_cast<CImVector*>(color.value);

    CGxTexFlags gxTexFlags = CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1);

    texture->gxTex = TextureAllocGxTex(
        GxTex_2d,
        8,
        8,
        0,
        GxTex_Argb8888,
        gxTexFlags,
        userArg,
        GxuUpdateSingleColorTexture,
        GxTex_Argb8888
    );

    if (color.a < 0xFE) {
        texture->flags &= ~0x1;
    } else {
        texture->flags |= 0x1;
    }

    texture->dataFormat = GxTex_Argb8888;
    texture->gxTexFormat = GxTex_Argb8888;
    texture->gxTexTarget = GxTex_2d;
    texture->gxWidth = 8;
    texture->gxHeight = 8;
    texture->gxTexFlags = CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1);

    SStrCopy(texture->filename, "SolidTexture", STORM_MAX_PATH);
}

uint32_t GetBitDepth(uint32_t fourCC) {
    switch (fourCC) {
        case 0:
            return 4;

        case 1:
        case 6:
        case 7:
            return 8;

        case 2:
            return 32;

        case 3:
        case 4:
        case 5:
            return 16;

        default:
            return 0;
    }
}

uint32_t GxCalcTexelStrideInBytes(EGxTexFormat format, uint32_t width) {
    static uint16_t word9F103C[] = {
        0,      // GxTex_Unknown
        32,     // GxTex_Abgr8888
        32,     // GxTex_Argb8888
        16,     // GxTex_Argb4444
        16,     // GxTex_Argb1555
        16,     // GxTex_Rgb565
        4,      // GxTex_Dxt1
        8,      // GxTex_Dxt3
        8,      // GxTex_Dxt5
        16,     // GxTex_Uv88
        32,     // GxTex_Gr1616F
        32,     // GxTex_R32F
        32,     // GxTex_D24X8
        0       // GxTexFormats_Last
    };

    static uint16_t word9F1058[] = {
        0,      // GxTex_Unknown
        0,      // GxTex_Abgr8888
        0,      // GxTex_Argb8888
        0,      // GxTex_Argb4444
        0,      // GxTex_Argb1555
        0,      // GxTex_Rgb565
        8,      // GxTex_Dxt1
        16,     // GxTex_Dxt3
        16,     // GxTex_Dxt5
        0,      // GxTex_Uv88
        0,      // GxTex_Gr1616F
        0,      // GxTex_R32F
        0,      // GxTex_D24X8
        0       // GxTexFormats_Last
    };

    if (format == GxTex_Dxt1 || format == GxTex_Dxt3 || format == GxTex_Dxt5) {
        uint32_t v11 = (width >> 2) * word9F1058[format];
        return std::max(v11, static_cast<uint32_t>(word9F1058[format]));
    } else {
        return width * word9F103C[format] >> 3;
    }
}

// ref: FUN_00681ee0
// The parameter-block form, under the empty name.
int32_t GxTexCreate(const CGxTexParms& parms, CGxTex*& texId) {
    return GxTexCreate(
        parms.target,
        parms.width,
        parms.height,
        parms.depth,
        parms.format,
        parms.dataFormat,
        parms.flags,
        parms.userArg,
        parms.userFunc,
        "",
        texId
    );
}

// ref: FUN_00681cb0
// Checked rather than asserted: an out-of-range request sets ERROR_INVALID_PARAMETER and fails.
int32_t GxTexCreate(uint32_t width, uint32_t height, EGxTexFormat format, CGxTexFlags flags, void* userArg, TEXTURE_CALLBACK* userFunc, CGxTex*& texId) {
    texId = nullptr;

    if (width <= GxCaps().m_texMaxSize[GxTex_2d]
        && height <= GxCaps().m_texMaxSize[GxTex_2d]
        && (width & (width - 1)) == 0
        && (height & (height - 1)) == 0
        && format <= GxTexFormats_Last
        && (!flags.m_generateMipMaps || (GxCaps().m_generateMipMaps && !(format >= GxTex_Dxt1 && format <= GxTex_Dxt5)))
        && (flags.m_filter != GxTex_Anisotropic || GxCaps().m_texFilterAnisotropic)
        && userFunc && width >= 8 && height >= 8) {
        return g_theGxDevicePtr->TexCreate(GxTex_2d, width, height, 0, format, format, flags, userArg, userFunc, "Unknown", texId);
    }

    SErrSetLastError(0x57);
    return 0;
}

int32_t GxTexCreate(EGxTexTarget target, uint32_t width, uint32_t height, uint32_t depth, EGxTexFormat format, EGxTexFormat dataFormat, CGxTexFlags flags, void* userArg, TEXTURE_CALLBACK* userFunc, const char* name, CGxTex*& texId) {
    texId = nullptr;

    STORM_ASSERT(target <= GxTexTargets_Last);
    STORM_ASSERT(GxCaps().m_texTarget[target] == 1);
    STORM_ASSERT(width >= 8);
    STORM_ASSERT(height >= 8);
    STORM_ASSERT(width <= GxCaps().m_texMaxSize[target]);
    STORM_ASSERT(height <= GxCaps().m_texMaxSize[target]);
    STORM_ASSERT((target != GxTex_Rectangle && target != GxTex_NonPow2) ? (width & (width - 1)) == 0 : 1);
    STORM_ASSERT((target != GxTex_Rectangle && target != GxTex_NonPow2) ? (height & (height - 1)) == 0 : 1);
    STORM_ASSERT((target == GxTex_Rectangle) ? flags.m_filter <= GxTex_Linear : 1);
    STORM_ASSERT(format <= GxTexFormats_Last);
    STORM_ASSERT((flags.m_generateMipMaps) ? (GxCaps().m_generateMipMaps && !(format >= GxTex_Dxt1 && format <= GxTex_Dxt5)) : 1);
    STORM_ASSERT((flags.m_filter == GxTex_Anisotropic) ? GxCaps().m_texFilterAnisotropic : 1);
    STORM_ASSERT(dataFormat <= GxTexFormats_Last);
    STORM_ASSERT(userFunc != nullptr);

    return g_theGxDevicePtr->TexCreate(
        target,
        width,
        height,
        depth,
        format,
        dataFormat,
        flags,
        userArg,
        userFunc,
        name,
        texId
    );
}

// ref: FUN_00681470
void GxTexDestroy(CGxTex* texId) {
    g_theGxDevicePtr->TexDestroy(texId);
}

// ref: FUN_00681490
void GxTexParameters(const CGxTex* texId, CGxTexParms& parms) {
    g_theGxDevicePtr->TexParameters(texId, parms);
}

// ref: FUN_006815c0
// Whether a texture of these parameters may come out of, and go back into, the released-texture
// cache: a flat 2D texture of one of the first nine formats, with no generated mips and not a
// render target.
bool GxTexReusable(const CGxTexParms& parms) {
    return parms.depth == 0
        && !parms.flags.m_generateMipMaps
        && !parms.flags.m_renderTarget
        && parms.target == GxTex_2d
        && parms.format < GxTex_Uv88;
}

// ref: FUN_00681580
// The same test on a texture that exists, which must also still have the filter it was created
// with (a flags change that alters the filter clears m_filterUnchanged).
bool GxTexReusable(const CGxTex* texId) {
    return texId
        && texId->m_filterUnchanged
        && texId->m_depth == 0
        && !texId->m_flags.m_generateMipMaps
        && !texId->m_flags.m_renderTarget
        && texId->m_target == GxTex_2d
        && texId->m_format < GxTex_Uv88;
}

// ref: FUN_00681410
void GxTexSetCallback(CGxTex* texId, TEXTURE_CALLBACK* userFunc, void* userArg) {
    g_theGxDevicePtr->TexSetCallback(texId, userFunc, userArg);
}

// ref: FUN_00681430
void GxTexSetFlags(CGxTex* texId, CGxTexFlags flags) {
    g_theGxDevicePtr->TexSetFlags(texId, flags);
}

// ref: FUN_006814b0
void GxTexSetDataFormat(CGxTex* texId, EGxTexFormat dataFormat) {
    g_theGxDevicePtr->TexSetDataFormat(texId, dataFormat);
}

// ref: FUN_006831c0
// Whether the device copy exists and holds the latest texels.
int32_t GxTexIsUploaded(const CGxTex* texId) {
    return texId->m_apiSpecificData && !texId->m_needsUpdate && !texId->m_needsCreation;
}

// ref: FUN_006831f0
int32_t GxTexHasCallback(const CGxTex* texId) {
    return texId->m_userFunc != nullptr;
}

void GxTexSetWrap(CGxTex* texId, EGxTexWrapMode wrapU, EGxTexWrapMode wrapV) {
    g_theGxDevicePtr->TexSetWrap(texId, wrapU, wrapV);
}

// ref: FUN_004b6300
// What a device texture costs in bytes, for the released-texture cache's budget: whole blocks of
// the format at the device's base mip level, and every level below it when the filter mips.
uint32_t GxTexMemSize(EGxTexFormat format, uint32_t width, uint32_t filter, uint32_t height) {
    // Bytes per block, and the block's edge in texels (four for the DXT formats), by EGxTexFormat
    // (reference 0x009f1120 and 0x009f1154).
    static uint32_t s_blockBytes[] = { 0, 4, 4, 2, 2, 2, 8, 16, 16, 2, 4, 4, 4 };
    static uint32_t s_blockSize[] = { 1, 1, 1, 1, 1, 1, 4, 4, 4, 1, 1, 1, 1 };

    uint32_t baseMip = g_theGxDevicePtr->DeviceBaseMipLevel();

    width >>= baseMip;
    height >>= baseMip;

    if (!width) {
        width = 1;
    }

    if (!height) {
        height = 1;
    }

    uint32_t blockSize = s_blockSize[format];
    uint32_t blockBytes = s_blockBytes[format];

    uint32_t size = ((blockSize - 1 + width) / blockSize) * ((blockSize - 1 + height) / blockSize) * blockBytes;

    if (filter <= GxTex_Linear) {
        return size;
    }

    while (true) {
        if (width == 1) {
            if (height == 1) {
                return size;
            }
        } else if (width > 1) {
            width >>= 1;
        }

        if (height > 1) {
            height >>= 1;
        }

        size += ((blockSize - 1 + width) / blockSize) * ((blockSize - 1 + height) / blockSize) * blockBytes;
    }
}

// ref: FUN_004b5c30
// Open a BLP and read its levels from the device's base mip into `images`, in the format the
// device takes for it. The out parameters, any of which may be null, describe what was read.
// `fileExt`, when given, receives ".blp" first.
int32_t GetBlpMips(char* fileExt, const char* fileName, int32_t openFlag, MipBits** images, uint32_t* width, uint32_t* height, EGxTexFormat* gxTexFormat, int32_t* isOpaque, uint32_t* alphaBits, PIXEL_FORMAT* pixFormat) {
    if (fileExt) {
        SStrCopy(fileExt, ".blp", STORM_MAX_STR);
    }

    CBLPFile image;

    if (!image.Open(fileName, openFlag)) {
        image.Close();
        return 0;
    }

    EGxTexFormat gxFormat = GxTex_Argb8888;
    PIXEL_FORMAT format = PIXEL_ARGB8888;

    if (image.m_header.colorEncoding == COLOR_DXT) {
        format = static_cast<PIXEL_FORMAT>(image.m_header.preferredFormat);

        if (format == PIXEL_DXT1) {
            if (GxCaps().m_texFmt[GxTex_Dxt1]) {
                gxFormat = GxTex_Dxt1;
            } else if (image.m_header.alphaSize == 0) {
                gxFormat = GxTex_Rgb565;
                format = PIXEL_RGB565;
            } else {
                gxFormat = GxTex_Argb1555;
                format = PIXEL_ARGB1555;
            }
        } else if (format == PIXEL_DXT3) {
            if (GxCaps().m_texFmt[GxTex_Dxt3]) {
                gxFormat = GxTex_Dxt3;
            } else {
                gxFormat = GxTex_Argb4444;
                format = PIXEL_ARGB4444;
            }
        } else if (format == PIXEL_DXT5) {
            if (GxCaps().m_texFmt[GxTex_Dxt5]) {
                gxFormat = GxTex_Dxt5;
            } else {
                gxFormat = GxTex_Argb4444;
                format = PIXEL_ARGB4444;
            }
        }
    }

    uint32_t imageWidth = image.m_header.width;
    uint32_t imageHeight = image.m_header.height;
    uint32_t bestMip = 0;

    RequestImageDimensions(&imageWidth, &imageHeight, &bestMip);

    if (width) {
        *width = imageWidth;
    }

    if (height) {
        *height = imageHeight;
    }

    if (gxTexFormat) {
        *gxTexFormat = gxFormat;
    }

    uint32_t alpha = static_cast<uint8_t>(image.m_header.alphaSize);

    if (isOpaque) {
        *isOpaque = alpha == 0;
    }

    if (alphaBits) {
        *alphaBits = alpha;
    }

    if (pixFormat) {
        *pixFormat = format;
    }

    if (!image.LockChain(format, *images, bestMip)) {
        image.Close();
        return 0;
    }

    image.Close();
    return 1;
}

// ref: FUN_004b5e10
// Read a texture's levels again, for a device that lost them: the stored name is tried as a .blp
// (or what the substitution makes of it). The extension is appended in place and cut again.
int32_t ReloadMips(char* fileName, int32_t openFlag, MipBits** images) {
    char* fileExt = fileName + SStrLen(fileName);

    fileExt[0] = 0x2E; // .blp
    fileExt[1] = 0x62;
    fileExt[2] = 0x6C;
    fileExt[3] = 0x70;
    fileExt[4] = 0;

    char substitute[STORM_MAX_PATH];
    const char* name = fileName;

    if (FindSubstitution(substitute, fileName)) {
        name = substitute;
    }

    int32_t result = GetBlpMips(nullptr, name, openFlag, images, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    *fileExt = '\0';

    return result;
}

// ref: FUN_004b70a0
// Give a device texture back. One the cache can take -- a flat 32..512 texture that still has its
// creation filter -- is kept in its size bucket, its update callback disarmed, and counted against
// the budget; anything else is destroyed. `name` is the owner's, and unused.
void TextureFreeGxTex(CGxTex* texId, const char* name) {
    CGxTexParms parms;
    GxTexParameters(texId, parms);

    if (!GxTexReusable(texId) || parms.width - 32 > 480 || parms.height - 32 > 480) {
        GxTexMemSize(parms.format, parms.width, parms.flags.m_filter, parms.height);
        GxTexDestroy(texId);

        return;
    }

    uint32_t widthLog = 0;

    for (uint32_t w = parms.width >> 5; !(w & 1); w >>= 1) {
        widthLog++;
    }

    uint32_t heightLog = 0;

    for (uint32_t h = parms.height >> 5; !(h & 1); h >>= 1) {
        heightLog++;
    }

    auto node = Texture::s_gxTexCacheNodes.Head();

    if (!node) {
        node = Texture::s_gxTexCacheNodes.NewNode(STORM_LIST_HEAD, 0, 0);
    }

    node->m_link.Unlink();

    node->m_gxTex = texId;
    node->m_time = Texture::s_gxTexCacheTime;
    node->m_size = GxTexMemSize(parms.format, parms.width, parms.flags.m_filter, parms.height);

    Texture::s_gxTexCache[heightLog + widthLog * 6].LinkToTail(node);

    Texture::s_gxTexCacheSize += node->m_size;

    GxTexSetCallback(node->m_gxTex, &TextureCacheNullCallback, nullptr);
}

// ref: FUN_004b6760
// A device texture for these parameters: a released one of the same size, format, target, depth
// and filter if the cache holds one (the most recently released first), re-armed with the new
// callback, data format and flags; otherwise a new one.
CGxTex* TextureAllocGxTex(EGxTexTarget target, uint32_t width, uint32_t height, uint32_t depth, EGxTexFormat format, CGxTexFlags flags, void* userArg, TEXTURE_CALLBACK* userFunc, EGxTexFormat dataFormat) {
    CGxTexParms parms;

    parms.target = target;
    parms.width = width;
    parms.height = height;
    parms.depth = depth;
    parms.format = format;
    parms.dataFormat = dataFormat;
    parms.flags = flags;
    parms.flags.m_generateMipMaps = 0;
    parms.userArg = userArg;
    parms.userFunc = userFunc;

    if (GxTexReusable(parms) && width - 32 <= 480 && height - 32 <= 480) {
        uint32_t widthLog = 0;

        for (uint32_t w = width >> 5; !(w & 1); w >>= 1) {
            widthLog++;
        }

        uint32_t heightLog = 0;

        for (uint32_t h = height >> 5; !(h & 1); h >>= 1) {
            heightLog++;
        }

        auto& bucket = Texture::s_gxTexCache[heightLog + widthLog * 6];

        for (auto node = bucket.Tail(); node; node = node->m_link.Prev()) {
            CGxTexParms cached;
            GxTexParameters(node->m_gxTex, cached);

            if (cached.flags.m_filter != parms.flags.m_filter
                || cached.format != parms.format
                || cached.target != parms.target
                || cached.depth != parms.depth) {
                continue;
            }

            Texture::s_gxTexCacheSize -= node->m_size;

            node->m_link.Unlink();

            CGxTex* texId = node->m_gxTex;
            node->m_gxTex = nullptr;

            Texture::s_gxTexCacheNodes.LinkToHead(node);

            GxTexSetDataFormat(texId, parms.dataFormat);
            GxTexSetCallback(texId, parms.userFunc, parms.userArg);
            GxTexSetFlags(texId, parms.flags);

            return texId;
        }
    }

    CGxTex* texId = nullptr;
    CGxTexParms create = parms;
    GxTexCreate(create, texId);

    return texId;
}

// Spare nodes (reference 0x00ac3364), and the cached chains by shape (reference 0x00b49e88):
// width and height of 8..256 as log2(size / 8), times the two cached format families.
static STORM_EXPLICIT_LIST(CMipBitsCache, m_link) s_mipBitsCacheNodes;
static STORM_EXPLICIT_LIST(CMipBitsCache, m_link) s_mipBitsCache[6 * 6 * 2];

static uint32_t MipBitsCacheIndex(int32_t cache, uint32_t width, uint32_t height) {
    uint32_t widthLog = 0;

    for (uint32_t w = width >> 3; !(w & 1); w >>= 1) {
        widthLog++;
    }

    uint32_t heightLog = 0;

    for (uint32_t h = height >> 3; !(h & 1); h >>= 1) {
        heightLog++;
    }

    return cache + (heightLog + widthLog * 6) * 2;
}

// ref: FUN_004b7220
MipBits* TextureAllocMippedImg(PIXEL_FORMAT pixelFormat, uint32_t width, uint32_t height) {
    auto cache = s_pixelFormatToMipBitsCache[pixelFormat];

    if (width < 8 || height < 8 || width > 256 || height > 256 || cache == -1) {
        return reinterpret_cast<MipBits*>(AllocMipChain(pixelFormat, width, height, __FILE__, __LINE__));
    }

    auto node = s_mipBitsCache[MipBitsCacheIndex(cache, width, height)].Head();

    if (!node) {
        return reinterpret_cast<MipBits*>(AllocMipChain(pixelFormat, width, height, __FILE__, __LINE__));
    }

    node->m_link.Unlink();
    auto image = node->m_data;
    node->m_data = nullptr;
    s_mipBitsCacheNodes.LinkToTail(node);

    return image;
}

// ref: FUN_004b7300
void TextureFreeMippedImg(MipBits* image, PIXEL_FORMAT pixelFormat, uint32_t width, uint32_t height) {
    auto cache = s_pixelFormatToMipBitsCache[pixelFormat];

    if (width < 8 || height < 8 || width > 256 || height > 256 || cache == -1) {
        if (image) {
            SMemFree(image, __FILE__, __LINE__, 0);
        }

        return;
    }

    auto node = s_mipBitsCacheNodes.Head();

    if (!node) {
        node = s_mipBitsCacheNodes.NewNode(STORM_LIST_TAIL, 0, 0);
        node->m_link.Unlink();
    }

    node->m_data = image;
    s_mipBitsCache[MipBitsCacheIndex(cache, width, height)].LinkToTail(node);
}

// ref: FUN_004b5550
// Fills the mip levels the files did not supply by filtering the last level that was loaded.
static void TextureBuildMissingMips(uint32_t width, uint32_t height, uint32_t firstMissing, uint32_t levelCount, MipBits* images) {
    uint32_t source = 0;
    uint32_t sourceWidth = width;
    uint32_t sourceHeight = height;

    if (firstMissing) {
        source = firstMissing - 1;
        sourceWidth = width >> source;
        sourceHeight = height >> source;
    }

    uint32_t levelWidth = width;
    uint32_t levelHeight = height;

    for (uint32_t level = 1; level < levelCount; level++) {
        levelWidth = (levelWidth >> 1) ? levelWidth >> 1 : 1;
        levelHeight = (levelHeight >> 1) ? levelHeight >> 1 : 1;

        if (level >= firstMissing) {
            TgaDownsample(reinterpret_cast<uint32_t*>(images->mip[level]), levelWidth, levelHeight,
                reinterpret_cast<const uint8_t*>(images->mip[source]), sourceWidth, sourceHeight);
        }
    }
}

// ref: FUN_004b5a00
// Each further level may ship as its own file ("name_mip1.tga", ...) of exactly the expected
// size. Answers the first level that was not found.
static uint32_t TextureLoadTgaMips(const TgaFile& base, const char* pattern, int32_t openFlag, MipBits* images) {
    uint32_t width = base.m_header.width;
    uint32_t height = base.m_header.height;

    uint32_t level = 1;
    int32_t remaining = CalcLevelCountFlat(width, height) - 1;

    uint32_t levelWidth = width >> 1;
    uint32_t levelHeight = height >> 1;

    while (remaining != 0) {
        remaining--;

        char name[STORM_MAX_PATH];
        SStrPrintf(name, sizeof(name), pattern, level);

        if (!SFile::FileExists(name)) {
            break;
        }

        TgaFile tga = {};

        if (!tga.Open(name, openFlag) || levelWidth != tga.m_header.width || levelHeight != tga.m_header.height || !tga.ReadImage(2)) {
            tga.Close();
            return level;
        }

        if ((tga.m_header.imageDescriptor & 0xF) == 0) {
            tga.AddAlpha(nullptr);
        }

        tga.SetTopDown(1);

        auto src = reinterpret_cast<const uint32_t*>(tga.GetImage32());
        auto dst = reinterpret_cast<uint8_t*>(images->mip[level]);
        level++;

        for (uint32_t i = levelHeight * levelWidth; i != 0; i--) {
            uint32_t pixel = *src++;
            dst[3] = static_cast<uint8_t>(pixel >> 24);
            dst[2] = static_cast<uint8_t>(pixel >> 16);
            dst[1] = static_cast<uint8_t>(pixel >> 8);
            dst[0] = static_cast<uint8_t>(pixel);
            dst += 4;
        }

        if (levelWidth > 1) {
            levelWidth >>= 1;
        }

        if (levelHeight > 1) {
            levelHeight >>= 1;
        }

        tga.Close();
    }

    return level;
}

// ref: FUN_004b78a0
// A .tga is always loaded as ARGB8888, its mip chain from the _mip files and filtering.
static int32_t TextureLoadTga(char* extension, int32_t* isOpaque, const char* filename, int32_t openFlag, MipBits** images, uint32_t* width, uint32_t* height, uint32_t* format, uint32_t* alphaBits, PIXEL_FORMAT* dataFormat) {
    if (extension) {
        extension[0] = 0x2E; // .tga
        extension[1] = 0x74;
        extension[2] = 0x67;
        extension[3] = 0x61;
        extension[4] = 0;
    }

    TgaFile tga = {};

    if (tga.Open(filename, openFlag)) {
        if (isOpaque) {
            *isOpaque = (tga.m_header.imageDescriptor & 0xF) == 0;
        }

        if (tga.ReadImage(3)) {
            tga.SetTopDown(1);

            uint32_t imageWidth = tga.m_header.width;
            uint32_t imageHeight = tga.m_header.height;
            uint32_t levelCount = CalcLevelCountFlat(imageWidth, imageHeight);

            if (!*images) {
                *images = TextureAllocMippedImg(PIXEL_ARGB8888, imageWidth, imageHeight);
            } else {
                BuildMipLevelPointers(PIXEL_ARGB8888, imageWidth, imageHeight, reinterpret_cast<void**>(*images));
            }

            auto src = reinterpret_cast<const uint32_t*>(tga.GetImage32());
            auto dst = reinterpret_cast<uint8_t*>((*images)->mip[0]);

            for (uint32_t i = imageHeight * imageWidth; i != 0; i--) {
                uint32_t pixel = *src++;
                dst[3] = static_cast<uint8_t>(pixel >> 24);
                dst[2] = static_cast<uint8_t>(pixel >> 16);
                dst[1] = static_cast<uint8_t>(pixel >> 8);
                dst[0] = static_cast<uint8_t>(pixel);
                dst += 4;
            }

            tga.Close();

            char pattern[STORM_MAX_PATH];
            SStrCopy(pattern, filename, sizeof(pattern));

            auto dot = SStrChrR(pattern, '.');

            if (dot) {
                *dot = 0;
            }

            SStrPack(pattern, "_mip%d.tga", sizeof(pattern));

            uint32_t firstMissing = TextureLoadTgaMips(tga, pattern, openFlag, *images);
            TextureBuildMissingMips(tga.m_header.width, tga.m_header.height, firstMissing, levelCount, *images);

            if (width) {
                *width = tga.m_header.width;
            }

            if (height) {
                *height = tga.m_header.height;
            }

            if (format) {
                *format = 2;
            }

            if (alphaBits) {
                *alphaBits = tga.m_header.imageDescriptor & 0xF;
            }

            if (dataFormat) {
                *dataFormat = PIXEL_ARGB8888;
            }

            tga.Close();
            return 1;
        }
    }

    tga.Close();
    return 0;
}

// ref: FUN_004b8070
// Without a format asked for, the BLP's alpha depth picks one: none RGB565, 1 bit ARGB1555,
// otherwise ARGB4444.
static int32_t TextureLoadBlp(char* extension, const char* filename, int32_t openFlag, MipBits** images, uint32_t* width, uint32_t* height, int32_t* isOpaque, uint32_t* alphaBits, PIXEL_FORMAT* dataFormat) {
    if (extension) {
        extension[0] = 0x2E; // .blp
        extension[1] = 0x62;
        extension[2] = 0x6C;
        extension[3] = 0x70;
        extension[4] = 0;
    }

    CBLPFile image;

    if (!image.Open(filename, openFlag)) {
        image.Close();
        return 0;
    }

    uint32_t alpha = static_cast<uint8_t>(image.m_header.alphaSize);
    uint32_t imageWidth = image.m_header.width;
    uint32_t imageHeight = image.m_header.height;

    PIXEL_FORMAT format;

    if (!dataFormat || *dataFormat == PIXEL_UNSPECIFIED) {
        format = alpha == 0 ? PIXEL_RGB565 : alpha == 1 ? PIXEL_ARGB1555 : PIXEL_ARGB4444;
    } else {
        format = *dataFormat;
    }

    *images = TextureAllocMippedImg(format, imageWidth, imageHeight);

    if (!image.LockChain2(filename, format, *images, 0, 0)) {
        image.Close();
        return 0;
    }

    if (width) {
        *width = imageWidth;
    }

    if (height) {
        *height = imageHeight;
    }

    if (isOpaque) {
        *isOpaque = alpha == 0;
    }

    if (alphaBits) {
        *alphaBits = alpha;
    }

    if (dataFormat) {
        *dataFormat = format;
    }

    image.Close();
    return 1;
}

// ref: FUN_004b81d0
// The extension given is ignored: the name is tried as .blp, then as .tga.
MipBits* TextureLoadImage(const char* filename, uint32_t* width, uint32_t* height, PIXEL_FORMAT* dataFormat, int32_t* isOpaque, CStatus* status, uint32_t* alphaBits, int32_t openFlag) {
    if (!filename || !width || !height || !dataFormat) {
        SErrSetLastError(0x57);
        return nullptr;
    }

    char path[STORM_MAX_PATH];
    strcpy(path, filename);

    auto slash = strrchr(path, '\\');

    if (!slash) {
        slash = strrchr(path, '/');
    }

    auto extension = strrchr(path, '.');

    if (!extension || (slash && extension <= slash)) {
        extension = path + strlen(path);
    }

    *extension = 0;

    MipBits* images = nullptr;
    uint32_t method = 1;

    for (uint32_t attempt = 0; attempt < 2; attempt++) {
        if (method == 0) {
            TextureLoadTga(extension, isOpaque, path, openFlag, &images, width, height, nullptr, alphaBits, dataFormat);
        } else if (method == 1) {
            TextureLoadBlp(extension, path, openFlag, &images, width, height, isOpaque, alphaBits, dataFormat);
        }

        if (images) {
            return images;
        }

        method = (method + 1) % 2;
    }

    if (status) {
        status->Add(STATUS_FATAL, "Error loading texure file \"%s\": unsupported image format\n", filename);
    }

    return images;
}

// ref: FUN_00681f20
void GxTexUpdate(CGxTex* texId, int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, int32_t immediate) {
    CiRect rect = { minY, minX, maxY, maxX };
    g_theGxDevicePtr->TexMarkForUpdate(texId, rect, immediate);
}

// ref: FUN_006813d0
void GxTexUpdate(CGxTex* texId, CiRect& updateRect, int32_t immediate) {
    g_theGxDevicePtr->TexMarkForUpdate(texId, updateRect, immediate);
}

void GxuUpdateSingleColorTexture(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    static uint8_t image[256] = { 0 };

    switch (cmd) {
        case GxTex_Lock: {
            // Treat the userArg pointer as the literal color to use while filling the texture
            uint32_t color = reinterpret_cast<uintptr_t>(userArg);

            for (int32_t i = 0; i < sizeof(image) / sizeof(color); i++) {
                reinterpret_cast<uint32_t*>(image)[i] = color;
            }

            return;
        }

        case GxTex_Latch: {
            texelStrideInBytes = 4 * w;
            texels = image;

            return;
        }

        default:
            return;
    }
}

// ref: FUN_004b5fe0
// The pixel format a BLP is decoded to and the device format it is uploaded as, from the format it
// was saved for and its alpha depth.
void GetTextureFormats(PIXEL_FORMAT* pixFormat, EGxTexFormat* gxTexFormat, PIXEL_FORMAT preferredFormat, int32_t alphaBits) {
    switch (preferredFormat) {
        case PIXEL_DXT1:
            if (GxCaps().m_texFmt[GxTex_Dxt1]) {
                *gxTexFormat = GxTex_Dxt1;
                *pixFormat = PIXEL_DXT1;
            } else if (alphaBits) {
                *gxTexFormat = GxTex_Argb1555;
                *pixFormat = PIXEL_ARGB1555;;
            } else {
                *gxTexFormat = GxTex_Rgb565;
                *pixFormat = PIXEL_RGB565;
            }

            break;

        case PIXEL_DXT3:
            if (GxCaps().m_texFmt[GxTex_Dxt3]) {
                *gxTexFormat = GxTex_Dxt3;
                *pixFormat = PIXEL_DXT3;
            } else {
                *gxTexFormat = GxTex_Argb4444;
                *pixFormat = PIXEL_ARGB4444;
            }

            break;

        case PIXEL_ARGB8888:
            *gxTexFormat = GxTex_Argb8888;
            *pixFormat = PIXEL_ARGB8888;

            break;

        case PIXEL_ARGB1555:
            *gxTexFormat = GxTex_Argb1555;
            *pixFormat = PIXEL_ARGB8888;

            break;

        case PIXEL_ARGB4444:
            *gxTexFormat = GxTex_Argb4444;
            *pixFormat = PIXEL_ARGB8888;

            break;

        case PIXEL_RGB565:
            *gxTexFormat = GxTex_Rgb565;
            *pixFormat = PIXEL_ARGB8888;

            break;

        case PIXEL_DXT5:
            if (GxCaps().m_texFmt[GxTex_Dxt5]) {
                *gxTexFormat = GxTex_Dxt5;
                *pixFormat = PIXEL_DXT5;
            } else {
                *gxTexFormat = GxTex_Argb4444;
                *pixFormat = PIXEL_ARGB4444;
            }

            break;

        case PIXEL_UNSPECIFIED:
            if (alphaBits > 0) {
                if (alphaBits == 1) {
                    *gxTexFormat = GxTex_Argb1555;
                    *pixFormat = PIXEL_ARGB8888;
                } else if (alphaBits == 4) {
                    *gxTexFormat = GxTex_Argb4444;
                    *pixFormat = PIXEL_ARGB8888;
                } else {
                    *gxTexFormat = GxTex_Argb8888;
                    *pixFormat = PIXEL_ARGB8888;
                }
            } else {
                *gxTexFormat = GxTex_Rgb565;
                *pixFormat = PIXEL_ARGB8888;
            }

            break;

        default:
            break;
    }
}

MipBits* MippedImgAllocA(uint32_t fourCC, uint32_t width, uint32_t height, const char* fileName, int32_t lineNumber) {
    uint32_t levelCount = CalcLevelCount(width, height);
    uint32_t levelDataSize = CalcLevelOffset(levelCount, width, height, fourCC);

    // Size must account for pointer array (MipBits::mip[]) + mip data + alignment
    size_t imageSize = (sizeof(MipBits::mip) * levelCount) + levelDataSize + MIPPED_IMG_ALIGN;
    auto imageData = static_cast<char*>(SMemAlloc(imageSize, fileName, lineNumber, 0x0));

    // Image (MipBits) is a dynamically sized array of mip pointers (MipBits::mip[])
    auto image = reinterpret_cast<MipBits*>(imageData);

    // Mip data starts after mip pointers
    auto mipBase = imageData + (sizeof(MipBits::mip) * levelCount);
    auto alignedMipBase = static_cast<char*>(ALIGN_PTR(mipBase, MIPPED_IMG_ALIGN));

    // Populate mip pointers for each mip level
    uint32_t levelOffset = 0;
    for (int32_t level = 0; level < levelCount; level++) {
        image->mip[level] = reinterpret_cast<C4Pixel*>(alignedMipBase + levelOffset);
        levelOffset += CalcLevelSize(level, width, height, fourCC);
    }

    return image;
}

uint32_t MippedImgCalcSize(uint32_t fourCC, uint32_t width, uint32_t height) {
    uint32_t levelCount = CalcLevelCount(width, height);
    uint32_t levelDataSize = CalcLevelOffset(levelCount, width, height, fourCC);
    uint32_t imgSize = levelDataSize + (sizeof(void*) * levelCount);

    return imgSize;
}

// Lays out the mip pointers of an existing image buffer (e.g. Texture::s_mipBits) for a given
// format and size, the same way MippedImgAllocA does for a freshly allocated one
void MippedImgSet(MipBits* images, uint32_t fourCC, uint32_t width, uint32_t height) {
    uint32_t levelCount = CalcLevelCount(width, height);

    auto imageData = reinterpret_cast<char*>(images);
    auto mipBase = imageData + (sizeof(MipBits::mip) * levelCount);
    auto alignedMipBase = static_cast<char*>(ALIGN_PTR(mipBase, MIPPED_IMG_ALIGN));

    uint32_t levelOffset = 0;
    for (uint32_t level = 0; level < levelCount; level++) {
        images->mip[level] = reinterpret_cast<C4Pixel*>(alignedMipBase + levelOffset);
        levelOffset += CalcLevelSize(level, width, height, fourCC);
    }
}

// ref: FUN_004b5bb0
// Halve an image until it fits the device's largest 2D texture, counting the levels skipped. A
// device that reports no limit is a bad parameter.
void RequestImageDimensions(uint32_t* width, uint32_t* height, uint32_t* bestMip) {
    CGxCaps systemCaps;
    memcpy(&systemCaps, &GxCaps(), sizeof(systemCaps));

    auto maxTextureSize = systemCaps.m_texMaxSize[GxTex_2d];

    if (!maxTextureSize) {
        SErrSetLastError(0x57);
        return;
    }

    while (*width > maxTextureSize || *height > maxTextureSize) {
        *width >>= 1;
        *height >>= 1;

        ++*bestMip;

        if (!*width) {
            *width = 1;
        }

        if (!*height) {
            *height = 1;
        }
    }
}

// The order the six faces of a cube map sit in a strip, by the face the device asks for.
static uint32_t s_cubeFaceOrder[6] = { 0, 2, 4, 5, 3, 1 };

// ref: FUN_004b5e80
// A BLP texture's device callback. The texels are in the shared chain: put there by the load that
// is uploading now (s_mipBitsValid), or read from the file again when the device asks on its own,
// with a white image standing in for a file that will not read. A cube map's faces sit side by side
// in each level, so a face is an offset into the row and the row is six faces wide.
void UpdateBlpTextureAsync(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t face, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    CTexture* texture = static_cast<CTexture*>(userArg);

    switch (cmd) {
        case GxTex_Lock:
            if (Texture::s_mipBitsValid) {
                return;
            }

            if (!ReloadMips(texture->filename, texture->flags & 0x2, &Texture::s_mipBits)) {
                GetDefaultTexture(w, h);

                GetGlobalStatusObj().Add(
                    STATUS_ERROR,
                    "Texture %s not loaded -- replaced with default.\n",
                    texture->filename
                );
            }

            return;

        case GxTex_Latch: {
            texelStrideInBytes = GxCalcTexelStrideInBytes(texture->dataFormat, w);

            auto level = reinterpret_cast<const uint8_t*>(Texture::s_mipBits->mip[mipLevel]);
            texels = level;

            if (texture->gxTexTarget == GxTex_CubeMap) {
                texels = level + texelStrideInBytes * s_cubeFaceOrder[face];
                texelStrideInBytes *= 6;
            }

            return;
        }

        default:
            return;
    }
}

// Report a BLP failure once, up to a cap. Every one of these ends as a CRAPPY_GREEN square on
// screen, and until 2026-09-23 the only record was a CStatus nothing reads -- so a wrong texture
// looked identical to a missing feature. The cap is there because a systematic failure (a format
// the backend cannot take, say) would otherwise fill the log with the same line.
static void ReportTextureFailure(const char* filename, const char* why) {
    static int32_t s_reported = 0;

    if (s_reported >= 24) {
        return;
    }

    s_reported++;

    SysMsgPrintf(SYSMSG_ERROR, "BLP load failed (%s): %s", why, filename ? filename : "?");

    if (s_reported == 24) {
        SysMsgPrintf(SYSMSG_ERROR, "BLP load failures: further ones not reported");
    }
}

// The allocation failure carries the dimensions and format, because that is what distinguishes a
// backend that cannot take the format from one that cannot take the size.
static void ReportTextureAllocFailure(const char* filename, uint32_t w, uint32_t h, int32_t fmt) {
    static int32_t s_reported = 0;

    if (s_reported >= 24) {
        return;
    }

    s_reported++;

    SysMsgPrintf(SYSMSG_ERROR, "texture alloc failed %ux%u fmt=%d: %s",
                 w, h, fmt, filename ? filename : "?");
}

// ref: FUN_004b7bd0
// Decode a BLP that is in memory into the shared chain and upload it: into an atlas page when the
// texture is atlased, otherwise into a device texture of its own.
int32_t PumpBlpTextureAsync(CTexture* texture, void* buf) {
    CBLPFile image;

    if (!image.Source(buf)) {
        texture->loadStatus.Add(
            STATUS_FATAL,
            "BLP Texture failure: \"%s\" invalid file version\n",
            texture->filename
        );

        ReportTextureFailure(texture->filename, "invalid file version");

        image.Close();

        return 0;
    }

    if (texture->flags & 0x4 && !(image.m_header.hasMips & 0x10)) {
        texture->flags &= 0xFFFB;
    }

    texture->alphaBits = image.m_header.alphaSize;

    if (image.m_header.alphaSize == 0) {
        texture->flags |= 0x1;
    }

    uint32_t width = image.m_header.width;
    uint32_t height = image.m_header.height;
    uint32_t bestMip = 0;

    RequestImageDimensions(&width, &height, &bestMip);

    texture->bestMip = bestMip;

    PIXEL_FORMAT pixFormat;
    EGxTexFormat gxTexFormat;
    PIXEL_FORMAT preferredFormat = static_cast<PIXEL_FORMAT>(image.m_header.preferredFormat);
    int32_t alphaSize = image.m_header.alphaSize;

    GetTextureFormats(&pixFormat, &gxTexFormat, preferredFormat, alphaSize);

    int32_t mipLevel = texture->bestMip;

    Texture::s_mipBitsValid = 1;

    if (!image.LockChain2(texture->filename, pixFormat, Texture::s_mipBits, mipLevel, 1)) {
        Texture::s_mipBitsValid = 0;

        texture->loadStatus.Add(
            STATUS_FATAL,
            "BLP Texture failure: \"%s\" decompression failed.\n",
            texture->filename
        );

        ReportTextureFailure(texture->filename, "decompression failed");

        image.Close();

        return 0;
    }

    uint32_t gxHeight = height;
    uint32_t gxWidth = width;
    EGxTexTarget gxTexTarget = GxTex_2d;

    // Check if texture dimensions indicate cube mapping
    if (width == 6 * height) {
        gxHeight = height;
        gxWidth = width / 6u;
        gxTexTarget = GxTex_CubeMap;
    }

    texture->gxHeight = gxHeight;
    texture->gxWidth = gxWidth;
    texture->gxTexTarget = gxTexTarget;

    EGxTexFormat dataFormat = Texture::s_pixelFormatToGxTexFormat[pixFormat];
    texture->dataFormat = dataFormat;
    texture->gxTexFormat = gxTexFormat;

    // A single-level BLP has no mip chain, so the filter must not ask for one: ITexWHDStartEnd
    // returns a full mip count for any filter above Linear, and the upload then reads mip pointers
    // that were never filled. The narrow form of this guard (width < 256 only) was enough while
    // most textures kept the filter their caller built, and became a fault the moment the global
    // texture-filtering mode was applied the way the reference applies it.
    if (image.m_numLevels == 1 && !texture->gxTexFlags.m_generateMipMaps) {
        if (gxWidth < 256) {
            texture->gxTexFlags.m_filter = GxTex_Nearest;
        } else if (texture->gxTexFlags.m_filter > GxTex_Linear) {
            texture->gxTexFlags.m_filter = GxTex_Linear;
        }
    }

    // An atlased texture is uploaded into its block of a shared page; the texture itself gets no
    // device texture. One the pages will not take loses the flag and is uploaded on its own.
    if (texture->flags & 0x4) {
        texture->atlas = CTextureAtlas::Allocate(texture);

        if (texture->atlas) {
            texture->atlas->UpdateBlock(texture);
        } else {
            texture->flags &= ~0x4;
        }
    }

    if (!texture->atlas) {
        if (texture->gxTex) {
            TextureFreeGxTex(texture->gxTex, texture->filename);
            texture->gxTex = nullptr;
        }

        CGxTex* gxTex = TextureAllocGxTex(
            texture->gxTexTarget,
            texture->gxWidth,
            texture->gxHeight,
            0,
            texture->gxTexFormat,
            texture->gxTexFlags,
            texture,
            &UpdateBlpTextureAsync,
            texture->dataFormat
        );

        texture->gxTex = gxTex;

        if (!gxTex) {
            Texture::s_mipBitsValid = 0;

            texture->loadStatus.Add(
                STATUS_FATAL,
                "BLP Texture failure: \"%s\" allocating %dx%d texture failed.\n",
                texture->filename,
                gxWidth,
                gxHeight
            );

            ReportTextureAllocFailure(texture->filename, gxWidth, gxHeight,
                                      static_cast<int32_t>(texture->gxTexFormat));

            image.Close();

            return 0;
        }

        GxTexUpdate(gxTex, 0, 0, gxWidth, gxHeight, 1);
    }

    Texture::s_mipBitsValid = 0;

    image.Close();

    return 1;
}

// ref: FUN_004b7e80
// A texture read has landed: decode and upload it (a solid green square when that fails), then
// close the file and release the request, whose bytes leave the in-flight total.
//
// On a failure the reference also hands a non-empty load status to FUN_004b4f90, a system-message
// display whose output is compiled out of 12340: it formats the status into text and frees it, and
// its only effect is the last error. Frozen does not carry that formatter; the failure is logged by
// ReportTextureFailure inside PumpBlpTextureAsync instead.
static void AsyncTextureLoaded(void* param) {
    auto texture = static_cast<CTexture*>(param);

    if (!PumpBlpTextureAsync(texture, texture->asyncObject->buffer)) {
        FillInSolidTexture(CRAPPY_GREEN, texture);
    }

    SFile::Close(texture->asyncObject->file);
    texture->asyncObject->file = nullptr;

    Texture::s_asyncBytesInFlight -= texture->asyncObject->size;

    SMemFree(texture->asyncObject->buffer, __FILE__, __LINE__, 0);

    AsyncFileReadDestroyObject(texture->asyncObject);

    texture->asyncObject = nullptr;
}

// ref: FUN_004b8a50
// Open a BLP and queue its read. The texture exists at once, with no data: its CGxTex arrives when
// the read lands (AsyncTextureLoaded), or straight away from a texture blob when one carries a
// low-detail copy of it. The read starts now if it fits under 4 MB in flight, and otherwise waits
// for the read queue's poll to find room.
CTexture* CreateBlpAsync(char* fileExt, char* fileName, int32_t createFlags, CGxTexFlags texFlags) {
    SFile* file = nullptr;

    SErrSetLastError(0);

    if (!SFile::OpenEx(nullptr, fileName, (createFlags >> 1) & 1, &file)) {
        if (!SErrGetLastError()) {
            SErrSetLastError(2);
        }

        return nullptr;
    }

    if (!file) {
        return nullptr;
    }

    if (fileExt) {
        *fileExt = '\0';
    }

    CTextureBlobTexture* blobTexture = nullptr;

    if (!(createFlags & 0x4)) {
        blobTexture = TextureBlobFind(fileName);
    }

    auto m = SMemAlloc(sizeof(CTexture), "HTEXTURE", -2, 0x0);
    auto texture = m ? new (m) CTexture() : nullptr;

    texture->gxTexFlags = texFlags;

    if (createFlags & 0x2) {
        texture->flags |= 0x2;
    }

    if ((createFlags & 0x4) && Texture::s_atlasEnable) {
        texture->flags |= 0x4;
    }

    if ((createFlags & 0x20) && SFile::IsStreamingMode()) {
        texture->flags |= 0x20;
    }

    SStrCopy(texture->filename, fileName, STORM_MAX_STR);

    if (blobTexture) {
        TextureBlobCreateGxTex(texture, blobTexture);
    }

    texture->asyncObject = AsyncFileReadAllocObject();
    texture->asyncObject->userArg = texture;
    texture->asyncObject->userPostloadCallback = &AsyncTextureLoaded;
    texture->asyncObject->userFailedCallback = &AsyncTextureFailed;
    texture->asyncObject->file = file;
    texture->asyncObject->size = SFile::GetFileSize(file, nullptr);

    if (blobTexture) {
        texture->asyncObject->priority = 0x83;
    } else if (createFlags & 0x10) {
        texture->asyncObject->priority = 0x81;
    } else {
        texture->asyncObject->priority = 0x82;
    }

    auto object = texture->asyncObject;

    if (object->size <= static_cast<uint32_t>(0x400000 - Texture::s_asyncBytesInFlight)) {
        AsyncTextureStartRead(object, 0);
    } else {
        Texture::s_asyncDeferredList.LinkToTail(object);
    }

    return texture;
}

// ref: FUN_004b8910
// The same, read and decoded at once. The reference reaches it only when s_createBlpAsync is clear.
CTexture* CreateBlpSync(int32_t createFlags, char* fileName, char* fileExt, CGxTexFlags texFlags) {
    SFile* file = nullptr;

    SErrSetLastError(0);

    if (!SFile::OpenEx(nullptr, fileName, (createFlags >> 1) & 1, &file)) {
        if (!SErrGetLastError()) {
            SErrSetLastError(2);
        }

        return nullptr;
    }

    if (!file) {
        return nullptr;
    }

    auto m = SMemAlloc(sizeof(CTexture), "HTEXTURE", -2, 0x0);
    auto texture = new (m) CTexture();

    texture->gxTexFlags = texFlags;

    if (createFlags & 0x2) {
        texture->flags |= 0x2;
    }

    if ((createFlags & 0x4) && Texture::s_atlasEnable) {
        texture->flags |= 0x4;
    }

    if (fileExt) {
        *fileExt = 0;
    }

    SStrCopy(texture->filename, fileName, 0x7FFFFFFF);

    size_t fileSize = SFile::GetFileSize(file, 0);

    void* buf = SMemAlloc(fileSize, __FILE__, __LINE__, 0);

    if (!SFile::Read(file, buf, fileSize, nullptr, nullptr, nullptr)) {
        // The reference logs "CreateBlpTexture() failed read" through its release-build nullsub.
    }

    if (!PumpBlpTextureAsync(texture, buf)) {
        FillInSolidTexture(CRAPPY_GREEN, texture);
    }

    SFile::Close(file);

    SMemFree(buf, __FILE__, __LINE__, 0);

    return texture;
}

// ref: FUN_004b8be0
HTEXTURE CreateBlpTexture(char* fileExt, char* fileName, int32_t createFlags, CGxTexFlags texFlags) {
    if (fileExt) {
        SStrCopy(fileExt, ".blp", 0x7FFFFFFF);
    }

    char* fileExtFinal = fileExt;
    char* fileNameFinal = fileName;

    char fileNameSub[260];

    if (FindSubstitution(fileNameSub, fileName)) {
        fileNameFinal = fileNameSub;
        fileExtFinal = OsPathFindExtensionWithDot(fileNameSub);
    }

    CTexture* texture;

    if (Texture::s_createBlpAsync) {
        texture = CreateBlpAsync(fileExtFinal, fileNameFinal, createFlags, texFlags);
    } else {
        texture = CreateBlpSync(createFlags, fileNameFinal, fileExtFinal, texFlags);
    }

    HTEXTURE handle = texture ? HandleCreate(texture) : nullptr;

    return handle;
}

// ref: FUN_004b7aa0
// A TGA texture's device callback: the image is read again from the file into the shared chain on
// every upload (a white image standing in when it will not read), always as ARGB8888.
static void UpdateTgaTexture(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t face, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    CTexture* texture = static_cast<CTexture*>(userArg);

    switch (cmd) {
        case GxTex_Lock: {
            if (Texture::s_mipBitsValid) {
                return;
            }

            char* fileExt = texture->filename + SStrLen(texture->filename);

            int32_t loaded = TextureLoadTga(fileExt, nullptr, texture->filename, texture->flags & 0x2, &Texture::s_mipBits, nullptr, nullptr, nullptr, nullptr, nullptr);

            *fileExt = '\0';

            if (!loaded) {
                GetDefaultTexture(w, h);

                GetGlobalStatusObj().Add(
                    STATUS_ERROR,
                    "Texture %s not loaded -- replaced with default.\n",
                    texture->filename
                );
            }

            return;
        }

        case GxTex_Latch: {
            texelStrideInBytes = w * 4;

            auto level = reinterpret_cast<const uint8_t*>(Texture::s_mipBits->mip[mipLevel]);
            texels = level;

            if (texture->gxTexTarget == GxTex_CubeMap) {
                texels = level + texelStrideInBytes * s_cubeFaceOrder[face];
                texelStrideInBytes *= 6;
            }

            return;
        }

        default:
            return;
    }
}

// ref: FUN_004b95b0
// A .tga texture. Only the header is read here; the device callback reads the image when it
// uploads. An image narrower or shorter than 8 is refused with a solid green square. A strip six
// times as wide as it is high is a cube map of six square faces side by side.
HTEXTURE CreateTgaTexture(const char* fileName, char* fileExt, int32_t openFlag, CGxTexFlags texFlags, CStatus* status) {
    if (fileExt) {
        SStrCopy(fileExt, ".tga", STORM_MAX_STR);
    }

    TgaFile tga = {};

    if (!tga.Open(fileName, openFlag)) {
        tga.Close();
        return nullptr;
    }

    uint32_t height = tga.m_header.height;
    uint32_t width = tga.m_header.width;

    EGxTexTarget target = GxTex_2d;

    if (width == height * 6) {
        width /= 6;
        target = GxTex_CubeMap;
    }

    if (width < 8 || height < 8) {
        status->Add(STATUS_FATAL, "Error loading file \"%s\": Texture size must be at least %dx%d\n", fileName, 8, 8);

        HTEXTURE solid = TextureCreateSolid(CRAPPY_GREEN);

        tga.Close();
        return solid;
    }

    auto m = SMemAlloc(sizeof(CTexture), "HTEXTURE", -2, 0x0);

    if (m) {
        auto texture = new (m) CTexture();

        texture->alphaBits = tga.m_header.imageDescriptor & 0xF;

        if (texture->alphaBits == 0) {
            texture->flags |= 0x1;
        }

        if (openFlag) {
            texture->flags |= 0x2;
        }

        if (fileExt) {
            *fileExt = '\0';
        }

        SStrCopy(texture->filename, fileName, STORM_MAX_PATH);

        texture->gxTex = TextureAllocGxTex(target, width, height, 0, GxTex_Argb8888, texFlags, texture, &UpdateTgaTexture, GxTex_Argb8888);

        if (texture->gxTex) {
            texture->gxTexTarget = target;
            texture->gxWidth = width;
            texture->gxHeight = tga.m_header.height;

            HTEXTURE handle = HandleCreate(texture);

            tga.Close();
            return handle;
        }

        texture->~CTexture();
        SMemFree(texture, __FILE__, __LINE__, 0);
    }

    tga.Close();
    return nullptr;
}

HTEXTURE TextureCacheGetTexture(char* fileName, char* fileExt, CGxTexFlags texFlags) {
    if (fileExt) {
        *fileExt = '\0';
    }

    auto hashval = SStrHashHT(fileName);
    HASHKEY_TEXTUREFILE key = { fileName, texFlags };

    auto texture = Texture::s_textureCache.Ptr(hashval, key);

    if (fileExt) {
        *fileExt = '.';
    }

    if (texture) {
        return HandleCreate(texture);
    }

    return nullptr;
}

// The solid-colour half of the texture cache. Until 2026-09-23 both halves were stubs, so
// TextureCreateSolid asked for a cached texture, got nothing, built a fresh 8x8 one and handed it
// to an insert that dropped it -- every request for a solid colour leaked a texture. Callers are
// model load (one per missing model texture) and CSimpleTexture's SetColorTexture, so it grew with
// play rather than per frame, but it only grew.
//
// The note that used to sit here said this could not be fixed without a second hash table and a
// second TSHashObject base on CTexture, on the reasoning that the existing cache is keyed by
// filename and FillInSolidTexture names every solid texture "SolidTexture", so all colours would
// collide. That reasoning was wrong, and the reference shows why: it uses ONE table and does not
// put the name in play at all.
//
// FUN_004b7020 builds a HASHKEY_TEXTUREFILE whose filename is the EMPTY STRING -- the pointer is
// 0x009e14ff, which is the NUL terminator of the string before it, checked in the binary -- with
// the default texture flags, and then passes THE COLOUR ITSELF as the hash value. TSHashTable::Ptr
// matches on `m_hashval == hashval && m_key == key`, so the constant key never separates anything
// and the colour does all the work: two different colours can never match, and the same colour
// always does. The insert below is the same key with the same hash value.
//
// So the "SolidTexture" name that FillInSolidTexture writes is for display and debugging only. It
// is never a cache key, which is the piece the old note had back to front.
// ref: FUN_004b7020
HTEXTURE TextureCacheGetTexture(const CImVector& color) {
    // The default constructor is CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), which is exactly
    // what the reference builds here and what FillInSolidTexture gives the texture itself.
    HASHKEY_TEXTUREFILE key = { const_cast<char*>(""), CGxTexFlags() };

    auto texture = Texture::s_textureCache.Ptr(color.value, key);

    if (texture) {
        return HandleCreate(texture);
    }

    return nullptr;
}

// ref: FUN_004b6f30
// The by-name fetch for a GENERATED texture. It is a separate entry point from the file lookup above
// for one reason: that one splits the extension off the name before hashing and refuses to hand back
// a handle unless it was given somewhere to put the dot. A procedural name has no extension, so it
// hashes whole.
//
// The key's third word has bit 0 set, which makes the cache match on the name alone: the flags a
// generated texture was registered with do not matter to a by-name fetch.
HTEXTURE TextureCacheGetProcedural(char* name) {
    auto hashval = SStrHashHT(name);

    HASHKEY_TEXTUREFILE key = { name, CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), 0x1 };

    auto texture = Texture::s_textureCache.Ptr(hashval, key);

    if (!texture) {
        return nullptr;
    }

    return HandleCreate(texture);
}
// ref: FUN_004b9480
void TextureCacheNewTexture(CTexture* texture, CGxTexFlags texFlags) {
    auto hashval = SStrHashHT(texture->filename);
    HASHKEY_TEXTUREFILE key = { texture->filename, texFlags };

    Texture::s_textureCache.Insert(texture, hashval, key);
}

// ref: FUN_004b9420
// The same, under the flags the texture was made with.
void TextureCacheNewTexture(CTexture* texture) {
    auto hashval = SStrHashHT(texture->filename);
    HASHKEY_TEXTUREFILE key = { texture->filename, texture->gxTexFlags };

    Texture::s_textureCache.Insert(texture, hashval, key);
}

// The other half of the solid-colour cache; see the note on the lookup above for why the key is a
// constant and the colour is the hash value.
// ref: FUN_004b94e0
void TextureCacheNewTexture(CTexture* texture, const CImVector& color) {
    HASHKEY_TEXTUREFILE key = { const_cast<char*>(""), CGxTexFlags() };

    Texture::s_textureCache.Insert(texture, color.value, key);
}

uint32_t TextureCalcMipCount(uint32_t width, uint32_t height) {
    uint32_t count = 1;

    while (width > 1 || height > 1) {
        width /= 2;
        if (width == 0) {
            width = 1;
        }

        height /= 2;
        if (height == 0) {
            height = 1;
        }

        count++;
    }

    return count;
}

// ref: FUN_004b9760
// Create flags: 0x1 keep the filter the caller built, 0x2 open the file with the caller's flag,
// 0x4 the texture may share an atlas page, 0x8 the extension given decides the loader, 0x10 and
// 0x20 read priority and streaming hints for the async read.
HTEXTURE TextureCreate(const char* fileName, CGxTexFlags texFlags, CStatus* status, int32_t createFlags) {
    // The reference checks each in turn, naming the parameter, and fails with
    // ERROR_INVALID_PARAMETER.
    if (!fileName || !*fileName || !status) {
        SErrSetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }

    // Bit 0 CLEAR means "no explicit filter", so the global texture-filtering mode wins; only the
    // loading screen, the one caller passing 1, keeps the flags it built. This had been inverted,
    // which left the texture-filtering CVar with no effect on world and character textures.
    if (!(createFlags & 0x1)) {
        texFlags.m_filter = CTexture::s_filterMode;
    }

    if (texFlags.m_filter == 5) {
        texFlags.m_maxAnisotropy = CTexture::s_maxAnisotropy;
    } else {
        texFlags.m_maxAnisotropy = 1;
    }

    // Two loaders, the BLP one first: each attempt that fails hands over to the other. A name whose
    // own extension is to decide (create flag 0x8) gets one attempt, with that loader.
    int32_t attempts = 2;
    uint32_t loader = 1;

    char tmpFileName[STORM_MAX_PATH];

    SStrCopy(tmpFileName, fileName, STORM_MAX_PATH);

    char* fileExt = OsPathFindExtensionWithDot(tmpFileName);

    if ((createFlags & 0x8) && fileExt) {
        attempts = 1;
        loader = SStrCmpI(fileExt, ".blp", STORM_MAX_STR) == 0;
        fileExt = nullptr;
    }

    HTEXTURE texture = TextureCacheGetTexture(tmpFileName, fileExt, texFlags);

    if (texture) {
        return texture;
    }

    for (int32_t i = 0; i < attempts; i++) {
        if (loader == 0) {
            texture = CreateTgaTexture(tmpFileName, fileExt, createFlags & 0x2, texFlags, status);
        } else if (loader == 1) {
            texture = CreateBlpTexture(fileExt, tmpFileName, createFlags, texFlags);
        }

        if (texture) {
            TextureCacheNewTexture(TextureGetTexturePtr(texture), texFlags);
            return texture;
        }

        loader = (loader + 1) % 2;
    }

    FileError(status, "texture", fileName);

    // Frozen's own: the failure also goes to the log, since nothing reads the caller's status.
    ReportTextureFailure(fileName, "no loader accepted it");

    // The green square is the reference's own behaviour: TextureCreateSolid(&DAT_00ac3354), and
    // that constant is 0xff00ff00, opaque green.
    return TextureCreateSolid(CRAPPY_GREEN);
}

// ref: FUN_004b9200
// A 2D texture the caller fills through its callback.
HTEXTURE TextureCreate(uint32_t width, uint32_t height, EGxTexFormat format, EGxTexFormat dataFormat, CGxTexFlags texFlags, void* userArg, TEXTURE_CALLBACK* userFunc, const char* a8, int32_t a9) {
    return TextureCreate(
        GxTex_2d,
        width,
        height,
        0,
        format,
        dataFormat,
        texFlags,
        userArg,
        userFunc,
        a8,
        a9
    );
}

// ref: FUN_004b8c80
// The widest of the TextureCreate overloads, and the one the other two end at.
//
// Two things in the reference's flag arithmetic that confirm this layout rather than assume it:
// it ORs the filter mode into bits 0..2 of the flag word, and it decides the anisotropy from
// `(flags & 7) == 5` before packing it into bits 9..13 -- so GxTex_Anisotropic is 5 and
// m_maxAnisotropy is five bits at 9.
HTEXTURE TextureCreate(EGxTexTarget target, uint32_t width, uint32_t height, uint32_t depth, EGxTexFormat format, EGxTexFormat dataFormat, CGxTexFlags texFlags, void* userArg, TEXTURE_CALLBACK* userFunc, const char* a10, int32_t a11) {
    auto m = SMemAlloc(sizeof(CTexture), __FILE__, __LINE__, 0x0);
    auto texture = new (m) CTexture();

    if (a11) {
        texFlags.m_filter = CTexture::s_filterMode;
    }

    texFlags.m_maxAnisotropy = texFlags.m_filter == GxTex_Anisotropic ? CTexture::s_maxAnisotropy : 1;

    texture->gxTex = TextureAllocGxTex(target, width, height, depth, format, texFlags, userArg, userFunc, dataFormat);
    texture->dataFormat = dataFormat;
    texture->gxWidth = width;
    texture->gxHeight = height;
    texture->gxTexFormat = format;
    texture->gxTexTarget = target;
    texture->gxTexFlags = texFlags;
    texture->asyncObject = nullptr;

    const char* filename = a10 ? a10 : "UniqueTexture";
    SStrCopy(texture->filename, filename, STORM_MAX_PATH);

    return HandleCreate(texture);
}

HTEXTURE TextureCreateSolid(const CImVector& color) {
    HTEXTURE textureHandle = TextureCacheGetTexture(color);

    if (textureHandle) {
        return textureHandle;
    }

    auto m = SMemAlloc(sizeof(CTexture), __FILE__, __LINE__, 0x0);
    auto texture = new (m) CTexture();

    FillInSolidTexture(color, texture);
    textureHandle = HandleCreate(texture);
    TextureCacheNewTexture(texture, color);

    return textureHandle;
}

// ref: FUN_004b6610
// Takes the texture unchecked, by design: the reference dereferences it on the first line too
// (asyncObject, its +0x40), and both of its call sites in CSimpleTexture test for null first.
// Do not add a null check here to paper over a caller that is missing one.
int32_t TextureGetDimensions(CTexture* texture, uint32_t* width, uint32_t* height, int32_t force) {
    if (texture->asyncObject) {
        if (!force) {
            return 0;
        }

        if (!texture->asyncObject->buffer) {
            AsyncTextureStartRead(texture->asyncObject, 1);
        }

        AsyncFileReadWait(texture->asyncObject);
    }

    if (width) {
        *width = texture->gxWidth;
    }

    if (height) {
        *height = texture->gxHeight;
    }

    return 1;
}

int32_t TextureGetDimensions(HTEXTURE textureHandle, uint32_t* width, uint32_t* height, int32_t force) {
    return TextureGetDimensions(TextureGetTexturePtr(textureHandle), width, height, force);
}

// ref: FUN_004b6cb0
CGxTex* TextureGetGxTex(CTexture* texture, int32_t a2, CStatus* status) {
    // The reference validates rather than asserts: it names the parameter, sets last error to
    // ERROR_INVALID_PARAMETER (0x57) and returns null, so a caller handed a null texture draws
    // nothing instead of dying. STORM_ASSERT compiles out entirely in Release, which left the
    // null case falling straight through into `texture->flags`.
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(texture);
    STORM_VALIDATE_END;

    if (texture->flags & 0x4) {
        if (texture->asyncObject) {
            if (a2 != 1 && (a2 != 2 || texture->asyncObject->char24)) {
                TextureIncreasePriority(texture);
                return nullptr;
            }

            AsyncTextureWait(texture);

            if (status) {
                status->Add(texture->loadStatus);
            }
        }

        // An atlased texture draws through its page, which the device may have lost since.
        if (texture->atlas) {
            if (texture->atlas->m_flags & 0x1) {
                texture->atlas->Reload();
            }

            return texture->atlas->m_gxTex;
        }
    }

    if (texture->asyncObject) {
        TextureIncreasePriority(texture);
    }

    if (!texture->gxTex) {
        if (a2 != 1 && (a2 != 2 || texture->asyncObject->char24)) {
            return nullptr;
        }

        AsyncTextureWait(texture);

        if (status) {
            status->Add(texture->loadStatus);
        }
    }

    return texture->gxTex;
}

CGxTex* TextureGetGxTex(HTEXTURE handle, int32_t a2, CStatus* status) {
    return TextureGetGxTex(reinterpret_cast<CTexture*>(handle), a2, status);
}

CTexture* TextureGetTexturePtr(HTEXTURE handle) {
    return reinterpret_cast<CTexture*>(handle);
}

// ref: FUN_004b54f0
int32_t TextureHasAlpha(HTEXTURE handle) {
    if (!handle) {
        SErrSetLastError(ERROR_INVALID_PARAMETER);

        return 0;
    }

    return reinterpret_cast<CTexture*>(handle)->flags & 0x1;
}

// ref: FUN_004b55e0
void TextureFreeMem(void* ptr) {
    if (ptr) {
        SMemFree(ptr, __FILE__, __LINE__, 0);
    }
}

// ref: FUN_004b5750
void TextureGetTexFlags(HTEXTURE handle, CGxTexFlags* flags) {
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(handle);
    STORM_VALIDATE_END_VOID;

    *flags = TextureGetTexturePtr(handle)->gxTexFlags;
}

// ref: FUN_004b5800
int32_t TextureHasPendingData(HTEXTURE handle) {
    auto texture = TextureGetTexturePtr(handle);

    return texture->asyncObject && texture->gxTex ? 1 : 0;
}

// ref: FUN_004b6280
// How many holders the texture handle has; 1 means the caller's is the only one.
int32_t TextureGetRefCount(HTEXTURE handle) {
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(handle);
    STORM_VALIDATE_END;

    return TextureGetTexturePtr(handle)->m_refcount;
}

// ref: FUN_004b6c50
// A texture being drawn whose data has not arrived: in streaming mode its read moves up. One still
// waiting for room goes to the front of the waiting reads; one already queued is bumped within its
// queue, unless a thread has it already.
void TextureIncreasePriority(CTexture* texture) {
    if (!SFile::IsStreamingMode()) {
        return;
    }

    auto object = texture->asyncObject;

    if (!object->buffer) {
        Texture::s_asyncPriorityList.LinkToHead(object);
        return;
    }

    AsyncFileReadLockQueue();

    if (!object->isCurrent && !object->isRead && !object->isProcessed) {
        AsyncReadBumpPriority(object);
    }

    AsyncFileReadUnlockQueue();
}

// The poll-event handler that keeps the released-texture cache under its budget (reference
// 0x004b7200, registered by TextureInitialize).
static int32_t TextureCachePoll(const void* data, void* param) {
    Texture::s_gxTexCacheTime = static_cast<uint32_t>(OsGetAsyncTimeMs());

    TextureTrimGxTexCache();

    return 1;
}

// ref: FUN_004b7f80
// The shared chain every BLP and TGA upload passes through (room for 1024 x 1024 ARGB8888 with all
// its levels), the read queue's two texture hooks, and the cache's poll handler.
void TextureInitialize() {
    uint32_t size = MippedImgCalcSize(PIXEL_ARGB8888, 1024, 1024) + MIPPED_IMG_ALIGN;
    Texture::s_mipBits = reinterpret_cast<MipBits*>(SMemAlloc(size, __FILE__, __LINE__, 0));

    AsyncFileReadRegisterPollCallback(&AsyncTexturePoll);
    AsyncFileReadRegisterPendingCounter(&AsyncTexturePendingCount);

    Texture::s_gxTexCacheTime = static_cast<uint32_t>(OsGetAsyncTimeMs());
    Texture::s_gxTexCacheSize = 0;
    Texture::s_gxTexCacheBudget = 0;

    EventRegisterEx(EVENT_ID_POLL, &TextureCachePoll, nullptr, 0.0f);
}

// ref: FUN_004b8420
// Shutdown: the texture blobs, the released-texture cache and its spare nodes, the shared chain,
// the cached mip chains, and the poll handler. Live textures are named here by the reference
// through its release-build nullsub, so nothing is printed.
void TextureDestroy() {
    TextureBlobDestroy();

    GxTexCacheFlush();
    Texture::s_gxTexCacheNodes.Clear();

    SMemFree(Texture::s_mipBits, __FILE__, __LINE__, 0);
    Texture::s_mipBits = nullptr;

    for (uint32_t i = 0; i < 6 * 6; i++) {
        s_mipBitsCache[i * 2].Clear();
        s_mipBitsCache[i * 2 + 1].Clear();
    }

    s_mipBitsCacheNodes.Clear();

    EventUnregisterEx(EVENT_ID_POLL, &TextureCachePoll, nullptr, 0xFFFFFFFF);
}

// ref: FUN_004b8000
// Destroy every texture the released-texture cache holds, bucket by bucket.
void GxTexCacheFlush() {
    for (uint32_t i = 0; i < 6 * 6; i++) {
        auto& bucket = Texture::s_gxTexCache[i];

        for (auto node = bucket.Head(); node; node = bucket.Next(node)) {
            Texture::s_gxTexCacheSize -= node->m_size;
        }

        bucket.Clear();
    }
}

// ref: FUN_004b8060
// The same, and the spare nodes with it (entering the world, and on a map change).
void TextureFlushGxTexCache() {
    GxTexCacheFlush();
    Texture::s_gxTexCacheNodes.Clear();
}

// ref: FUN_004b6ae0
// Destroy released textures, smallest buckets first, until the cache is back under its budget:
// up to 16 a call, or 32 when it holds more than twice its budget. A budget of nothing empties it.
void TextureTrimGxTexCache() {
    uint32_t pressure;

    if (Texture::s_gxTexCacheBudget) {
        if (Texture::s_gxTexCacheBudget * 2 < Texture::s_gxTexCacheSize) {
            pressure = 2;
        } else if (Texture::s_gxTexCacheSize <= Texture::s_gxTexCacheBudget) {
            return;
        } else {
            pressure = 1;
        }
    } else {
        pressure = 1;
    }

    int32_t limit = pressure > 1 ? 32 : 16;
    int32_t destroyed = 0;

    for (int32_t row = 0; ; ) {
        if (Texture::s_gxTexCacheSize <= Texture::s_gxTexCacheBudget) {
            return;
        }

        for (int32_t col = 0; col < 6; col++) {
            if (Texture::s_gxTexCacheSize <= Texture::s_gxTexCacheBudget) {
                break;
            }

            auto& bucket = Texture::s_gxTexCache[col + row];

            for (auto node = bucket.Head(); node && destroyed < limit && Texture::s_gxTexCacheBudget < Texture::s_gxTexCacheSize; ) {
                Texture::s_gxTexCacheSize -= node->m_size;

                GxTexDestroy(node->m_gxTex);
                destroyed++;

                node->m_gxTex = nullptr;
                node->m_size = 0;
                node->m_time = 0xFFFFFFFF;

                auto next = bucket.Next(node);

                node->m_link.Unlink();
                Texture::s_gxTexCacheNodes.LinkToHead(node);

                node = next;
            }
        }

        row += 6;

        if (row > 35) {
            return;
        }
    }
}

// ref: FUN_004b6580
// The released-texture cache's budget in bytes, never more than the default for this machine;
// a negative request is none at all.
void TextureSetCacheSize(int32_t size) {
    int32_t limit = 0x4000000;

    uint64_t memory = OsGetPhysicalMemory();

    if (memory <= 0x40000000) {
        limit = 0x2000000;
    }

    if (GxDevApi() == GxApi_D3d9Ex) {
        limit = 0;
    }

    if (size < 0) {
        Texture::s_gxTexCacheBudget = 0;
        return;
    }

    Texture::s_gxTexCacheBudget = size <= limit ? size : limit;
}

// ref: FUN_004b6180
// The largest budget TextureSetCacheSize allows: 64 MB, 32 MB on a machine with a gigabyte or less,
// none on a Direct3D 9Ex device.
int32_t TextureGetDefaultCacheSize() {
    int32_t limit = 0x4000000;

    uint64_t memory = OsGetPhysicalMemory();

    if (memory <= 0x40000000) {
        limit = 0x2000000;
    }

    if (GxDevApi() != GxApi_D3d9Ex) {
        return limit;
    }

    return 0;
}

// ref: FUN_004b61c0
// The filter every texture made without an explicit one gets. Trilinear and anisotropic fall back
// a step on a device that cannot do them, and anisotropy is then off.
void TextureSetFilterMode(int32_t mode) {
    if (mode > GxTex_LinearMipNearest && !GxCaps().m_texFilterTrilinear) {
        CTexture::s_maxAnisotropy = 1;
        CTexture::s_filterMode = GxTex_LinearMipNearest;
        return;
    }

    if (mode > GxTex_LinearMipLinear && !GxCaps().m_texFilterAnisotropic) {
        CTexture::s_maxAnisotropy = 1;
        CTexture::s_filterMode = GxTex_LinearMipLinear;
        return;
    }

    CTexture::s_filterMode = static_cast<EGxTexFilter>(mode);
}

// ref: FUN_004b6230
// The anisotropy an anisotropic texture gets, up to what the device allows; none for any other
// filter mode.
void TextureSetMaxAnisotropy(uint32_t maxAnisotropy) {
    if (CTexture::s_filterMode != GxTex_Anisotropic) {
        CTexture::s_maxAnisotropy = 1;
        return;
    }

    if (GxCaps().m_maxTexAnisotropy < maxAnisotropy) {
        maxAnisotropy = GxCaps().m_maxTexAnisotropy;
    }

    CTexture::s_maxAnisotropy = maxAnisotropy;
}

// ref: FUN_004b62a0
// Whether the texture's device copy exists and is current.
int32_t TextureIsGxTexUploaded(HTEXTURE handle) {
    if (!handle || !TextureGetTexturePtr(handle)->gxTex) {
        return 0;
    }

    return GxTexIsUploaded(TextureGetTexturePtr(handle)->gxTex);
}

// ref: FUN_004b62d0
int32_t TextureHasGxTexCallback(HTEXTURE handle) {
    if (!handle || !TextureGetTexturePtr(handle)->gxTex) {
        return 0;
    }

    return GxTexHasCallback(TextureGetTexturePtr(handle)->gxTex);
}

// ref: FUN_004b8d70
// A texture made from a texture blob's low-detail copy alone, for a caller that wants a stand-in
// while the real file streams. Null when no blob carries the name. The name's own extension is cut
// for the lookup and put back.
HTEXTURE TextureCreateFromBlob(const char* fileName, CGxTexFlags texFlags, CStatus* status, int32_t useFilterMode) {
    if (!fileName || !*fileName || !status) {
        SErrSetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }

    if (useFilterMode) {
        texFlags.m_filter = CTexture::s_filterMode;
    }

    texFlags.m_maxAnisotropy = texFlags.m_filter == GxTex_Anisotropic ? CTexture::s_maxAnisotropy : 1;

    char* fileExt = OsPathFindExtensionWithDot(const_cast<char*>(fileName));

    if (fileExt) {
        *fileExt = '\0';
    }

    HTEXTURE handle = nullptr;
    auto blobTexture = TextureBlobFind(fileName);

    if (blobTexture) {
        auto m = SMemAlloc(sizeof(CTexture), "HTEXTURE", -2, 0x0);
        auto texture = m ? new (m) CTexture() : nullptr;

        texture->gxTexFlags = texFlags;

        SStrCopy(texture->filename, fileName, STORM_MAX_STR);

        TextureBlobCreateGxTex(texture, blobTexture);

        handle = HandleCreate(texture);
    }

    if (fileExt) {
        *fileExt = '.';
    }

    return handle;
}


// ref: FUN_004b53a0
// Does this texture already hold that file? The name is normalised the way the cache stores it
// first -- the extension dropped, and if what is left still ends in a dot, whatever follows that
// dot lowercased and the dot cut -- so "Foo.BLP" and "foo" compare equal. The two-step is the
// reference's own and handles a name that carries two extensions.
//
// Pins CGxTex::filename at +0x6c, which is what the reference compares against.
int32_t TextureIsSame(HTEXTURE textureHandle, const char* fileName) {
    char buf[STORM_MAX_PATH];
    uint32_t len = SStrCopy(buf, fileName, sizeof(buf));

    if (len >= 4 && buf[len - 4] == '.') {
        len -= 4;
    }
    auto v3 = &buf[len];
    if (*v3 == '.') {
        SStrLower(v3 + 1);
        *v3 = '\0';
    }

    STORM_ASSERT(textureHandle);

    return SStrCmpI(buf, TextureGetTexturePtr(textureHandle)->filename, sizeof(buf)) == 0;
}

// ref: FUN_004b57a0
// How much of a texture's file a streaming install has (its name with ".blp" put back on), plus one
// for the texture's own read having landed.
void TextureGetLoadProgress(CTexture* texture, uint64_t* done, uint64_t* total) {
    char* name = texture->filename;
    size_t length = SStrLen(name);
    char* extension = name + length;

    extension[0] = '.';
    extension[1] = 'b';
    extension[2] = 'l';
    extension[3] = 'p';
    extension[4] = '\0';

    SFile::GetStreamedBytes(name, done, total);
    (*total)++;

    if (!texture->asyncObject) {
        (*done)++;
    }

    *extension = '\0';
}
