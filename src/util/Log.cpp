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

// Client diagnostics. Until now this was an empty TODO, so everything handed to it was thrown
// away silently -- including the two warnings about being unable to create the interface log
// files, which is exactly the kind of thing worth hearing about.
//
// Routed to stderr because that is the one channel that reaches every target: the console on
// desktop, and logcat on Android, where the entry point pumps stdout and stderr into the log.
// The severity is spelled out rather than printed as a number so a line is readable on its own.
void SysMsgPrintf(SYSMSG_TYPE severity, const char* format, ...) {
    static const char* const s_names[SYSMSG_NUMTYPES] = {
        "info",
        "warning",
        "error",
        "fatal",
    };

    const char* name = (severity >= 0 && severity < SYSMSG_NUMTYPES) ? s_names[severity] : "?";

    char text[1024];

    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    fprintf(stderr, "SysMsg %s: %s\n", name, text);
}
