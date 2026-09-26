#include "component/Texture.hpp"
#include "async/AsyncFile.hpp"
#include "async/AsyncFileRead.hpp"
#include "gx/Blp.hpp"
#include "gx/Texture.hpp"
#include "util/SFile.hpp"
#include <common/ObjectAlloc.hpp>
#include <storm/String.hpp>

// Deliberately immortal: allocated once and never destroyed. Its nodes live in an ObjectAlloc
// heap, and static destruction order across translation units is unspecified, so when this table
// was a plain global its destructor could walk nodes whose heap had already been torn down --
// which faulted on exit (freed-memory pattern in the node pointers). A process-lifetime cache has
// nothing to release at exit anyway; the OS reclaims it.
TSHashTable<CACHEENTRY, HASHKEY_NONE>& s_cacheTable = *new TSHashTable<CACHEENTRY, HASHKEY_NONE>();
HASHKEY_NONE s_cacheKey;
uint32_t* s_entryHeap;

void LoadSuccessCallback(void* handle) {
    auto entry = static_cast<CACHEENTRY*>(handle);

    AsyncFileReadDestroyObject(entry->m_asyncObject);
    entry->m_asyncObject = nullptr;

    auto& header = *static_cast<BLPHeader*>(entry->m_data);
    // TODO CBLPFile::ValidateHeader(header);

    auto& info = entry->m_info;
    info.width = header.width;
    info.height = header.height;
    info.alphaSize = header.alphaSize;
    info.opaque = header.alphaSize == 0;
    info.mipCount = TextureCalcMipCount(info.width, info.height);
}

TCTEXTUREINFO::TCTEXTUREINFO() {
    this->width = 0;
    this->height = 0;
    this->mipCount = 0;
    this->alphaSize = 0;
    this->opaque = 1;
}

CACHEENTRY::CACHEENTRY() {
    this->m_asyncObject = nullptr;
    this->m_fileName[0] = '\0';
    this->m_refCount = 0;
    this->m_memHandle = 0;
    this->m_data = nullptr;
    this->m_size = 0;
    this->m_missing = 0;
}

void CACHEENTRY::AddRef() {
    this->m_refCount++;
}

TCTEXTUREINFO& CACHEENTRY::Info() {
    return this->m_info;
}

bool CACHEENTRY::IsLoading() {
    return !this->m_missing && this->m_info.width == 0 && this->m_asyncObject;
}

bool CACHEENTRY::IsMissing() {
    return this->m_missing;
}

// ref: FUN_004b5600
int32_t TextureGetFileType(const char* fileName) {
    auto extension = SStrChrR(fileName, '.');

    if (!extension || SStrLen(extension) != 4) {
        return 0;
    }

    if (SStrCmpI(extension, ".TGA", 0x7FFFFFFF) == 0) {
        return 1;
    }

    return SStrCmpI(extension, ".BLP", 0x7FFFFFFF) == 0 ? 2 : 0;
}

// ref: FUN_004b5670
int32_t TextureBuildAlternateName(const char* fileName, int32_t type, char* out,
                                  uint32_t outSize) {
    if (fileName != out) {
        SStrCopy(out, fileName, outSize);
    }

    if (type == 0) {
        return 0;
    }

    auto extension = SStrChrR(out, '.');

    if (extension) {
        *extension = '\0';
    }

    if (type == 1) {
        SStrPack(out, ".BLP", outSize);

        return 2;
    }

    if (type == 2) {
        SStrPack(out, ".TGA", outSize);

        return 1;
    }

    return type;
}

