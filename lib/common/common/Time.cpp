#include "common/Time.hpp"

#if defined(WHOA_SYSTEM_WIN)
#include <storm/Memory.hpp>
#include <cstdlib>
#include <new>
#include <windows.h>

// In src/util/win/OsSystem.cpp; common has no header of its own for it.
uint32_t OsGetProcessorCount();
#elif defined(WHOA_SYSTEM_MAC)
#include <mach/mach_time.h>
#include <unistd.h>
#elif defined(WHOA_SYSTEM_LINUX)
#include <chrono>
#include <unistd.h>
#endif

#if defined(WHOA_SYSTEM_WIN)

// The reference's W32 TimeManager (0x20 bytes): milliseconds per tick, the method in use, why the
// better method was rejected (0 when it was not), the performance counter's frequency, and an
// offset added to every reading.
class OsTimeManager {
    public:
        // Member variables
        double m_scale;
        int32_t m_timingMethod;
        int32_t m_timingTestError;
        int64_t m_frequency;
        double m_offset;

        // Member functions
        OsTimeManager(int32_t method);
        int32_t Calibrate();
        uint64_t GetTimeMs();
};

static OsTimeManager* s_timeManager; // ref: DAT_00d4159c

// ref: FUN_0086ab30
// Decides between the two counters. The performance counter is rejected (and the reason kept in
// m_timingTestError) when it is missing (1), runs at zero (2), drifts from the tick count by more
// than 4ms over a quarter of a second (3), or goes backwards as the thread is moved from core to
// core (4). The test runs at raised priority, which is put back afterwards.
int32_t OsTimeManager::Calibrate() {
    if (!QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&this->m_frequency))) {
        this->m_timingTestError = 1;
        return OS_TIMING_GET_TICK_COUNT;
    }

    if (this->m_frequency == 0) {
        this->m_timingTestError = 2;
        return OS_TIMING_GET_TICK_COUNT;
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    DWORD priorityClass = GetPriorityClass(process);
    int threadPriority = GetThreadPriority(thread);
    SetPriorityClass(process, HIGH_PRIORITY_CLASS);
    SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    OsSleep(0);

    this->m_timingTestError = 0;

    DWORD start = GetTickCount();
    DWORD tick0;

    do {
        tick0 = GetTickCount();
    } while (start == tick0);

    LARGE_INTEGER counter0;
    QueryPerformanceCounter(&counter0);

    uint32_t processors = OsGetProcessorCount();

    if (processors > 1) {
        LONGLONG last = counter0.QuadPart;

        DWORD_PTR processMask;
        DWORD_PTR systemMask;
        GetProcessAffinityMask(process, &processMask, &systemMask);

        for (uint32_t i = 0; i < 0x200; i++) {
            SetThreadAffinityMask(thread, static_cast<DWORD_PTR>(1) << ((i % processors) & 0x1F));
            OsSleep(0);

            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);

            if (counter.QuadPart <= last) {
                this->m_timingTestError = 4;
                break;
            }

            last = counter.QuadPart;
        }

        SetThreadAffinityMask(thread, processMask);
        OsSleep(0);
    }

    if (this->m_timingTestError == 0) {
        DWORD now;

        do {
            now = GetTickCount();
        } while (now - tick0 < 250);

        DWORD tick1;

        do {
            tick1 = GetTickCount();
        } while (now == tick1);

        LARGE_INTEGER counter1;
        QueryPerformanceCounter(&counter1);

        // Truncated, as the reference's fistp under a chop control word is.
        auto elapsed = static_cast<uint32_t>(static_cast<int64_t>(
            static_cast<double>(counter1.QuadPart - counter0.QuadPart) / static_cast<double>(this->m_frequency) * 1000.0));

        auto drift = static_cast<int32_t>(tick1 - elapsed - tick0);

        if (abs(drift) > 4) {
            this->m_timingTestError = 3;
        }
    }

    SetPriorityClass(process, priorityClass);
    SetThreadPriority(thread, threadPriority);
    OsSleep(0);

    return this->m_timingTestError ? OS_TIMING_GET_TICK_COUNT : OS_TIMING_QUERY_PERFORMANCE;
}

