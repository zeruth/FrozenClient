#include "util/Log.hpp"
#include <cstdarg>
#include <cstdio>
#include <storm/String.hpp>

// Windows uses Storm's SLog (lib/squall/storm/Log.cpp). Other platforms -- the Android build --
// keep this plain-file stand-in behind the same handle API.
#if !defined(WHOA_SYSTEM_WIN)

static FILE* s_logFiles[64];

void SLogInitialize() {
}

int32_t SLogCreate(const char* filename, uint32_t flags, HSLOG* log) {
    if (!filename || !*filename || !log) {
        return 0;
    }

    *log = 0;

    char path[STORM_MAX_PATH];
    SStrCopy(path, filename, sizeof(path));

    for (char* c = path; *c; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }

    for (uint32_t i = 1; i < 64; i++) {
        if (!s_logFiles[i]) {
            FILE* file = fopen(path, "w");

            if (!file) {
                return 0;
            }

            s_logFiles[i] = file;
            *log = i;
            return 1;
        }
    }

    return 0;
}

void SLogClose(HSLOG log) {
    if (log && log < 64 && s_logFiles[log]) {
        fclose(s_logFiles[log]);
        s_logFiles[log] = nullptr;
    }
}

void SLogFlush(HSLOG log) {
    if (log && log < 64 && s_logFiles[log]) {
        fflush(s_logFiles[log]);
    }
}

void SLogFlushAll() {
}

void SLogVWrite(HSLOG log, const char* format, va_list args) {
    if (!log || log >= 64 || !s_logFiles[log] || !format) {
        return;
    }

    vfprintf(s_logFiles[log], format, args);
    fputc('\n', s_logFiles[log]);
}

void SLogWrite(HSLOG log, const char* format, ...) {
    va_list args;
    va_start(args, format);
    SLogVWrite(log, format, args);
    va_end(args);
}

#endif

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

// DEBUG SCAFFOLDING; see the header. Writes beside the client so a run needs no redirect.
void ProbeLog(const char* format, ...) {
    static FILE* s_file = nullptr;
    static bool s_tried = false;

    if (!s_tried) {
        s_tried = true;

        s_file = fopen("Logs/probe.log", "w");
    }

    if (!s_file) {
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(s_file, format, args);
    va_end(args);

    // 10 rather than a character escape: this function was written through a shell heredoc and the
    // escape was eaten, which is the trap CLAUDE.md describes. A bare 10 cannot be mangled.
    fputc(10, s_file);
    fflush(s_file);
}
