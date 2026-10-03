#ifndef COMMON_TIME_HPP
#define COMMON_TIME_HPP

#include <cstdint>

// The timing methods OsTimeStartup chooses between: 0 asks for the best one available.
enum OS_TIMING_METHOD {
    OS_TIMING_BEST_AVAILABLE    = 0,
    OS_TIMING_GET_TICK_COUNT    = 1,
    OS_TIMING_QUERY_PERFORMANCE = 2,
};

void OsTimeStartup(int32_t method);

void OsTimeShutdown();

int32_t OsTimeGetTestError();

int32_t OsTimeGetTimingMethod();

const char* OsTimeGetTimingMethodName(int32_t method);

uint64_t OsGetAsyncTimeMs();

// C linkage: the Lua core (C) reads these for script profiling.
extern "C" uint64_t OsGetAsyncClocks();

extern "C" uint64_t OsGetAsyncClocksPerSecond();

uint64_t OsGetAsyncTimeMsPrecise();

void OsSleep(uint32_t duration);

#endif
