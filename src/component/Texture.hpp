#ifndef COMPONENT_TEXTURE_HPP
#define COMPONENT_TEXTURE_HPP

#include <storm/Hash.hpp>

class CAsyncObject;

struct BlpPalPixel;

struct TCTEXTUREINFO {
    uint16_t width;
    uint16_t height;
    uint32_t mipCount   : 8;
    uint32_t alphaSize  : 8;
    uint32_t opaque     : 1;
    uint32_t pad        : 15;

    TCTEXTUREINFO();
};

class CACHEENTRY : public TSHashObject<CACHEENTRY, HASHKEY_NONE> {
    public:
        // Member variables
        CAsyncObject* m_asyncObject;
        TCTEXTUREINFO m_info;
        char m_fileName[128];
        uint32_t m_refCount;
        uint32_t m_memHandle;
        void* m_data;
        uint32_t m_size     : 20;
        uint32_t m_missing  : 1;

        // Member functions
        CACHEENTRY();
        void AddRef();
        TCTEXTUREINFO& Info();
        bool IsLoading();
        bool IsMissing();
        int32_t LoadTexture();
        bool NeedsLoad();
        // Give up the entry's data: cancel a read still in flight (its buffer is freed when the
        // read finishes) or free what was read, and forget the name. ref: FUN_004f2ef0
        void Unload();
};

// ref: FUN_004b5600
// Which of the two texture containers a filename names, by extension: 1 for .TGA, 2 for .BLP,
// 0 for anything else (including no extension at all).
int32_t TextureGetFileType(const char* fileName);

// ref: FUN_004b5670
// `fileName` with its extension swapped for the OTHER container's, written into `out`. Returns
// the type it produced, so a caller can tell what it is about to open; 0 when `type` is 0, in
// which case `out` is just a copy. This is what lets a missing .BLP fall back to the .TGA beside
// it and the other way round.
int32_t TextureBuildAlternateName(const char* fileName, int32_t type, char* out,
                                  uint32_t outSize);

void* TextureCacheCreateTexture(const char* fileName);

// One more holder of a cache entry. ref: FUN_004f2ce0
void TextureCacheAddRef(void* handle);

// ref: FUN_004f2dc0
void TextureCacheGetStreamedBytes(void* handle, uint64_t* done, uint64_t* total);

void TextureCacheDestroyTexture(void* texture);

int32_t TextureCacheGetInfo(void* handle, TCTEXTUREINFO& info, int32_t a3);

uint8_t* TextureCacheGetMip(void* handle, uint32_t mipLevel);

BlpPalPixel* TextureCacheGetPal(void* handle);

int32_t TextureCacheHasMips(void* handle);

#endif
