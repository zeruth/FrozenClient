#include "client/Archive.hpp"
#include "console/CVar.hpp"
#include "util/Filesystem.hpp"
#include "util/SFile.hpp"
#include <cstdlib>
#include <cstring>
#include <storm/Array.hpp>
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>

// Archive table entry types
enum ARCHIVE_TYPE {
    ARCHIVE_TYPE_REQUIRED   = 1, // Fatal error if missing
    ARCHIVE_TYPE_OPTIONAL   = 2, // Opened if present; skipped entirely in streaming mode
    ARCHIVE_TYPE_ALTERNATE  = 3, // alternate.MPQ, also looked for inside each patch archive
    ARCHIVE_TYPE_STREAMING  = 4  // Only opened in streaming mode
};

// Archive table layout flags; which set applies is decided by the presence of Data\common.MPQ
enum ARCHIVE_LAYOUT {
    ARCHIVE_LAYOUT_LEGACY   = 0x1, // Split archives (dbc.MPQ, model.MPQ, ...)
    ARCHIVE_LAYOUT_COMMON   = 0x2  // common.MPQ and friends
};

struct ARCHIVEENTRY {
    const char* name;
    uint32_t type;
    uint32_t layout;
    bool loaded;
};

struct PATCHARCHIVEENTRY {
    int32_t pass;
    int32_t alternate;
    bool useRetailPath;
    const char* dirFormat;
    const char* nameFormat;
};

struct PATCHARCHIVELIST {
    const char* dir;
    TSGrowableArray<char*> names;
};

// Archive table (0xAB6168 in the original). "****" is replaced with the locale.
static ARCHIVEENTRY s_archiveTable[] = {
    { "alternate.MPQ",                  ARCHIVE_TYPE_ALTERNATE, ARCHIVE_LAYOUT_LEGACY | ARCHIVE_LAYOUT_COMMON, false },
    { "interface.MPQ",                  ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "misc.MPQ",                       ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "model.MPQ",                      ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "texture.MPQ",                    ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "terrain.MPQ",                    ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "wmo.MPQ",                        ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "sound.MPQ",                      ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "fonts.MPQ",                      ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "dbc.MPQ",                        ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "speech.MPQ",                     ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "expansionloc.MPQ",               ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "lichkingloc.MPQ",                ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "expansionspeech.MPQ",            ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "lichkingspeech.MPQ",             ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "expansion.MPQ",                  ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_LEGACY | ARCHIVE_LAYOUT_COMMON, false },
    { "lichking.MPQ",                   ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_LEGACY | ARCHIVE_LAYOUT_COMMON, false },
    { "common.MPQ",                     ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_COMMON,                        false },
    { "common-2.MPQ",                   ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_COMMON,                        false },
    { "****\\locale-****.MPQ",          ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_COMMON,                        false },
    { "****\\speech-****.MPQ",          ARCHIVE_TYPE_REQUIRED,  ARCHIVE_LAYOUT_COMMON,                        false },
    { "****\\expansion-locale-****.MPQ", ARCHIVE_TYPE_OPTIONAL, ARCHIVE_LAYOUT_COMMON,                        false },
    { "****\\lichking-locale-****.MPQ", ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_COMMON,                        false },
    { "****\\expansion-speech-****.MPQ", ARCHIVE_TYPE_OPTIONAL, ARCHIVE_LAYOUT_COMMON,                        false },
    { "****\\lichking-speech-****.MPQ", ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_COMMON,                        false },
    { "development.MPQ",                ARCHIVE_TYPE_OPTIONAL,  ARCHIVE_LAYOUT_LEGACY,                        false },
    { "streaming.MPQ",                  ARCHIVE_TYPE_STREAMING, ARCHIVE_LAYOUT_LEGACY,                        false },
    { "streamingloc.MPQ",               ARCHIVE_TYPE_STREAMING, ARCHIVE_LAYOUT_LEGACY,                        false },
};

