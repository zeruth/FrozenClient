#include "gx/Texture.hpp"
#include "gx/Blp.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "util/Filesystem.hpp"
#include "util/SFile.hpp"
#include <algorithm>
#include <cstring>
#include <new>
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>

#define ALIGN_PTR(ptr, align) \
    ((void*)(((uintptr_t)(ptr) + ((uintptr_t)(align) - 1)) & ~((uintptr_t)(align) - 1)))

#define MIPPED_IMG_ALIGN 16

namespace Texture {
    // Invented name. Never assigned, so it is zero and CreateBlpAsync -- a stub -- is unreachable.
    // Do not set it without porting that function: its call site has no fallback. See the note
    // there.
    int32_t s_createBlpAsync;
    MipBits* s_mipBits;
    int32_t s_mipBitsValid;
    TSHashTable<CTexture, HASHKEY_TEXTUREFILE> s_textureCache;

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

static CImVector CRAPPY_GREEN = { 0x00, 0xFF, 0x00, 0xFF };

// Waits for a texture's pending async read to land. **Still a stub**, and it has live callers:
// TextureGetGxTex calls it on every blocking fetch (a2 == 1), so today that fetch never actually
// waits and simply returns whatever gxTex happens to be there, usually null.
//
// The reference (FUN_004b6550, ESI = the texture) is only seven instructions:
//
//     CAsyncObject* a = texture->asyncObject;   // +0x40
//     if (!a) return;
//     if (a->field_4 == 0) FUN_004b64e0(1);     // EDI = a: unlink its node at a+0x28 and
//                                               //   SMemFree a+0x8 -- release a finished request
//     AsyncFileReadWait(a);                     // 004ba060, which frozen has
//
// Not ported here deliberately. frozen already has AsyncFileReadWait, but FUN_004b64e0 frees the
// request out from under the list it is linked into, and getting the order or the guard wrong in
// a blocking path is a hang or a use-after-free rather than a wrong pixel. It needs the async
// texture queue read properly first; see docs/ref/parity-texture-async.md.
// ref: FUN_004b6550
void AsyncTextureWait(CTexture* texture) {
    // TODO
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

void GxTexParameters(const CGxTex* texId, CGxTexParms& parms) {
    // TODO
}

// **Do not make this return true without porting the pool on both sides at once.** It is the
// predicate of a texture reuse pool, and `false` is the safe answer: never reuse, always create
// and always destroy, which is correct and merely wasteful.
//
// `true` is not safe today. TextureFreeGxTex reads
//
//     if (GxTexReusable(parms)) { /* TODO */ return; }
//     GxTexDestroy(texId);
//
// so the moment this says yes, the free path returns without destroying the texture AND without
// putting it anywhere -- every texture leaks. TextureAllocGxTex has the other half, a bucketed
// pool keyed on width >> 5 and height >> 5 with a 512 cap, and its lookup is written; the release
// side is the `// TODO` above.
//
// That is the same shape as the three traps already found in this port (M2UseThreads,
// IsBatchDoodadCompatible, s_createBlpAsync): a stub whose caller changes its own control flow
// assuming the stub succeeded. This one is disarmed only because the constant is `false`.
//
// It is also a pure performance feature -- nothing it changes is visible -- so it is a poor
// candidate for an unverified change.
bool GxTexReusable(CGxTexParms& parms) {
    // TODO -- see above before implementing
    return false;
}

void GxTexSetWrap(CGxTex* texId, EGxTexWrapMode wrapU, EGxTexWrapMode wrapV) {
    g_theGxDevicePtr->TexSetWrap(texId, wrapU, wrapV);
}

int32_t ReloadMips(const char* filename, uint32_t a2, MipBits*& mipBits) {
    // TODO

    return 0;
}

void TextureFreeGxTex(CGxTex* texId) {
    STORM_ASSERT(texId);

    CGxTexParms gxTexParms;
    GxTexParameters(texId, gxTexParms);

    if (GxTexReusable(gxTexParms)) {
        // TODO

        return;
    }

    GxTexDestroy(texId);
}

CGxTex* TextureAllocGxTex(EGxTexTarget target, uint32_t width, uint32_t height, uint32_t depth, EGxTexFormat format, CGxTexFlags flags, void* userArg, TEXTURE_CALLBACK* userFunc, EGxTexFormat dataFormat) {
    CGxTexParms gxTexParms;

    gxTexParms.height = height;
    gxTexParms.depth = depth;
    gxTexParms.target = target;
    gxTexParms.dataFormat = dataFormat;
    gxTexParms.userArg = userArg;
    gxTexParms.format = format;
    gxTexParms.width = width;
    gxTexParms.userFunc = userFunc;
    gxTexParms.flags = flags;
    gxTexParms.flags.m_generateMipMaps = 0;

    CGxTexParms gxTexParms2;

    if (!GxTexReusable(gxTexParms) || width > 512 || height > 512) {
        CGxTex* texture = nullptr;
        memcpy(&gxTexParms2, &gxTexParms, sizeof(gxTexParms2));
        GxTexCreate(gxTexParms2, texture);

        return texture;
    }

    uint32_t v9 = width >> 5;
    uint32_t v10 = height >> 5;

    for (int32_t i = 0; !(v9 & 1); i++) {
        v9 >>= 1;
    }

    for (int32_t j = 0; !(v10 & 1); j++) {
        v10 >>= 1;
    }

    // TODO

    return nullptr;
}

MipBits* TextureAllocMippedImg(PIXEL_FORMAT pixelFormat, uint32_t width, uint32_t height) {
    auto cache = s_pixelFormatToMipBitsCache[pixelFormat];

    if (width < 8 || height < 8 || width > 256 || height > 256 || cache == -1) {
        return MippedImgAllocA(pixelFormat, width, height, __FILE__, __LINE__);
    }

    // TODO
    return nullptr;
}

void GxTexUpdate(CGxTex* texId, int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, int32_t immediate) {
    CiRect rect = { minY, minX, maxY, maxX };
    GxTexUpdate(texId, rect, immediate);
}

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

void GetDefaultTexture(uint32_t height, uint32_t width) {
    // TODO
}

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

// TODO
// - order: width, height or height, width?
void RequestImageDimensions(uint32_t* width, uint32_t* height, uint32_t* bestMip) {
    CGxCaps systemCaps;
    memcpy(&systemCaps, &GxCaps(), sizeof(systemCaps));

    auto maxTextureSize = systemCaps.m_texMaxSize[GxTex_2d];

    if (maxTextureSize) {
        while (*height > maxTextureSize || *width > maxTextureSize) {
            *height >>= 1;
            *width >>= 1;

            ++*bestMip;

            if (!*height) {
                *height = 1;
            }

            if (!*width) {
                *width = 1;
            }
        }
    } else {
        // TODO
        // SErrSetLastError(0x57u);
    }
}

void UpdateBlpTextureAsync(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    CTexture* texture = static_cast<CTexture*>(userArg);

    switch (cmd) {
        case GxTex_Lock:
            if (Texture::s_mipBitsValid) {
                return;
            }

            if (!ReloadMips(texture->filename, texture->flags & 0x2, Texture::s_mipBits)) {
                GetDefaultTexture(h, w);

                // TODO
                // GetGlobalStatusObj()->Add(
                //     STATUS_ERROR,
                //     "Texture %s not loaded -- replaced with default.\n",
                //     texture->filename
                // );
            }

            return;

        case GxTex_Latch:
            texelStrideInBytes = GxCalcTexelStrideInBytes(texture->dataFormat, w);

            if (texture->gxTexTarget == GxTex_CubeMap) {
                // TODO
            } else {
                texels = reinterpret_cast<MipBits**>(Texture::s_mipBits)[mipLevel];
            }

            return;

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

    if (texture->flags & 0x4) {
        // TODO

        // CTextureAtlas* atlas = CTextureAtlas::Get(v2);

        // texture->atlas = atlas;

        // if (atlas) {
        //    sub_4B50D0(atlas, v2);
        // } else {
        //    texture->flags &= 0xFFFBu;
        // }
    }

    if (!texture->atlas) {
        if (texture->gxTex) {
            TextureFreeGxTex(texture->gxTex);
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

int32_t FindSubstitution(const char* a1, char* a2) {
    // TODO

    return 0;
}

// **A stub with a trap attached, and the trap is one flag away.** Its only call site chooses
// between this and CreateBlpSync on Texture::s_createBlpAsync, with no fallback: when that flag is
// set and this returns nullptr, the texture is simply not created. s_createBlpAsync is declared and
// never assigned, so it is zero and every BLP currently takes the synchronous path. Setting it --
// which is what porting the reference's own initialisation would do -- stops every BLP texture in
// the client from loading, silently.
//
// The reference (FUN_004b8a50) opens the file, allocates a CTexture, checks SFile::IsStreamingMode,
// allocates an async read object and queues the read. Port it and set the flag in the same change,
// or leave both alone.
CTexture* CreateBlpAsync(char* fileExt, char* fileName, int32_t createFlags, CGxTexFlags texFlags) {
    // TODO

    return nullptr;
}

CTexture* CreateBlpSync(int32_t createFlags, char* fileName, char* fileExt, CGxTexFlags texFlags) {
    SFile* file = nullptr;

    // TODO
    // SErrSetLastError(0);

    if (!SFile::OpenEx(nullptr, fileName, (createFlags >> 1) & 1, &file)) {
        // TODO
        // if (!sub_7717E0()) {
        //     SErrSetLastError(2u);
        // }

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

    // TODO
    // if (createFlags & 0x4 && dword_B49C84) {
    //     texture->flags |= 0x4;
    // }

    if (fileExt) {
        *fileExt = 0;
    }

    SStrCopy(texture->filename, fileName, 0x7FFFFFFF);

    size_t fileSize = SFile::GetFileSize(file, 0);

    void* buf = SMemAlloc(fileSize, __FILE__, __LINE__, 0);

    if (!SFile::Read(file, buf, fileSize, nullptr, nullptr, nullptr)) {
        // nullsub_3();
    }

    if (!PumpBlpTextureAsync(texture, buf)) {
        FillInSolidTexture(CRAPPY_GREEN, texture);
    }

    SFile::Close(file);

    SMemFree(buf, __FILE__, __LINE__, 0);

    return texture;
}

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

// Every TGA texture request yields nothing. Unlike the BLP pair above there is no synchronous
// alternative to fall back to, so this one is a live gap rather than a dormant trap -- it just
// happens to be narrow, because the game's art is BLP and TGA turns up only in a few places.
//
// The reference (FUN_004b95b0) reads the file through 006aaf40/006aafb0, allocates a CTexture,
// calls TextureAllocGxTex, and falls back to TextureCreateSolid when the upload fails.
HTEXTURE CreateTgaTexture(const char* fileName, const char* fileExt, int32_t a3, CGxTexFlags texFlags, CStatus* status) {
    // TODO

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

void TextureCacheNewTexture(CTexture* texture, CGxTexFlags texFlags) {
    auto hashval = SStrHashHT(texture->filename);
    HASHKEY_TEXTUREFILE key = { texture->filename, texFlags };

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
HTEXTURE TextureCreate(const char* fileName, CGxTexFlags texFlags, CStatus* status, int32_t createFlags) {
    STORM_ASSERT(fileName);
    STORM_ASSERT(*fileName);
    STORM_ASSERT(status);

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

    int32_t v16 = 2;
    int32_t v8 = 1;

    char tmpFileName[260];

    SStrCopy(tmpFileName, fileName, 260);

    char* fileExt = OsPathFindExtensionWithDot(tmpFileName);

    char* v10 = fileExt;

    if (createFlags & 0x8) {
        if (fileExt) {
            v16 = 1;
            v8 = SStrCmpI(fileExt, ".blp", 0x7FFFFFFFu) == 0;
            v10 = 0;
        }
    }

    HTEXTURE texture = TextureCacheGetTexture(tmpFileName, v10, texFlags);

    if (texture) {
        return texture;
    }

    for (int32_t i = 0; i < v16; ++i) {
        if (v8) {
            if (v8 != 1) {
                v8 = (v8 + 1) % 2;
                continue;
            }

            texture = CreateBlpTexture(v10, tmpFileName, createFlags, texFlags);
        } else {
            texture = CreateTgaTexture(tmpFileName, v10, createFlags & 0x2, texFlags, status);
        }

        if (texture) {
            TextureCacheNewTexture(TextureGetTexturePtr(texture), texFlags);
            return texture;
        }

        v8 = (v8 + 1) % 2;
    }

    // TODO
    // FileError(status, "texture", fileName);

    ReportTextureFailure(fileName, "no loader accepted it");

    return TextureCreateSolid(CRAPPY_GREEN);

    return nullptr;
}

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

        // TODO
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

        if (texture->atlas) {
            // TODO
            // atlas->Reload();

            // TODO
            // - pull texture out of atlas

            return nullptr;
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

// ref: FUN_004b6280
// How many holders the texture handle has; 1 means the caller's is the only one.
int32_t TextureGetRefCount(HTEXTURE handle) {
    STORM_VALIDATE_BEGIN;
    STORM_VALIDATE(handle);
    STORM_VALIDATE_END;

    return TextureGetTexturePtr(handle)->m_refcount;
}

// Promotes a texture's pending async read to the front of the queue. **Still a stub**, with live
// callers: TextureGetGxTex calls it on every non-blocking fetch, which is what should make a
// texture that is being drawn load before one that is not.
//
// The reference (FUN_004b6c50) is:
//
//     if (!FUN_00422130()) return;              // async/streaming enabled at all?
//     CAsyncObject* a = texture->asyncObject;   // +0x40
//     if (a->field_4 == 0) { FUN_007b5020(0xac337c, a); return; }   // already done
//     FUN_004b9950();                                               // take the queue lock
//     if (!a->byte_21 && !a->byte_22 && !a->byte_23) FUN_004bac20(a);  // requeue at priority
//     FUN_004b9970();                                               // release the lock (tail jmp)
//
// Not ported here deliberately: the requeue and the three priority bytes are the async texture
// queue's internals, and frozen's queue is not yet known to have the same shape. See
// docs/ref/parity-texture-async.md.
// ref: FUN_004b6c50
void TextureIncreasePriority(CTexture* texture) {
    // TODO
}

void TextureInitialize() {
    uint32_t v0 = MippedImgCalcSize(PIXEL_ARGB8888, 1024, 1024) + MIPPED_IMG_ALIGN;
    Texture::s_mipBits = reinterpret_cast<MipBits*>(SMemAlloc(v0, __FILE__, __LINE__, 0));

    // TODO
    // - rest of function
}

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
