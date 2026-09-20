#include "storm/Error.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#if defined(WHOA_SYSTEM_WIN)
#include <Windows.h>
#endif

#if defined(WHOA_SYSTEM_ANDROID)
#include <android/log.h>
#endif

static uint32_t s_lasterror = ERROR_SUCCESS;
static uint32_t s_suppress;

// One line of a crash report.
//
// The report cannot go through printf alone. On Android stdout is a pipe drained by a separate
// thread, and the exit below does not wait for it, so an assertion message written on the way out
// is lost in the race -- which is how a client that died on entering the world reported nothing
// but an exit code. The system log takes its write synchronously, so the report survives.
static void STORMCDECL ErrPrintf(const char* format, ...) {
    char text[1024];

    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    fputs(text, stdout);

#if defined(WHOA_SYSTEM_ANDROID)
    __android_log_write(ANDROID_LOG_ERROR, "Frozen", text);
#endif
}

// Leaves the process without running static destructors.
//
// exit() runs them, and this client keeps ~20 caches and lists as namespace-scope Storm containers
// whose nodes live in Storm heaps that the same teardown is busy freeing -- so a dying client dies
// a second time on the way out and the second death is the one the crash reporter shows. That cost
// a session on Android, where an assertion's message was buried under an abort inside a destroyed
// mutex. _Exit skips destructors and atexit handlers, so the report has to flush stdio itself.
[[noreturn]] static void ErrExitNow(uint32_t exitcode) {
    fflush(nullptr);

    _Exit(static_cast<int32_t>(exitcode));
}

[[noreturn]] void STORMCDECL SErrDisplayAppFatal(const char* format, ...) {
    char text[1024];

    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    ErrPrintf("%s\n", text);

    ErrExitNow(EXIT_FAILURE);
}

int32_t STORMAPI SErrDisplayError(uint32_t errorcode, const char* filename, int32_t linenumber, const char* description, int32_t recoverable, uint32_t exitcode, uint32_t a7) {
    // TODO

    ErrPrintf("\n=========================================================\n");

    if (linenumber == SERR_LINECODE_EXCEPTION) {
        ErrPrintf("Exception Raised!\n\n");

        ErrPrintf(" App:         %s\n", "GenericBlizzardApp");

        if (errorcode != 0x85100000) {
            ErrPrintf(" Error Code:  0x%08X\n", errorcode);
        }

        // TODO output time

        ErrPrintf(" Error:       %s\n\n", description);
    } else {
        ErrPrintf("Assertion Failed!\n\n");

        ErrPrintf(" App:         %s\n", "GenericBlizzardApp");
        ErrPrintf(" File:        %s\n", filename);
        ErrPrintf(" Line:        %d\n", linenumber);

        if (errorcode != 0x85100000) {
            ErrPrintf(" Error Code:  0x%08X\n", errorcode);
        }

        // TODO output time

        ErrPrintf(" Assertion:   %s\n", description);
    }

    if (recoverable) {
        return 1;
    } else {
        ErrExitNow(exitcode);
    }
}

int32_t STORMCDECL SErrDisplayErrorFmt(uint32_t errorcode, const char* filename, int32_t linenumber, int32_t recoverable, uint32_t exitcode, const char* format, ...) {
    char buffer[2048];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    buffer[sizeof(buffer) - 1] = '\0';
    va_end(args);

    return SErrDisplayError(errorcode, filename, linenumber, buffer, recoverable, exitcode, 1);
}

void STORMAPI SErrPrepareAppFatal(const char* filename, int32_t linenumber) {
    // TODO
}

void STORMAPI SErrSetLastError(uint32_t errorcode) {
    s_lasterror = errorcode;
#if defined(WHOA_SYSTEM_WIN)
    SetLastError(errorcode);
#endif
}

uint32_t STORMAPI SErrGetLastError() {
    return s_lasterror;
}

void STORMAPI SErrSuppressErrors(uint32_t suppress) {
    s_suppress = suppress;
}
