#include "util/SFile.hpp"
#include <cstring>
// StormLib's error helpers are renamed to avoid colliding with Storm's (see vendor/stormlib-9.31)
#define SErrGetLastError StormLib_SErrGetLastError
#define SErrSetLastError StormLib_SErrSetLastError
#include <StormLib.h>
#undef SErrGetLastError
#undef SErrSetLastError
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <storm/Thread.hpp>

// MPQ reading is backed by StormLib. The original client carries its own implementation in
// BlizzardCore's mopaq package (SFile.cpp, SFileArchives.cpp). The archive list, priority
// ordering, and local file fallback below follow that package's observable behavior.

// Flags for SFile::OpenEx / SFile::Load
// TODO confirm against the original client's mopaq SFile flags
// SFILE_OPEN_ALLOW_LOCAL now lives in the header, beside the function that reads it.

namespace {

SArchive* s_archiveList = nullptr;

// Directory every relative path is resolved against. Empty means the working directory, which is
// what Frozen assumes everywhere else.
// TODO the original sets this from the executable's location during startup
char s_basePath[STORM_MAX_PATH] = "";

// Directories searched below the base path when opening a local file, in order
char s_dataPath[STORM_MAX_PATH] = "Data\\";
char s_localePath[STORM_MAX_PATH] = "";

// Archive-relative paths use backslashes
void SFileNormalizeArchivePath(char* dest, const char* source, size_t destsize) {
    SStrCopy(dest, source, destsize);

    for (char* c = dest; *c; ++c) {
        if (*c == '/') {
            *c = '\\';
        }
    }
}

// Local paths use forward slashes, which every supported platform accepts
void SFileNormalizeLocalPath(char* dest, const char* source, size_t destsize) {
    SStrCopy(dest, source, destsize);

    for (char* c = dest; *c; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }
}

// Resolves a local file the way the original does: the base path, then the bare file name in the
// base path, then the data directory, then the locale directory.
FILE* SFileOpenLocalStream(const char* filename) {
    const char* bareName = filename;

    for (const char* c = filename; *c; ++c) {
        if (*c == '\\' || *c == '/') {
            bareName = c + 1;
        }
    }

    char candidate[STORM_MAX_PATH];
    char path[STORM_MAX_PATH];

    SStrPrintf(candidate, sizeof(candidate), "%s%s", s_basePath, filename);
    SFileNormalizeLocalPath(path, candidate, sizeof(path));
    FILE* stream = fopen(path, "rb");

    if (!stream && bareName != filename) {
        SStrPrintf(candidate, sizeof(candidate), "%s%s", s_basePath, bareName);
        SFileNormalizeLocalPath(path, candidate, sizeof(path));
        stream = fopen(path, "rb");
    }

    if (!stream && *s_dataPath) {
        SStrPrintf(candidate, sizeof(candidate), "%s%s%s", s_basePath, s_dataPath, filename);
        SFileNormalizeLocalPath(path, candidate, sizeof(path));
        stream = fopen(path, "rb");
    }

    if (!stream && *s_localePath) {
        SStrPrintf(candidate, sizeof(candidate), "%s%s%s", s_basePath, s_localePath, filename);
        SFileNormalizeLocalPath(path, candidate, sizeof(path));
        stream = fopen(path, "rb");
    }

    return stream;
}

int32_t SFileOpenLocal(const char* filename, SFile** file) {
    FILE* stream = SFileOpenLocalStream(filename);

    if (!stream) {
        return 0;
    }

    fseek(stream, 0, SEEK_END);
    long size = ftell(stream);
    fseek(stream, 0, SEEK_SET);

    if (size < 0) {
        fclose(stream);
        return 0;
    }

    SFile* fileptr = new SFile;
    fileptr->m_filename = SStrDupA(filename, __FILE__, __LINE__);
    fileptr->m_archive = nullptr;
    fileptr->m_handle = nullptr;
    fileptr->m_localFile = stream;
    fileptr->m_size = static_cast<size_t>(size);

    *file = fileptr;

    return 1;
}

int32_t SFileOpenInArchive(SArchive* archive, const char* filename, SFile** file) {
    char path[STORM_MAX_PATH];
    SFileNormalizeArchivePath(path, filename, sizeof(path));

    HANDLE handle;

    if (!SFileOpenFileEx(archive->m_handle, path, SFILE_OPEN_FROM_MPQ, &handle)) {
        return 0;
    }

    DWORD sizeHigh = 0;
    DWORD sizeLow = SFileGetFileSize(handle, &sizeHigh);

    if (sizeLow == SFILE_INVALID_SIZE) {
        SFileCloseFile(handle);
        return 0;
    }

    SFile* fileptr = new SFile;
    fileptr->m_filename = SStrDupA(filename, __FILE__, __LINE__);
    fileptr->m_archive = archive;
    fileptr->m_handle = handle;
    fileptr->m_localFile = nullptr;
    fileptr->m_size = (static_cast<size_t>(sizeHigh) << 32) | sizeLow;

    *file = fileptr;

    return 1;
}

int32_t SFileLocalExists(const char* filename) {
    FILE* stream = SFileOpenLocalStream(filename);

    if (!stream) {
        return 0;
    }

    fclose(stream);

    return 1;
}

} // namespace

