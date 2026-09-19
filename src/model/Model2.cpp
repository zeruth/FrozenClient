#include "model/Model2.hpp"
#include "model/CM2Cache.hpp"
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

void M2Initialize(uint16_t flags, uint32_t a2) {
    CM2Cache::s_cache.Initialize(flags);

    if (!a2) {
        a2 = 2048;
    }

    uint32_t* heapId = static_cast<uint32_t*>(SMemAlloc(sizeof(uint32_t), __FILE__, __LINE__, 0));
    *heapId = ObjectAllocAddHeap(sizeof(CM2Model), a2, "CM2Model", 1);

    g_modelPool = heapId;
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

int32_t ModelBlobQuery(const char* a1, C3Vector& a2, C3Vector& a3) {
    // TODO
    return 0;
}