#define ARCHIVE_TABLE_COUNT (sizeof(s_archiveTable) / sizeof(s_archiveTable[0]))

// Table indices of archives with special handling
#define ARCHIVE_INDEX_EXPANSION 15
#define ARCHIVE_INDEX_LICHKING  16
#define ARCHIVE_INDEX_COMMON    17

// Directories searched for each table archive, in order (0xAB6158 in the original). The original
// has a third entry, "Data\", relative to the retail install path found in the registry, which
// only applies to PTR builds.
// TODO retail install path lookup
static const char* s_archiveDirs[] = {
    "Data\\",
    "..\\Data\\",
};

#define ARCHIVE_DIR_COUNT (sizeof(s_archiveDirs) / sizeof(s_archiveDirs[0]))

// Patch archive search entries (built on the stack by the original at 0x405AB0). Pass 0 entries
// are discovered by wildcard and sorted; pass 1 entries are appended in table order. Entries
// flagged "alternate" are only used when the caller asks for the alternate set.
static const PATCHARCHIVEENTRY s_patchArchiveEntries[] = {
    { 0, 0, false, "Data\\",        "patch-?.MPQ"    },
    { 0, 0, false, "Data\\%s\\",    "patch-%s-?.MPQ" },
    { 1, 0, false, "Data\\",        "patch.MPQ"      },
    { 1, 0, false, "Data\\%s\\",    "patch-%s.MPQ"   },
    { 1, 1, false, "Data\\",        "patch-4.MPQ"    },
    { 1, 1, false, "Data\\%s\\",    "patch-%s-4.MPQ" },
    { 1, 1, false, "Data\\",        "patch-3.MPQ"    },
    { 1, 1, false, "Data\\%s\\",    "patch-%s-3.MPQ" },
    { 1, 1, false, "..\\Data\\",    "patch-2.MPQ"    },
    { 1, 1, false, "..\\Data\\%s\\", "patch-%s-2.MPQ" },
    { 1, 1, false, "..\\Data\\",    "patch.MPQ"      },
    { 1, 1, false, "..\\Data\\%s\\", "patch-%s.MPQ"   },
};

#define PATCH_ARCHIVE_ENTRY_COUNT (sizeof(s_patchArchiveEntries) / sizeof(s_patchArchiveEntries[0]))

// Base priority for patch archives; the base archive table counts down from one below this
#define PATCH_ARCHIVE_BASE_PRIORITY 0x40

// Flags passed to SFile::OpenArchive by the original client
#define ARCHIVE_OPEN_FLAGS 0xC00

static TSGrowableArray<SArchive*> s_archives;
static SArchive* s_archiveTableHandles[ARCHIVE_TABLE_COUNT];
static uint32_t s_startArchivePriority = 0;
static uint8_t s_installedExpansionLevel = 0;

// Replaces every "****" in path with the four character locale code
static void ApplyLocale(char* path, const char* locale) {
    char* marker;

    while ((marker = SStrStr(path, "****"))) {
        memcpy(marker, locale, 4);
    }
}

// Callback for patch archive discovery (0x405A10 in the original)
static int32_t AddPatchArchive(const OSFILEENTRY* entry, void* param) {
    auto list = static_cast<PATCHARCHIVELIST*>(param);

    char path[STORM_MAX_PATH];
    SStrPrintf(path, sizeof(path), "%s%s", list->dir, entry->name);

    *list->names.New() = SStrDupA(path, __FILE__, __LINE__);

    return 0;
}

// Sort comparator for discovered patch archives (0x401200 in the original): descending,
// case-insensitive. Priorities are handed out from the end of the sorted list, so the archive
// sorting first receives the highest priority.
static int ComparePatchArchives(const void* a, const void* b) {
    auto nameA = *static_cast<const char* const*>(a);
    auto nameB = *static_cast<const char* const*>(b);

    return -SStrCmpI(nameA, nameB, STORM_MAX_STR);
}

