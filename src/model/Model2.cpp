#include "util/SFile.hpp"
#include <storm/Error.hpp>
#include <storm/Hash.hpp>
#include "model/Model2.hpp"
#include "model/CM2Cache.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include "model/M2Internal.hpp"
#include "console/CVar.hpp"
#include "console/Console.hpp"
#include "util/Filesystem.hpp"
#include <cstring>
#include <new>
#include <common/ObjectAlloc.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>

static CVar* s_M2UseZFillVar;
static CVar* s_M2UseClipPlanesVar;
static CVar* s_M2UseThreadsVar;
static CVar* s_M2BatchDoodadsVar;
static CVar* s_M2BatchParticlesVar;
static CVar* s_M2ForceAdditiveParticleSortVar;
static CVar* s_M2FasterVar;
static CVar* s_M2FasterDebugVar;

// ref: FUN_00402100
uint32_t M2ConvertFasterFlags(int32_t faster, int32_t debugFaster) {
    uint32_t flags = 0x0;

    switch (faster) {
        case 0: {
            break;
        }

        case 1: {
            return 0x2000 | 0x4000 | 0x8000;
        }

        case 2:
        case 3: {
            flags = 0x2000;
            return flags;
        }

        default: {
            return flags;
        }
    }

    if (debugFaster) {
        // The ones digit picks the level, the hundreds digit demands a capability the reference
        // checks through FUN_0047d230(2) (unported); when that check fails no flags are set.
        int32_t level = debugFaster % 10;

        if (level == 1) {
            flags = 0x2000;
        } else if (level == 2) {
            flags = 0x2000 | 0x4000;
        } else if (level == 3) {
            flags = 0x2000 | 0x4000 | 0x8000;
        }

        if ((debugFaster / 100) % 10 == 0) {
            return flags;
        }

        // TODO FUN_0047d230(2) == 0 -> return flags
        return flags;
    }

    return 0;
}

bool BatchDoodadsCallback(CVar* cvar, char const* oldValue, char const* newValue, void* userArg) {
    int32_t enabled = SStrToInt(newValue);
    uint32_t flags = M2GetCacheFlags();

    if (enabled) {
        M2SetCacheFlags(flags | 0x20);
        ConsoleWrite("Doodad batching enabled.", DEFAULT_COLOR);

        return true;
    }

    M2SetCacheFlags(flags & ~0x20);
    ConsoleWrite("Doodad batching disabled.", DEFAULT_COLOR);

    return true;
}

// ref: FUN_00402470
bool BatchParticlesCallback(CVar* cvar, char const* oldValue, char const* newValue, void* userArg) {
    int32_t enabled = SStrToInt(newValue);
    uint32_t flags = M2GetCacheFlags();

    if (enabled) {
        M2SetCacheFlags(flags | 0x80);
        ConsoleWrite("Particle batching enabled.", DEFAULT_COLOR);

        return true;
    }

    M2SetCacheFlags(flags & ~0x80);
    ConsoleWrite("Particle batching disabled.", DEFAULT_COLOR);

    return true;
}

// ref: FUN_004024d0
bool ForceAdditiveParticleSortCallback(CVar* cvar, char const* oldValue, char const* newValue, void* userArg) {
    int32_t enabled = SStrToInt(newValue);
    uint32_t flags = M2GetCacheFlags();

    if (enabled) {
        M2SetCacheFlags(flags | 0x100);
        ConsoleWrite("Sorting all particles as though they were additive.", DEFAULT_COLOR);

        return true;
    }

    M2SetCacheFlags(flags & ~0x100);
    ConsoleWrite("Sorting particles normally.", DEFAULT_COLOR);

    return true;
}

// ref: FUN_004021c0
bool M2FasterChanged(CVar* cvar, char const* oldValue, char const* newValue, void* userArg) {
    int32_t faster = SStrToInt(newValue);

    if (s_M2FasterDebugVar) {
        M2SetGlobalOptFlags(M2ConvertFasterFlags(faster, s_M2FasterDebugVar->GetInt()));

        return true;
    }

    M2SetGlobalOptFlags(M2ConvertFasterFlags(faster, 0));

    return true;
}

// ref: FUN_00402210
bool M2DebugFasterChanged(CVar* cvar, char const* oldValue, char const* newValue, void* userArg) {
    int32_t faster = s_M2FasterVar ? s_M2FasterVar->GetInt() : 0;

    M2SetGlobalOptFlags(M2ConvertFasterFlags(faster, SStrToInt(newValue)));

    return true;
}

