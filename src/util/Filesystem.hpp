#ifndef UTIL_FILESYSTEM_HPP
#define UTIL_FILESYSTEM_HPP

#include <cstddef>
#include <cstdint>
#include <storm/String.hpp>

// Attribute bits reported in OSFILEENTRY::attributes
#define OS_FILE_ATTRIBUTE_READONLY  0x1
#define OS_FILE_ATTRIBUTE_HIDDEN    0x2
#define OS_FILE_ATTRIBUTE_DIRECTORY 0x10

struct OSFILEENTRY {
    uint32_t size;
    uint32_t attributes;
    char name[STORM_MAX_PATH];
};

// Returns nonzero to stop enumeration
typedef int32_t (*OSFILEENUMCALLBACK)(const OSFILEENTRY* entry, void* param);

void OsCreateDirectory(const char*, int32_t);

// Make path the working directory. Nonzero on success.
//
// Needed because parts of the client resolve data paths relative to where the process is running,
// not against SFile's base path -- ClientOpenArchives looks for "Data" that way. A tool that wants
// to read the client's archives has to move there first.
int32_t OsChangeDirectory(const char* path);

int32_t OsDirectoryExists(const char* path);

int32_t OsFileExists(const char* path);

// Calls callback for every entry in dir whose name matches pattern. '?' matches exactly one
// character, '*' any run of characters, and '\' escapes the next pattern character. Matching is
// case-insensitive. Directories are only reported when includeDirectories is set.
int32_t OsFileEnumerate(const char* dir, const char* pattern, OSFILEENUMCALLBACK callback, void* param, int32_t includeDirectories);

int32_t OsFileMatch(const char* name, const char* pattern);

void OsBuildFontFilePath(const char*, char*, size_t);

char* OsPathFindExtensionWithDot(char*);

// The open flags for a Win32-style request: access (GENERIC_READ 0x80000000, GENERIC_WRITE
// 0x40000000), share mode (1 read, 2 write) and creation disposition (1..5).
uint32_t OsFileTranslateOpenFlags(uint32_t access, uint8_t shareMode, uint32_t disposition);

#endif