// Builds the list of patch archives to open (0x405AB0 in the original)
static void BuildPatchArchiveList(PATCHARCHIVELIST* list, const char* locale, int32_t alternate) {
    char dir[256];
    char name[256];

    list->dir = dir;

    for (int32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < PATCH_ARCHIVE_ENTRY_COUNT; ++i) {
            const PATCHARCHIVEENTRY& entry = s_patchArchiveEntries[i];

            if (entry.pass != pass || entry.alternate != (alternate != 0)) {
                continue;
            }

            SStrPrintf(dir, sizeof(dir), entry.dirFormat, locale);
            SStrPrintf(name, sizeof(name), entry.nameFormat, locale);

            // TODO retail install path prefix when entry.useRetailPath is set

            OsFileEnumerate(dir, name, &AddPatchArchive, list, 0);
        }

        if (pass == 0 && list->names.Count() > 1) {
            qsort(list->names.m_data, list->names.Count(), sizeof(char*), &ComparePatchArchives);
        }
    }

    list->dir = nullptr;
}

// Opens a table archive, trying each data directory in turn (0x404930 in the original)
static bool OpenTableArchive(const char* name, uint32_t priority, const char* locale, SArchive** archive) {
    for (uint32_t i = 0; i < ARCHIVE_DIR_COUNT; ++i) {
        char path[STORM_MAX_PATH];
        SStrPrintf(path, sizeof(path), "%s%s", s_archiveDirs[i], name);
        ApplyLocale(path, locale);

        if (SFile::OpenArchive(path, priority, ARCHIVE_OPEN_FLAGS, archive)) {
            return true;
        }

        // TODO the original stops early on hard errors (access denied, corrupt archive) and only
        // falls through to the next directory when the archive simply isn't there
    }

    return false;
}

// Decides which archive layout is installed (0x402B90 in the original)
static bool HasCommonArchiveLayout() {
    for (uint32_t i = 0; i < ARCHIVE_DIR_COUNT; ++i) {
        char path[STORM_MAX_PATH];
        SStrPrintf(path, sizeof(path), "%s%s", s_archiveDirs[i], s_archiveTable[ARCHIVE_INDEX_COMMON].name);
        ApplyLocale(path, "----");

        if (OsFileExists(path)) {
            return true;
        }
    }

    return false;
}

