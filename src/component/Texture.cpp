#include "component/Texture.hpp"
#include "async/AsyncFile.hpp"
#include "async/AsyncFileRead.hpp"
#include "gx/Blp.hpp"
#include "gx/blp/CBLPFile.hpp"
#include "async/CAsyncObject.hpp"
#include "gx/Device.hpp"
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

// ref: FUN_004b50a0
// Has this read not been touched yet? Not handed to a thread, not read, not the one being
// worked on -- so it is still sitting in a queue where its position can still be changed.
// Bumping a read that is already in flight would do nothing but churn the list.
static bool AsyncReadIsUnstarted(CAsyncObject* object) {
    return !object->isCurrent && !object->isRead && !object->isProcessed;
}


// ref: FUN_004f2b40
// A cancelled read's buffer outlives the entry that asked for it; it is freed here once the read
// lets go of it.
static void CancelledReadCallback(CAsyncObject* object) {
    void* buffer = object->buffer;

    AsyncFileReadDestroyObject(object);
    SMemFree(buffer, __FILE__, __LINE__, 0x0);
}

// ref: FUN_004f2b70
void LoadSuccessCallback(void* handle) {
    auto entry = static_cast<CACHEENTRY*>(handle);

    AsyncFileReadDestroyObject(entry->m_asyncObject);
    entry->m_asyncObject = nullptr;

    auto& header = *static_cast<BLPHeader*>(entry->m_data);

    // The reference checks the header and goes on regardless of the answer.
    CBLPFile::HeaderValid(&header);

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

// ref: FUN_004f2ef0
void CACHEENTRY::Unload() {
    int32_t freeData = 1;

    if (this->m_asyncObject) {
        freeData = AsyncFileReadCancel(this->m_asyncObject, &CancelledReadCallback);
        this->m_asyncObject = nullptr;
    }

    if (this->m_data && freeData) {
        SMemFree(this->m_data, __FILE__, __LINE__, 0x0);
    }

    this->m_size = 0;
    this->m_data = nullptr;
    this->m_fileName[0] = '\0';
}

// ref: FUN_004f2ce0
void TextureCacheAddRef(void* handle) {
    if (!handle) {
        return;
    }

    static_cast<CACHEENTRY*>(handle)->m_refCount++;
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

// ref: FUN_004f31a0
// Drop one reference; the last one takes the entry out of the table, unloads it and gives its
// slot back to the heap.
void TextureCacheDestroyTexture(void* texture) {
    auto entry = static_cast<CACHEENTRY*>(texture);

    if (!entry) {
        return;
    }

    entry->m_refCount--;

    if (static_cast<int32_t>(entry->m_refCount) >= 1) {
        return;
    }

    s_cacheTable.Unlink(entry);

    uint32_t* heap = s_entryHeap;
    entry->Unload();

    ObjectFree(*heap, entry->m_memHandle);
}

// ref: FUN_004f2dc0
// How much of a texture's file has streamed in, starting its load if it has not started. Each
// count is one higher than the file's own, the done count only once the read has finished, so a
// texture that is still reading never reports itself complete.
void TextureCacheGetStreamedBytes(void* handle, uint64_t* done, uint64_t* total) {
    auto entry = static_cast<CACHEENTRY*>(handle);

    if (!entry) {
        return;
    }

    if (!entry->IsMissing() && !entry->m_data) {
        entry->LoadTexture();
    }

    SFile::GetStreamedBytes(entry->m_fileName, done, total);

    (*total)++;

    if (!entry->m_asyncObject) {
        (*done)++;
    }
}

// ref: FUN_004f2e50
// A texture's dimensions and format, starting the load if it has not started and waiting for
// it only when the caller says it will wait. Answers 0 for a texture whose size is not known
// yet, which is why `force` exists: a caller that can draw a frame without this texture asks
// with 0 and gets told to come back, and one that cannot asks with 1 and blocks.
//
// A MISSING texture answers 1 with whatever the entry holds, rather than 0 -- the miss is
// recorded on the entry, so re-asking must not re-open the file. IsLoading carries the
// reference's re-test of that flag AFTER LoadTexture, which matters: LoadTexture is what
// discovers the file is absent.
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
                // The caller will not wait, so the answer is "not yet" either way -- but if
                // the data is coming off the network there is something useful to do first:
                // tell the queue that somebody wants this texture NOW. Off streaming mode the
                // read is local and already as fast as it gets, and the reference returns
                // without taking the lock at all.
                if (!SFile::IsStreamingMode()) {
                    return 0;
                }

                AsyncFileReadLockQueue();

                auto object = entry->m_asyncObject;

                if (AsyncReadIsUnstarted(object)) {
                    AsyncReadBumpPriority(object);
                }

                AsyncFileReadUnlockQueue();

                return 0;
            }
        }
    }

    info = entry->Info();

    return 1;
}

// ref: FUN_004f2d00
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

// ref: FUN_004f2d40
BlpPalPixel* TextureCacheGetPal(void* handle) {
    auto entry = static_cast<CACHEENTRY*>(handle);

    if (!entry || entry->IsMissing() || !entry->m_data) {
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
