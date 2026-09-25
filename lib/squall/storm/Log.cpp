#include "storm/String.hpp"

#if defined(WHOA_SYSTEM_WIN)

#include <Windows.h>
#include <cstdint>
#include <cstring>

// Storm's log files (the reference's SLog). Only the helpers under the public calls are here so
// far: finding a log by handle, its write buffer, the timestamp and indent prefixes, and freeing
// it. Nothing initializes the four bucket locks yet -- that is SLog's own startup, not ported.

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

}

#endif