void ClientOpenArchives() {
    // TODO the original always requires the archives; Whoa still supports running from a fully
    // extracted data set, so a missing Data directory switches to local files only
    if (!OsDirectoryExists("Data")) {
        return;
    }

    uint32_t layout = HasCommonArchiveLayout() ? ARCHIVE_LAYOUT_COMMON : ARCHIVE_LAYOUT_LEGACY;

    for (uint32_t i = 0; i < ARCHIVE_TABLE_COUNT; ++i) {
        s_archiveTable[i].loaded = false;
        s_archiveTableHandles[i] = nullptr;
    }

    const char* locale = "----";

    if (layout & ARCHIVE_LAYOUT_COMMON) {
        locale = CVar::Lookup("locale")->GetString();
    }

    // Patch archives, highest priority first in the list, so open from the back

    PATCHARCHIVELIST patches;
    BuildPatchArchiveList(&patches, locale, 0);

    uint32_t priority = PATCH_ARCHIVE_BASE_PRIORITY;

    for (uint32_t i = patches.names.Count(); i > 0; --i) {
        const char* path = patches.names[i - 1];
        SArchive* archive;

        if (SFile::OpenArchive(path, priority, ARCHIVE_OPEN_FLAGS, &archive)) {
            *s_archives.New() = archive;

            if (SStrStr(path, "Start.MPQ")) {
                s_startArchivePriority = priority;
            }

            ++priority;
        }
    }

    uint32_t patchArchiveCount = s_archives.Count();

    // Alternate archive from disk

    for (uint32_t i = 0; i < ARCHIVE_TABLE_COUNT; ++i) {
        ARCHIVEENTRY& entry = s_archiveTable[i];

        if (entry.type != ARCHIVE_TYPE_ALTERNATE) {
            continue;
        }

        SArchive* archive;

        if (OpenTableArchive(entry.name, priority, locale, &archive)) {
            *s_archives.New() = archive;
            s_archiveTableHandles[i] = archive;
            entry.loaded = true;
            ++priority;
        }
    }

    // Alternate archive nested inside each patch archive
    // TODO the original opens an archive named "alternate" from within every patch archive when
    // alternate.MPQ was not found on disk and the locale is not zhCN, enCN, zhTW, or enTW. That
    // needs archive-in-archive support in SFile.
    (void)patchArchiveCount;

    // Base archives, counting down in priority through the table

    uint32_t basePriority = PATCH_ARCHIVE_BASE_PRIORITY - 1;

    for (uint32_t i = 0; i < ARCHIVE_TABLE_COUNT; ++i) {
        ARCHIVEENTRY& entry = s_archiveTable[i];

        if (entry.type == ARCHIVE_TYPE_ALTERNATE || entry.type == ARCHIVE_TYPE_STREAMING) {
            continue;
        }

        if (!(entry.layout & layout)) {
            continue;
        }

        // Optional archives are never opened in streaming mode
        if (SFile::IsStreamingMode() && entry.type == ARCHIVE_TYPE_OPTIONAL) {
            continue;
        }

        SArchive* archive;

        if (OpenTableArchive(entry.name, basePriority, locale, &archive)) {
            *s_archives.New() = archive;
            s_archiveTableHandles[i] = archive;
        } else if (entry.type == ARCHIVE_TYPE_REQUIRED) {
            char path[STORM_MAX_PATH];
            SStrPrintf(path, sizeof(path), "%s%s", s_archiveDirs[0], entry.name);
            ApplyLocale(path, locale);

            char message[256];
            SStrPrintf(message, sizeof(message), "Failed to open archive %s. Missing or corrupted data", path);

            // TODO the original consults a command line flag that downgrades this to a warning
            SErrDisplayError(SErrGetLastError(), __FILE__, __LINE__, message, 0, 1, 0x11111111);
        }

        entry.loaded = true;
        --basePriority;
    }

    // Streaming archives

    if (SFile::IsStreamingMode()) {
        for (uint32_t i = 0; i < ARCHIVE_TABLE_COUNT; ++i) {
            ARCHIVEENTRY& entry = s_archiveTable[i];

            if (entry.type != ARCHIVE_TYPE_STREAMING) {
                continue;
            }

            SArchive* archive;

            if (OpenTableArchive(entry.name, basePriority, locale, &archive)) {
                *s_archives.New() = archive;
                s_archiveTableHandles[i] = archive;
                entry.loaded = true;
            }
        }
    }

    if (s_archiveTableHandles[ARCHIVE_INDEX_LICHKING]) {
        s_installedExpansionLevel = 2;
    } else {
        s_installedExpansionLevel = s_archiveTableHandles[ARCHIVE_INDEX_EXPANSION] ? 1 : 0;
    }

    for (uint32_t i = 0; i < patches.names.Count(); ++i) {
        SMemFree(patches.names[i], __FILE__, __LINE__, 0);
    }
}

void ClientCloseArchives() {
    for (uint32_t i = s_archives.Count(); i > 0; --i) {
        SFile::CloseArchive(s_archives[i - 1]);
    }

    s_archives.Clear();

    for (uint32_t i = 0; i < ARCHIVE_TABLE_COUNT; ++i) {
        s_archiveTable[i].loaded = false;
        s_archiveTableHandles[i] = nullptr;
    }
}

uint8_t ClientGetInstalledExpansionLevel() {
    return s_installedExpansionLevel;
}
