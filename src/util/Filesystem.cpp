#include "util/Filesystem.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <storm/String.hpp>

#if defined(WHOA_SYSTEM_WIN)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// Converts to the separator the platform accepts everywhere
static void OsLocalizePath(char* dest, const char* source, size_t destsize) {
    SStrCopy(dest, source, destsize);

    for (char* c = dest; *c; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }
}

static void OsMakeDirectory(const char* path) {
#if defined(WHOA_SYSTEM_WIN)
    CreateDirectoryA(path, nullptr);
#else
    mkdir(path, 0755);
#endif
}

void OsCreateDirectory(const char* pathName, int32_t recursive) {
    char path[STORM_MAX_PATH];
    OsLocalizePath(path, pathName, sizeof(path));

    if (recursive) {
        for (char* c = path + 1; *c; ++c) {
            if (*c == '/') {
                *c = '\0';
                OsMakeDirectory(path);
                *c = '/';
            }
        }
    }

    OsMakeDirectory(path);
}

int32_t OsChangeDirectory(const char* pathName) {
    char path[STORM_MAX_PATH];
    OsLocalizePath(path, pathName, sizeof(path));

#if defined(WHOA_SYSTEM_WIN)
    return SetCurrentDirectoryA(path) != 0;
#else
    return chdir(path) == 0;
#endif
}

int32_t OsDirectoryExists(const char* pathName) {
    char path[STORM_MAX_PATH];
    OsLocalizePath(path, pathName, sizeof(path));

#if defined(WHOA_SYSTEM_WIN)
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

int32_t OsFileExists(const char* pathName) {
    char path[STORM_MAX_PATH];
    OsLocalizePath(path, pathName, sizeof(path));

#if defined(WHOA_SYSTEM_WIN)
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat info;
    return stat(path, &info) == 0 && !S_ISDIR(info.st_mode);
#endif
}

// Wildcard match (0x428210 in the original)
int32_t OsFileMatch(const char* name, const char* pattern) {
    while (*pattern) {
        if (*pattern == '*') {
            while (*pattern == '*') {
                ++pattern;
            }

            if (!*pattern) {
                return 1;
            }

            for (; *name; ++name) {
                if (OsFileMatch(name, pattern)) {
                    return 1;
                }
            }

            return 0;
        }

        if (!*name) {
            return 0;
        }

        if (*pattern == '?') {
            ++pattern;
            ++name;
            continue;
        }

        char expected = *pattern;

        if (expected == '\\' && pattern[1]) {
            ++pattern;
            expected = *pattern;
        }

        if (expected != *name && tolower(static_cast<unsigned char>(expected)) != tolower(static_cast<unsigned char>(*name))) {
            return 0;
        }

        ++pattern;
        ++name;
    }

    return *name == '\0';
}

int32_t OsFileEnumerate(const char* dir, const char* pattern, OSFILEENUMCALLBACK callback, void* param, int32_t includeDirectories) {
    char path[STORM_MAX_PATH];
    OsLocalizePath(path, dir, sizeof(path));

    OSFILEENTRY entry;

#if defined(WHOA_SYSTEM_WIN)
    char search[STORM_MAX_PATH];
    SStrPrintf(search, sizeof(search), "%s*", path);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(search, &data);

    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        entry.attributes = 0;

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            entry.attributes |= OS_FILE_ATTRIBUTE_DIRECTORY;
        }

        if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
            entry.attributes |= OS_FILE_ATTRIBUTE_READONLY;
        }

        if (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) {
            entry.attributes |= OS_FILE_ATTRIBUTE_HIDDEN;
        }

        if ((entry.attributes & OS_FILE_ATTRIBUTE_DIRECTORY) && !includeDirectories) {
            continue;
        }

        if (!OsFileMatch(data.cFileName, pattern)) {
            continue;
        }

        entry.size = data.nFileSizeLow;
        SStrCopy(entry.name, data.cFileName, sizeof(entry.name));

        if (callback(&entry, param)) {
            break;
        }
    } while (FindNextFileA(find, &data));

    FindClose(find);
#else
    DIR* handle = opendir(path);

    if (!handle) {
        return 0;
    }

    while (dirent* dirEntry = readdir(handle)) {
        if (!OsFileMatch(dirEntry->d_name, pattern)) {
            continue;
        }

        char entryPath[STORM_MAX_PATH];
        SStrPrintf(entryPath, sizeof(entryPath), "%s%s", path, dirEntry->d_name);

        struct stat info;

        if (stat(entryPath, &info) != 0) {
            continue;
        }

        entry.attributes = 0;

        if (S_ISDIR(info.st_mode)) {
            entry.attributes |= OS_FILE_ATTRIBUTE_DIRECTORY;
        }

        if ((entry.attributes & OS_FILE_ATTRIBUTE_DIRECTORY) && !includeDirectories) {
            continue;
        }

        entry.size = static_cast<uint32_t>(info.st_size);
        SStrCopy(entry.name, dirEntry->d_name, sizeof(entry.name));

        if (callback(&entry, param)) {
            break;
        }
    }

    closedir(handle);
#endif

    return 1;
}

void OsBuildFontFilePath(const char* fileName, char* buffer, size_t size) {
    SStrPrintf(buffer, size, "%s\\%s", "Fonts", fileName);
}

char* OsPathFindExtensionWithDot(char* pathName) {
    char* v1;
    char* result;

    v1 = strrchr(pathName, '\\');

    if (!v1) {
        v1 = strrchr(pathName, '/');
    }

    result = strrchr(pathName, '.');

    if (!result || (v1 && v1 >= result)) {
        result = (char*)&pathName[strlen(pathName)];
    }

    return result;
}
