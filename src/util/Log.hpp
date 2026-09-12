#ifndef UTIL_LOG_HPP
#define UTIL_LOG_HPP

#include <cstdint>

enum SYSMSG_TYPE {
    SYSMSG_INFO         = 0x0,
    SYSMSG_WARNING      = 0x1,
    SYSMSG_ERROR        = 0x2,
    SYSMSG_FATAL        = 0x3,
    SYSMSG_NUMTYPES     = 0x4
};

typedef void* HSLOG;

// Creates (or truncates) a log file. Returns 0 and leaves *log null on failure.
int32_t SLogCreate(const char* filename, uint32_t flags, HSLOG* log);

void SLogClose(HSLOG log);

void SLogFlush(HSLOG log);

void SLogWrite(HSLOG log, const char* format, ...);

void SysMsgPrintf(SYSMSG_TYPE, const char*, ...);

#endif