int32_t M2ConvertModelFileName(const char* source, char* dest, uint32_t a3, uint32_t a4) {
    SStrCopy(dest, source, a3);
    SStrLower(dest);

    char* ext = OsPathFindExtensionWithDot(dest);

    if ((a4 & 0x1000) != 0) {
        return 1;
    }

    int32_t invalidExt =
        !*ext || (strcmp(ext, ".mdl") && strcmp(ext, ".mdx") && strcmp(ext, ".m2"));

    if (invalidExt) {
        // TODO
        // OsOutputDebugString("Model2: Invalid file extension: %s\n", dest);

        return 0;
    }

    if (!strcmp(ext, ".m2")) {
        return 1;
    }

    strcpy(ext, ".m2");

    return 1;
}

// ref: FUN_0081c080
CM2Scene* M2CreateScene() {
    auto m = SMemAlloc(sizeof(CM2Scene), __FILE__, __LINE__, 0x0);
    return new (m) CM2Scene(&CM2Cache::s_cache);
}

// ref: FUN_0081c0b0
uint32_t M2GetCacheFlags() {
    return CM2Cache::s_cache.m_flags;
}

// ref: FUN_0081c0c0
void M2SetCacheFlags(uint32_t flags) {
    CM2Cache::s_cache.m_flags = flags;
}

// ref: FUN_0081c6e0
void M2Initialize(uint16_t flags, uint32_t a2) {
    CM2Cache::s_cache.Initialize(flags);

    if (!a2) {
        a2 = 2048;
    }

    uint32_t* heapId = static_cast<uint32_t*>(SMemAlloc(sizeof(uint32_t), __FILE__, __LINE__, 0));
    *heapId = ObjectAllocAddHeap(sizeof(CM2Model), a2, "CM2Model", 1);

    g_modelPool = heapId;
}

// ref: FUN_0081c750
// The model system's shutdown: the cache (its thread, its queued shared models, the particle
// index buffer), the emitter pool the duplicate path fills, and the CM2Model heap id. The
// reference also releases a ribbon singleton (FUN_009812b0, global 0x00dce8d0) that is never
// assigned anywhere in the binary, so that call frees nothing and is not carried.
void M2Destroy() {
    CM2Cache::s_cache.Destroy();
    M2ParticleEmitterPoolDestroy();

    if (g_modelPool) {
        SMemFree(g_modelPool, __FILE__, __LINE__, 0);
    }

    g_modelPool = nullptr;
}

// ref: FUN_0081c060
void M2SetGlobalOptFlags(uint16_t flags) {
    flags &= (0x2000 | 0x4000 | 0x8000);
    CM2Cache::s_cache.m_flags |= flags;
}

