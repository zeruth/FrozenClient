#ifndef UTIL_S_FILE_HPP
#define UTIL_S_FILE_HPP

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <storm/String.hpp>

struct SOVERLAPPED;
struct TASYNCPARAMBLOCK;

// An open MPQ archive.
//
// Archives are kept in a global list ordered by priority. When a file is opened without naming
// an archive, the list is searched from the highest priority archive to the lowest, so an archive
// opened with a larger priority shadows any lower priority archive containing the same file.
class SArchive {
    public:
        // Member variables
        char m_path[STORM_MAX_PATH];
        void* m_handle;
        uint32_t m_priority;
        uint32_t m_flags;
        SArchive* m_next;
};

class SFile {
    public:
        // Static functions
        static int32_t Close(SFile* file);
        static int32_t CloseArchive(SArchive* archive);
        static int32_t FileExists(const char* filename);
        static SArchive* FirstArchive();
        static int32_t GetBasePath(char* buffer, size_t buffersize);
        static size_t GetFileSize(SFile* file, size_t* filesizeHigh);
        static int32_t IsStreamingMode();
        static int32_t IsStreamingTrial();
        static int32_t Load(SArchive* archive, const char* filename, void** buffer, size_t* bytes, size_t extraBytes, uint32_t flags, SOVERLAPPED* overlapped);
        static int32_t Open(const char* filename, SFile** file);
        static int32_t OpenArchive(const char* path, uint32_t priority, uint32_t flags, SArchive** archive);
        static int32_t OpenEx(SArchive* archive, const char* filename, uint32_t flags, SFile** file);
        static int32_t Read(SFile* file, void* buffer, size_t bytestoread, size_t* bytesread, SOVERLAPPED* overlapped, TASYNCPARAMBLOCK* asyncparam);
        static int32_t SetBasePath(const char* path);
        static int32_t SetLocalePath(const char* path);
        static int32_t Unload(void* ptr);

        // Member variables
        const char* m_filename;
        SArchive* m_archive;  // Archive the file was opened from, or nullptr for a local file
        void* m_handle;       // Archive file handle, valid when m_archive is set
        FILE* m_localFile;    // Local file stream, valid when m_archive is nullptr
        size_t m_size;
};

#endif
