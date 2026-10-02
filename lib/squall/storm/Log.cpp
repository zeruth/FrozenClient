#include "storm/Log.hpp"
#include "storm/Error.hpp"
#include "storm/String.hpp"

#if defined(WHOA_SYSTEM_WIN)

#include <Windows.h>
#include <cstdint>
#include <cstring>

// Storm's log files (the reference's SLog): a buffered, timestamped text log per handle, opened
// lazily on the first write unless asked otherwise. Not ported, recorded: SLogDestroy (FUN_007758e0),
// which runs at process exit and reports unreleased handles through Storm's registry-gated leak
// report.

namespace {

// One open log, taken whole from VirtualAlloc (0x10124 bytes in the reference; sized by the
// struct here, where the handle and pointers are 64-bit).
struct LOGRECORD {
    uint32_t handle;
    LOGRECORD* next;
    char filename[MAX_PATH];
    HANDLE file;
    uint32_t unk110;
    uint32_t bufferPos;         // end of the buffered text
    uint32_t echoPos;           // start of the text not yet echoed to the debugger
    int32_t indent;
    int32_t timestamps;
    char buffer[0x10000];
};

// ref: DAT_00cb7344
LOGRECORD* s_logTable[4];
// ref: DAT_00cb7358
CRITICAL_SECTION s_logCritSect[4];
// ref: DAT_00cb73c0
char s_timestamp[64];
// ref: DAT_00cb73b8
uint32_t s_timestampLen;
// ref: DAT_00cb7400
DWORD s_timestampTick;
// ref: DAT_00cb7220
int32_t s_logInitialized;
// ref: DAT_00cb7224
uint32_t s_logNextHandle;
// ref: DAT_00cb7228
char s_logDirectory[MAX_PATH];
// ref: DAT_00cb732c
CRITICAL_SECTION s_logDirectoryCritSect;
// ref: DAT_00cae968
int32_t s_logDebugEcho;

// ref: FUN_007750d0
// The length of a path's root: 1 for "/", 2 for "C:" and 3 for "C:\", and for a UNC path
// everything through the backslash after the share name (the whole path when there is none).
// Anything else has no root.
uint32_t PathRootLength(const char* path) {
    if (*path == '/') {
        return 1;
    }

    auto length = static_cast<uint32_t>(SStrLen(path));

    if (length > 1) {
        if (path[1] == ':') {
            return (path[2] == '\\') + 2;
        }

        if (path[0] == '\\' && path[1] == '\\') {
            auto root = path + 2;

            for (int32_t i = 2; i != 0; i--) {
                if (root) {
                    root = SStrChr(root, '\\') + 1;
                }
            }

            if (root) {
                return static_cast<uint32_t>(root - path);
            }

            return length;
        }
    }

    return 0;
}

// ref: FUN_00775140
// Writes out whatever is buffered and empties the buffer.
void FlushLog(LOGRECORD* log) {
    if (log->bufferPos) {
        DWORD written;
        WriteFile(log->file, log->buffer, log->bufferPos, &written, nullptr);
        FlushFileBuffers(log->file);

        log->bufferPos = 0;
        log->echoPos = 0;
    }
}

// ref: FUN_00775190
// Finds the log with this handle, creating it when asked, and returns it with its bucket locked;
// `bucket` receives the bucket to unlock later. With no log (or a zero handle) nothing is locked
// and `bucket` is -1.
LOGRECORD* LockLog(uint32_t handle, uint32_t* bucket, int32_t create) {
    if (!handle) {
        *bucket = 0xFFFFFFFF;
        return nullptr;
    }

    auto index = handle & 3;
    auto critSect = &s_logCritSect[index];
    EnterCriticalSection(critSect);

    *bucket = index;

    auto link = &s_logTable[index];
    auto log = s_logTable[index];

    while (log) {
        if (log->handle == handle) {
            return log;
        }

        link = &log->next;
        log = *link;
    }

    if (create) {
        log = static_cast<LOGRECORD*>(VirtualAlloc(nullptr, sizeof(LOGRECORD), MEM_COMMIT, PAGE_READWRITE));
        *link = log;

        if (log) {
            log->handle = handle;
            log->next = nullptr;
            log->filename[0] = '\0';
            log->file = INVALID_HANDLE_VALUE;
            log->bufferPos = 0;
            log->echoPos = 0;

            return log;
        }

        LeaveCriticalSection(critSect);
        *bucket = 0xFFFFFFFF;

        return nullptr;
    }

    LeaveCriticalSection(critSect);
    *bucket = 0xFFFFFFFF;

    return nullptr;
}

// ref: FUN_00775250
// Pads the line with the log's indent, at most 128 spaces.
void IndentLog(LOGRECORD* log) {
    auto size = log->indent;

    if (size > 0) {
        if (size > 0x7f) {
            size = 0x80;
        }

        memset(&log->buffer[log->bufferPos], ' ', size);
        log->buffer[log->bufferPos + size] = '\0';
        log->bufferPos += size;
    }
}

// ref: FUN_007752a0
// Starts a line with the local time when the log keeps timestamps -- or, for a continuation line,
// with as many spaces. The text is rebuilt at most once per tick and shared by every log.
void TimestampLog(LOGRECORD* log, int32_t stamp) {
    if (!log->timestamps) {
        return;
    }

    auto tick = GetTickCount();

    if (tick != s_timestampTick) {
        s_timestampTick = tick;

        SYSTEMTIME time;
        GetLocalTime(&time);

        wsprintfA(
            s_timestamp,
            "%u/%u %02u:%02u:%02u.%03u  ",
            static_cast<uint32_t>(time.wMonth),
            static_cast<uint32_t>(time.wDay),
            static_cast<uint32_t>(time.wHour),
            static_cast<uint32_t>(time.wMinute),
            static_cast<uint32_t>(time.wSecond),
            static_cast<uint32_t>(time.wMilliseconds)
        );

        s_timestampLen = static_cast<uint32_t>(SStrLen(s_timestamp));
    }

    auto length = s_timestampLen;

    if (stamp) {
        memcpy(&log->buffer[log->bufferPos], s_timestamp, length + 1);
        log->bufferPos += length;

        return;
    }

    memset(&log->buffer[log->bufferPos], ' ', length);
    log->buffer[log->bufferPos + length] = '\0';
    log->bufferPos += length;
}

// ref: FUN_00775380
// Unlinks and frees a log, then unlocks the bucket LockLog left locked.
void FreeLogAndUnlock(uint32_t bucket, LOGRECORD* log) {
    auto link = &s_logTable[bucket];
    auto cur = s_logTable[bucket];

    if (cur) {
        while (cur != log) {
            link = &cur->next;
            cur = *link;

            if (!cur) {
                LeaveCriticalSection(&s_logCritSect[bucket]);
                return;
            }
        }

        *link = cur->next;
        VirtualFree(cur, 0, MEM_RELEASE);
    }

    LeaveCriticalSection(&s_logCritSect[bucket]);
}

// ref: FUN_007753e0
// A bare file name -- no drive, no directory -- goes in the log directory, or beside the exe when
// none is set; anything else is used as given.
const char* ResolveLogPath(const char* filename, char* buffer, uint32_t size) {
    if (!filename || !*filename || filename[1] == ':' || SStrChr(filename, '\\')) {
        return filename;
    }

    EnterCriticalSection(&s_logDirectoryCritSect);

    if (s_logDirectory[0]) {
        auto length = SStrCopy(buffer, s_logDirectory, size);
        SStrCopy(buffer + length, filename, size - length);
        LeaveCriticalSection(&s_logDirectoryCritSect);
        return buffer;
    }

    GetModuleFileNameA(GetModuleHandleA(nullptr), buffer, size);

    auto slash = SStrChrR(buffer, '\\');
    if (slash) {
        *slash = '\0';
    }

    SStrPack(buffer, "\\", size);
    SStrPack(buffer, filename, size);

    LeaveCriticalSection(&s_logDirectoryCritSect);

    return buffer;
}

// ref: FUN_00775630
// Creates every directory on the way to a file.
BOOL CreateLogPath(const char* filename) {
    if (!filename) {
        SErrSetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    char path[MAX_PATH];
    SStrCopy(path, filename, sizeof(path));

    auto backslash = SStrChrR(path, '\\');
    auto slash = SStrChrR(path, '/');
    auto last = backslash > slash ? backslash : slash;

    if (last) {
        auto root = PathRootLength(path);

        if (last < path + root) {
            path[root] = '\0';
        } else {
            last[1] = '\0';
        }
    }

    auto root = PathRootLength(path);

    for (auto c = SStrChr(path + root, '/'); c; c = SStrChr(c + 1, '/')) {
        *c = '\\';
    }

    for (auto c = SStrChr(path + root, '\\'); c; c = SStrChr(c + 1, '\\')) {
        *c = '\0';
        CreateDirectoryA(path, nullptr);
        *c = '\\';
    }

    return CreateDirectoryA(path, nullptr);
}

// ref: FUN_00775740
int32_t OpenLogFile(const char* filename, uint32_t flags, HANDLE* file) {
    if (!filename || !*filename) {
        *file = INVALID_HANDLE_VALUE;
        return 0;
    }

    char buffer[MAX_PATH];
    auto path = ResolveLogPath(filename, buffer, sizeof(buffer));
    auto append = (flags & SLOG_FLAG_APPEND) != 0;

    CreateLogPath(path);

    *file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (*file != INVALID_HANDLE_VALUE && append) {
        SetFilePointer(*file, 0, nullptr, FILE_END);
    }

    return *file != INVALID_HANDLE_VALUE;
}

}

// ref: FUN_007755f0
void SLogInitialize() {
    if (s_logInitialized) {
        return;
    }

    for (auto& critSect : s_logCritSect) {
        InitializeCriticalSection(&critSect);
    }

    InitializeCriticalSection(&s_logDirectoryCritSect);
    s_logInitialized = 1;
}

// ref: FUN_007757e0
int32_t SLogCreate(const char* filename, uint32_t flags, HSLOG* log) {
    if (!filename || !*filename || !log) {
        SErrSetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    *log = 0;

    if (flags & SLOG_FLAG_NO_FILE) {
        filename = "";
        flags &= ~SLOG_FLAG_OPEN_NOW;
    }

    HANDLE file = INVALID_HANDLE_VALUE;

    if ((flags & SLOG_FLAG_OPEN_NOW) && !OpenLogFile(filename, flags, &file)) {
        return 0;
    }

    *log = ++s_logNextHandle;

    uint32_t bucket;
    auto record = LockLog(*log, &bucket, 1);

    if (!record) {
        *log = 0;
        return 0;
    }

    record->file = file;
    SStrCopy(record->filename, filename, sizeof(record->filename));
    record->unk110 = flags;
    record->timestamps = 1;
    record->indent = 0;

    LeaveCriticalSection(&s_logCritSect[bucket]);

    return 1;
}

// ref: FUN_007754a0
void SLogClose(HSLOG log) {
    if (!s_logInitialized) {
        return;
    }

    uint32_t bucket;
    auto record = LockLog(log, &bucket, 0);

    if (!record) {
        return;
    }

    if (record->file != INVALID_HANDLE_VALUE) {
        FlushLog(record);
        CloseHandle(record->file);
    }

    FreeLogAndUnlock(bucket, record);
}

// ref: FUN_00775500
void SLogFlush(HSLOG log) {
    uint32_t bucket;
    auto record = LockLog(log, &bucket, 0);

    if (!record) {
        return;
    }

    if (record->file != INVALID_HANDLE_VALUE) {
        FlushLog(record);
    }

    LeaveCriticalSection(&s_logCritSect[bucket]);
}

// ref: FUN_00775550
void SLogFlushAll() {
    for (uint32_t bucket = 0; bucket < 4; bucket++) {
        EnterCriticalSection(&s_logCritSect[bucket]);

        for (auto record = s_logTable[bucket]; record; record = record->next) {
            if (record->file != INVALID_HANDLE_VALUE && record->bufferPos) {
                DWORD written;
                WriteFile(record->file, record->buffer, record->bufferPos, &written, nullptr);
                FlushFileBuffers(record->file);
                record->bufferPos = 0;
                record->echoPos = 0;
            }
        }

        LeaveCriticalSection(&s_logCritSect[bucket]);
    }
}

// ref: FUN_00775a90
// Opens the file on the first write if it is not open yet; a file that will not open clears the
// name so it is not tried again. Each write is one timestamped, indented line ending in CRLF; the
// buffer goes to disk once it passes 48 KB.
void SLogVWrite(HSLOG log, const char* format, va_list args) {
    uint32_t bucket;
    auto record = LockLog(log, &bucket, 0);

    if (!record) {
        return;
    }

    if (record->file == INVALID_HANDLE_VALUE && !OpenLogFile(record->filename, record->unk110, &record->file)) {
        record->filename[0] = '\0';
        LeaveCriticalSection(&s_logCritSect[bucket]);
        return;
    }

    TimestampLog(record, 1);
    IndentLog(record);

    SStrVPrintf(&record->buffer[record->bufferPos], sizeof(record->buffer) - record->bufferPos, format, args);
    record->bufferPos += SStrLen(&record->buffer[record->bufferPos]);

    memcpy(&record->buffer[record->bufferPos], "\r\n", 3);
    record->bufferPos += 2;

    if (s_logDebugEcho) {
        OutputDebugStringA(&record->buffer[record->echoPos]);
    }

    record->echoPos = record->bufferPos;

    if (record->bufferPos > 0xBFFF) {
        FlushLog(record);
    }

    LeaveCriticalSection(&s_logCritSect[bucket]);
}

// ref: FUN_00775bb0
void SLogWrite(HSLOG log, const char* format, ...) {
    va_list args;
    va_start(args, format);
    SLogVWrite(log, format, args);
    va_end(args);
}

#endif