uint32_t M2RegisterCVars() {
    s_M2UseZFillVar = CVar::Register(
        "M2UseZFill",
        "z-fill transparent objects",
        0,
        "1",
        nullptr,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_M2UseClipPlanesVar = CVar::Register(
        "M2UseClipPlanes",
        "use clip planes for sorting transparent objects",
        0,
        "1",
        nullptr,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_M2UseThreadsVar = CVar::Register(
        "M2UseThreads",
        "multithread model animations",
        0,
        "1",
        nullptr,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_M2BatchDoodadsVar = CVar::Register(
        "M2BatchDoodads",
        "combine doodads to reduce batch count",
        0,
        "1",
        BatchDoodadsCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_M2BatchParticlesVar = CVar::Register(
        "M2BatchParticles",
        "combine particle emitters to reduce batch count",
        0,
        "1",
        BatchParticlesCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_M2ForceAdditiveParticleSortVar = CVar::Register(
        "M2ForceAdditiveParticleSort",
        "force all particles to sort as though they were additive",
        0,
        "0",
        ForceAdditiveParticleSortCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    s_M2FasterVar = CVar::Register(
        "M2Faster",
        "end user control of scene optimization mode - (0-3)",
        0,
        "1",
        M2FasterChanged,
        1,
        false,
        nullptr,
        false
    );

    s_M2FasterDebugVar = CVar::Register(
        "M2FasterDebug",
        "programmer control of scene optimization mode",
        0,
        "0",
        M2DebugFasterChanged,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    uint32_t flags = 0;

    if (s_M2UseZFillVar->GetInt()) {
        flags |= 0x1;
    }

    if (s_M2UseClipPlanesVar->GetInt()) {
        flags |= 0x2;
    }

    if (s_M2UseThreadsVar->GetInt()) {
        flags |= 0x4;
    }

    if (s_M2BatchDoodadsVar->GetInt()) {
        flags |= 0x20;
    }

    if (s_M2BatchParticlesVar->GetInt()) {
        flags |= 0x80;
    }

    if (s_M2ForceAdditiveParticleSortVar->GetInt()) {
        flags |= 0x100;
    }

    flags |= 0x8;

    return flags;
}

// The model blob (ModelBlob.cpp): world\model.blob holds one 0x1c-byte record per model -- a
// name offset and an axis-aligned box -- and the client hashes them by name so CM2Cache::
// CreateShared can hand each shared model its authored bounds (CM2Shared::aaBox154).
//
// The file is three chunks of {tag, size, data}: one the loader skips, the records, the names.
struct ModelBlobRecord {
    uint32_t nameOffset;
    C3Vector min;
    C3Vector max;
};

class CModelBlob {
    public:
        class CHashEntry : public TSHashObject<CHashEntry, HASHKEY_STRI> {
            public:
                CModelBlob* m_blob = nullptr;
                const ModelBlobRecord* m_record = nullptr;
        };

        char m_path[STORM_MAX_PATH] = {};
        uint8_t* m_data = nullptr;
        const ModelBlobRecord* m_records = nullptr;
        uint32_t m_recordCount = 0;
        const char* m_names = nullptr;

        ~CModelBlob();
        void Load(const char* path);
        void AddEntry(const ModelBlobRecord* record);
        int32_t Query(const char* name, C3Vector& min, C3Vector& max);
};

// Reference 0x00b4a274: the records by name.
static TSHashTable<CModelBlob::CHashEntry, HASHKEY_STRI> s_modelBlobTable;

// Reference 0x00b4a270.
static CModelBlob* s_modelBlob;

// ref: FUN_004baf60
CModelBlob::~CModelBlob() {
    if (this->m_data) {
        SMemFree(this->m_data, __FILE__, __LINE__, 0);
    }

    this->m_records = nullptr;
    this->m_recordCount = 0;
    this->m_names = nullptr;

    s_modelBlobTable.Clear();
}

// ref: FUN_004bba70
// A name already in the table keeps its first record.
void CModelBlob::AddEntry(const ModelBlobRecord* record) {
    auto name = this->m_names + record->nameOffset;

    if (s_modelBlobTable.Ptr(name)) {
        return;
    }

    auto entry = s_modelBlobTable.New(name, 0, 0);
    entry->m_blob = this;
    entry->m_record = record;
}

// ref: FUN_004bbb20
void CModelBlob::Load(const char* path) {
    SFile* file = nullptr;
    SErrSetLastError(0);

    if (!SFile::OpenEx(nullptr, path, 0, &file)) {
        if (!SErrGetLastError()) {
            SErrSetLastError(2);
        }

        return;
    }

    SStrCopy(this->m_path, path, sizeof(this->m_path));

    auto size = SFile::GetFileSize(file, nullptr);
    this->m_data = static_cast<uint8_t*>(SMemAlloc(size, __FILE__, __LINE__, 0));
    SFile::Read(file, this->m_data, size, nullptr, nullptr, nullptr);
    SFile::Close(file);

    auto chunk = this->m_data + 8 + *reinterpret_cast<uint32_t*>(this->m_data + 4);
    auto recordBytes = *reinterpret_cast<uint32_t*>(chunk + 4);

    this->m_records = reinterpret_cast<const ModelBlobRecord*>(chunk + 8);
    this->m_recordCount = recordBytes / sizeof(ModelBlobRecord);
    this->m_names = reinterpret_cast<const char*>(chunk + 8) + recordBytes + 8;

    for (uint32_t i = 0; i < this->m_recordCount; i++) {
        this->AddEntry(&this->m_records[i]);
    }
}

// ref: FUN_004bb370
int32_t CModelBlob::Query(const char* name, C3Vector& min, C3Vector& max) {
    auto entry = s_modelBlobTable.Ptr(name);

    if (!entry) {
        return 0;
    }

    min = entry->m_record->min;
    max = entry->m_record->max;

    return 1;
}

// ref: FUN_004bbc20
int32_t ModelBlobLoad(const char* path) {
    ModelBlobDestroy();

    auto m = SMemAlloc(sizeof(CModelBlob), __FILE__, __LINE__, 0);
    s_modelBlob = m ? new (m) CModelBlob() : nullptr;

    if (s_modelBlob) {
        s_modelBlob->Load(path);
    }

    if (!s_modelBlob || !s_modelBlob->m_data) {
        ModelBlobDestroy();
        return 0;
    }

    return 1;
}

// ref: FUN_004bb1c0
void ModelBlobDestroy() {
    if (s_modelBlob) {
        s_modelBlob->~CModelBlob();
        SMemFree(s_modelBlob, __FILE__, __LINE__, 0);
        s_modelBlob = nullptr;
    }
}

// ref: FUN_004bb3e0
// The name arrives without its extension (CM2Cache::CreateShared cuts it off for the call).
int32_t ModelBlobQuery(const char* name, C3Vector& min, C3Vector& max) {
    if (!s_modelBlob) {
        return 0;
    }

    return s_modelBlob->Query(name, min, max);
}