int32_t SFile::Close(SFile* file) {
    if (!file) {
        return 0;
    }

    if (file->m_archive) {
        SFileCloseFile(file->m_handle);
    } else if (file->m_localFile) {
        fclose(file->m_localFile);
    }

    SMemFree(const_cast<char*>(file->m_filename), __FILE__, __LINE__, 0);

    delete file;

    return 1;
}

int32_t SFile::CloseArchive(SArchive* archive) {
    if (!archive) {
        return 0;
    }

    SArchive** link = &s_archiveList;

    while (*link) {
        if (*link == archive) {
            *link = archive->m_next;
            break;
        }

        link = &(*link)->m_next;
    }

    SFileCloseArchive(archive->m_handle);

    delete archive;

    return 1;
}

int32_t SFile::FileExists(const char* filename) {
    char path[STORM_MAX_PATH];
    SFileNormalizeArchivePath(path, filename, sizeof(path));

    for (SArchive* archive = s_archiveList; archive; archive = archive->m_next) {
        if (SFileHasFile(archive->m_handle, path)) {
            return 1;
        }
    }

    return SFileLocalExists(filename);
}

SArchive* SFile::FirstArchive() {
    return s_archiveList;
}

int32_t SFile::GetBasePath(char* buffer, size_t buffersize) {
    SStrCopy(buffer, s_basePath, buffersize);
    return 1;
}

size_t SFile::GetFileSize(SFile* file, size_t* filesizeHigh) {
    if (filesizeHigh) {
        *filesizeHigh = 0;
    }

    return file->m_size;
}

int32_t SFile::IsStreamingMode() {
    // TODO
    return 0;
}

int32_t SFile::IsStreamingTrial() {
    // TODO
    return 0;
}

int32_t SFile::Load(SArchive* archive, const char* filename, void** buffer, size_t* bytes, size_t extraBytes, uint32_t flags, SOVERLAPPED* overlapped) {
    SFile* file;

    if (!SFile::OpenEx(archive, filename, flags, &file)) {
        return 0;
    }

    size_t size = file->m_size;
    char* data = static_cast<char*>(SMemAlloc(size + extraBytes, __FILE__, __LINE__, 0));

    size_t bytesRead = 0;

    if (size && !SFile::Read(file, data, size, &bytesRead, nullptr, nullptr)) {
        SMemFree(data, __FILE__, __LINE__, 0);
        SFile::Close(file);
        return 0;
    }

    SFile::Close(file);

    if (extraBytes) {
        memset(data + size, 0, extraBytes);
    }

    if (bytes) {
        *bytes = size;
    }

    *buffer = data;

    return 1;
}

int32_t SFile::Open(const char* filename, SFile** file) {
    return SFile::OpenEx(nullptr, filename, 0, file);
}

