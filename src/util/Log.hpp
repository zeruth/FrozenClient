#ifndef UTIL_LOG_HPP
#define UTIL_LOG_HPP

#include <cstdint>
#include <storm/Log.hpp>

enum SYSMSG_TYPE {
    SYSMSG_INFO         = 0x0,
    SYSMSG_WARNING      = 0x1,
    SYSMSG_ERROR        = 0x2,
    SYSMSG_FATAL        = 0x3,
    SYSMSG_NUMTYPES     = 0x4
};

void SysMsgPrintf(SYSMSG_TYPE, const char*, ...);

// DEBUG SCAFFOLDING. Writes to a file beside the client rather than to stderr, because stderr is
// lost unless the run was launched with a redirect -- and a diagnostic nobody can read is no
// diagnostic. Comes out with whatever it was added to chase.
void ProbeLog(const char* format, ...);

#endif
