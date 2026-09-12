#include "util/Log.hpp"
#include <cstdarg>
#include <cstdio>
#include <storm/String.hpp>

// TODO the original routes these through Storm's SLog, which prefixes each line with a timestamp
// and keeps the file open on a shared handle table

int32_t SLogCreate(const char* filename, uint32_t flags, HSLOG* log) {
    if (!filename || !*filename || !log) {
        return 0;
    }

    *log = nullptr;

    char path[STORM_MAX_PATH];
    SStrCopy(path, filename, sizeof(path));

    for (char* c = path; *c; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }

    FILE* file = fopen(path, "w");

    if (!file) {
        return 0;
    }

    *log = file;

    return 1;
}

void SLogClose(HSLOG log) {
    if (log) {
        fclose(static_cast<FILE*>(log));
    }
}

void SLogFlush(HSLOG log) {
    if (log) {
        fflush(static_cast<FILE*>(log));
    }
}

void SLogWrite(HSLOG log, const char* format, ...) {
    if (!log || !format) {
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(static_cast<FILE*>(log), format, args);
    va_end(args);

    fputc('\n', static_cast<FILE*>(log));
}

void SysMsgPrintf(SYSMSG_TYPE severity, const char* format, ...) {
    // TODO
}