// ref: FUN_004f2be0
int32_t CACHEENTRY::LoadTexture() {
    SFile* file;
    if (!SFile::OpenEx(nullptr, this->m_fileName, 0x0, &file)) {
        // Not there under the name asked for: the two containers sit side by side in the
        // archives, so try the other one before giving up.
        char alternate[260];
        auto type = TextureGetFileType(this->m_fileName);

        TextureBuildAlternateName(this->m_fileName, type, alternate, sizeof(alternate));
        SFile::OpenEx(nullptr, alternate, 0x0, &file);
    }

    if (!file) {
        this->m_missing = 1;

        return 0;
    }

    this->m_asyncObject = AsyncFileReadAllocObject();
    this->m_asyncObject->userArg = this;
    this->m_asyncObject->userPostloadCallback = &LoadSuccessCallback;
    this->m_asyncObject->file = file;
    this->m_asyncObject->size = SFile::GetFileSize(file, nullptr);
    this->m_asyncObject->priority = -126;

    this->m_size = this->m_asyncObject->size;
    this->m_data = STORM_ALLOC(this->m_size);

    this->m_asyncObject->buffer = this->m_data;

    AsyncFileReadObject(this->m_asyncObject, 0);

    return 1;
}

bool CACHEENTRY::NeedsLoad() {
    return !this->m_data;
}

CACHEENTRY* TextureCacheAllocEntry() {
    if (!s_entryHeap) {
        auto heapId = static_cast<uint32_t*>(SMemAlloc(sizeof(uint32_t), __FILE__, __LINE__, 0));
        *heapId = ObjectAllocAddHeap(sizeof(CACHEENTRY), 1024, "TCACHEENTRY", true);

        s_entryHeap = heapId;
    }

    uint32_t memHandle;
    void* mem;

    if (!ObjectAlloc(*s_entryHeap, &memHandle, &mem, false)) {
        return nullptr;
    }

    auto entry = new (mem) CACHEENTRY();
    entry->m_memHandle = memHandle;

    return entry;
}

// ref: FUN_004f3930
// The cache entry for a texture filename, created on first ask. Hashed by name, and every
// caller holds a reference -- the reference bumps the count on both paths, which is what the
// single AddRef at the end here does.
void* TextureCacheCreateTexture(const char* fileName) {
    auto hashval = SStrHash(fileName);
    auto texture = s_cacheTable.Ptr(hashval, s_cacheKey);

    if (!texture) {
        texture = TextureCacheAllocEntry();

        s_cacheTable.Insert(texture, hashval, s_cacheKey);

        SStrCopy(texture->m_fileName, fileName, sizeof(texture->m_fileName));
    }

    texture->AddRef();

    return texture;
}

void TextureCacheDestroyTexture(void* texture) {
    // TODO
}

int32_t TextureCacheGetInfo(void* handle, TCTEXTUREINFO& info, int32_t force) {
    auto entry = static_cast<CACHEENTRY*>(handle);

    if (!entry) {
        return 0;
    }

    if (!entry->IsMissing()) {
        if (entry->NeedsLoad()) {
            entry->LoadTexture();
        }

        if (entry->IsLoading()) {
            if (force) {
                AsyncFileReadWait(entry->m_asyncObject);
            } else {
                // TODO increase streaming priority

                return 0;
            }
        }
    }

    info = entry->Info();

    return 1;
}

uint8_t* TextureCacheGetMip(void* handle, uint32_t mipLevel) {
    auto entry = static_cast<CACHEENTRY*>(handle);

    if (!entry || entry->IsMissing() || !entry->m_data) {
        return nullptr;
    }

    if (mipLevel >= entry->Info().mipCount) {
        return nullptr;
    }

    auto blpHeader = static_cast<BLPHeader*>(entry->m_data);
    auto mipOffset = blpHeader->mipOffsets[mipLevel];

    return static_cast<uint8_t*>(entry->m_data) + mipOffset;
}

BlpPalPixel* TextureCacheGetPal(void* handle) {
    auto entry = static_cast<CACHEENTRY*>(handle);

    if (entry->IsMissing() || !entry->m_data) {
        return nullptr;
    }

    auto blpHeader = static_cast<BLPHeader*>(entry->m_data);

    if (blpHeader->colorEncoding != COLOR_PAL) {
        return nullptr;
    }

    return blpHeader->extended.palette;
}

// ref: FUN_004f2d80
int32_t TextureCacheHasMips(void* handle) {
    auto entry = static_cast<CACHEENTRY*>(handle);
    return entry && entry->m_data && !entry->IsMissing();
}