// ref: FUN_0086aea0
// A method the user asked for wins over the measured one. Asking for the performance counter
// after the test chose the tick count is recorded as error 5.
OsTimeManager::OsTimeManager(int32_t method) {
    this->m_timingMethod = -1;

    int32_t best = this->Calibrate();
    int32_t chosen = best;

    if (method != OS_TIMING_BEST_AVAILABLE && best != method) {
        chosen = method;

        if (method == OS_TIMING_QUERY_PERFORMANCE && best == OS_TIMING_GET_TICK_COUNT) {
            this->m_timingTestError = 5;
        }
    }

    this->m_timingMethod = chosen;

    int64_t ticksPerSecond = chosen == OS_TIMING_QUERY_PERFORMANCE ? this->m_frequency : 1000;
    this->m_scale = 1000.0 / static_cast<double>(ticksPerSecond);
    this->m_offset = 0.0;
}

// ref: FUN_0086adc0
uint64_t OsTimeManager::GetTimeMs() {
    int64_t ticks;

    if (this->m_timingMethod == OS_TIMING_QUERY_PERFORMANCE) {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        ticks = counter.QuadPart;
    } else {
        ticks = GetTickCount();
    }

    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<double>(ticks) * this->m_scale + this->m_offset));
}

// ref: FUN_0086af20
void OsTimeStartup(int32_t method) {
    auto mem = SMemAlloc(sizeof(OsTimeManager), ".\\W32\\TimeManager.cpp", 0x10C, 0x8);
    s_timeManager = mem ? new (mem) OsTimeManager(method) : nullptr;
}

// ref: FUN_0086af60
void OsTimeShutdown() {
    if (s_timeManager) {
        SMemFree(s_timeManager, "delete", -1, 0x0);
        s_timeManager = nullptr;
    }
}

// ref: FUN_0086ad50
int32_t OsTimeGetTestError() {
    return s_timeManager->m_timingTestError;
}

// ref: FUN_0086ad60
int32_t OsTimeGetTimingMethod() {
    return s_timeManager->m_timingMethod;
}

// ref: FUN_0086ae20
uint64_t OsGetAsyncTimeMs() {
    return s_timeManager->GetTimeMs();
}

// ref: FUN_0086ae30
uint64_t OsGetAsyncClocks() {
    if (s_timeManager->m_timingMethod == OS_TIMING_QUERY_PERFORMANCE) {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    return GetTickCount();
}

// ref: FUN_0086ae70
uint64_t OsGetAsyncClocksPerSecond() {
    if (s_timeManager->m_timingMethod == OS_TIMING_QUERY_PERFORMANCE) {
        return s_timeManager->m_frequency;
    }

    return 1000;
}

#else

// Only the Windows client has a time manager; elsewhere the platform clock is the only method.
void OsTimeStartup(int32_t method) {
}

void OsTimeShutdown() {
}

int32_t OsTimeGetTestError() {
    return 0;
}

int32_t OsTimeGetTimingMethod() {
    return OS_TIMING_GET_TICK_COUNT;
}

uint64_t OsGetAsyncTimeMs() {
#if defined(WHOA_SYSTEM_MAC)
    static mach_timebase_info_data_t timebase;

    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }

    uint64_t ticks = mach_absolute_time();

    return ticks * (timebase.numer / timebase.denom) / 1000000;

#elif defined(WHOA_SYSTEM_LINUX)
    auto now = std::chrono::steady_clock::now();
    uint64_t ticks = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return ticks;
#endif
}

uint64_t OsGetAsyncClocks() {
    return OsGetAsyncTimeMs();
}

uint64_t OsGetAsyncClocksPerSecond() {
    return 1000;
}

#endif

// ref: FUN_0086ad70
const char* OsTimeGetTimingMethodName(int32_t method) {
    switch (method) {
        case OS_TIMING_BEST_AVAILABLE:
            return "[Best Available]";

        case OS_TIMING_GET_TICK_COUNT:
            return "GetTickCount";

        case OS_TIMING_QUERY_PERFORMANCE:
            return "QueryPerformanceCounter";

        case -1:
            return "[Not Set]";

        default:
            return "[Unknown]";
    }
}

uint64_t OsGetAsyncTimeMsPrecise() {
#if defined(WHOA_SYSTEM_WIN)
    // TODO QueryPerformanceCounter implementation
    return OsGetAsyncTimeMs();

#else
    return OsGetAsyncTimeMs();
#endif
}

void OsSleep(uint32_t duration) {
#if defined(WHOA_SYSTEM_WIN)
    Sleep(duration);
#endif

#if defined(WHOA_SYSTEM_MAC) || defined(WHOA_SYSTEM_LINUX)
    // The duration is in milliseconds, as for Sleep; usleep takes microseconds. Passing it
    // through unscaled made OsSleep(1) a 1us nap, and the async read thread's idle loop spun on
    // the queue lock instead of resting.
    usleep(duration * 1000);
#endif
}