int32_t SFile::OpenArchive(const char* path, uint32_t priority, uint32_t flags, SArchive** archive) {
    char localPath[STORM_MAX_PATH];
    SFileNormalizeLocalPath(localPath, path, sizeof(localPath));

    HANDLE handle;

    // The client only ever reads archives; skipping the listfile and attributes avoids reading
    // and hashing several megabytes of names per archive at startup.
    DWORD openFlags = MPQ_OPEN_READ_ONLY | MPQ_OPEN_NO_LISTFILE | MPQ_OPEN_NO_ATTRIBUTES;

    if (!SFileOpenArchive(localPath, priority, openFlags, &handle)) {
        return 0;
    }

    SArchive* archiveptr = new SArchive;
    SStrCopy(archiveptr->m_path, path, sizeof(archiveptr->m_path));
    archiveptr->m_handle = handle;
    archiveptr->m_priority = priority;
    archiveptr->m_flags = flags;
    archiveptr->m_next = nullptr;

    // Insert ordered by priority, highest first. Among equal priorities the earlier opened
    // archive stays ahead.
    SArchive** link = &s_archiveList;

    while (*link && (*link)->m_priority >= priority) {
        link = &(*link)->m_next;
    }

    archiveptr->m_next = *link;
    *link = archiveptr;

    if (archive) {
        *archive = archiveptr;
    }

    return 1;
}

int32_t SFile::OpenEx(SArchive* archive, const char* filename, uint32_t flags, SFile** file) {
    *file = nullptr;

    if (archive) {
        return SFileOpenInArchive(archive, filename, file);
    }

    for (SArchive* candidate = s_archiveList; candidate; candidate = candidate->m_next) {
        if (SFileOpenInArchive(candidate, filename, file)) {
            return 1;
        }
    }

    // Local files are consulted when the caller allows it (e.g. AddOns), or when no archives are
    // open at all, which keeps a fully extracted data set usable.
    // TODO confirm the original client's precedence between archives and local files
    if ((flags & SFILE_OPEN_ALLOW_LOCAL) || !s_archiveList) {
        return SFileOpenLocal(filename, file);
    }

    return 0;
}

int32_t SFile::Read(SFile* file, void* buffer, size_t bytestoread, size_t* bytesread, SOVERLAPPED* overlapped, TASYNCPARAMBLOCK* asyncparam) {
    if (file->m_archive) {
        DWORD read = 0;
        bool ok = SFileReadFile(file->m_handle, buffer, static_cast<DWORD>(bytestoread), &read, nullptr);

        if (bytesread) {
            *bytesread = read;
        }

        // A short read at the end of the file is reported as a failure by StormLib, but the
        // original returns success whenever any bytes were delivered.
        return ok || read > 0;
    }

    size_t read = fread(buffer, 1, bytestoread, file->m_localFile);

    if (bytesread) {
        *bytesread = read;
    }

    return bytestoread == 0 || read > 0;
}

int32_t SFile::SetBasePath(const char* path) {
    SStrCopy(s_basePath, path, sizeof(s_basePath));

    size_t length = SStrLen(s_basePath);

    if (length && s_basePath[length - 1] != '\\' && s_basePath[length - 1] != '/') {
        SStrPack(s_basePath, "\\", sizeof(s_basePath));
    }

    return 1;
}

int32_t SFile::SetLocalePath(const char* path) {
    SStrCopy(s_localePath, path, sizeof(s_localePath));

    size_t length = SStrLen(s_localePath);

    if (length && s_localePath[length - 1] != '\\' && s_localePath[length - 1] != '/') {
        SStrPack(s_localePath, "\\", sizeof(s_localePath));
    }

    return 1;
}

int32_t SFile::Unload(void* ptr) {
    SMemFree(ptr, __FILE__, __LINE__, 0);
    return 1;
}

// ref: FUN_004283b0
void SFileCritSectEnter(SCritSect* critSect) {
    critSect->Enter();
}

// ref: FUN_004283c0
void SFileCritSectLeave(SCritSect* critSect) {
    critSect->Leave();
}
