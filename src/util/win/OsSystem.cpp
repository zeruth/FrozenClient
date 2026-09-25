#include "util/OsSystem.hpp"
#include <cstring>
#include <intrin.h>
#include <windows.h>

static uint64_t s_cpuSpeed; // ref: DAT_00d415c8

// Returns 0 when CPUID is unavailable, 1 with the standard leaves, 2 with the extended ones too.
// ref: FUN_0086af90
int32_t OsGetCpuidInfo(OsCpuidInfo* info) {
    memset(info, 0, sizeof(OsCpuidInfo));

    int32_t level = 0;

    // CPUID exists when the ID flag of EFLAGS can be toggled
    auto flags = __readeflags();
    __writeeflags(flags ^ 0x200000);

    if (__readeflags() == flags) {
        return level;
    }

    int32_t regs[4];

    __cpuid(regs, 0);
    uint32_t maxLeaf = regs[0];

    if (maxLeaf == 0) {
        return level;
    }

    info->maxLeaf = maxLeaf;
    info->vendor[0] = regs[1];
    info->vendor[1] = regs[3];
    info->vendor[2] = regs[2];
    level = 1;

    if (maxLeaf > 3) {
        __cpuid(regs, 4);
        info->leaf4Eax = regs[0];
    }

    __cpuid(regs, 1);
    info->leaf1Edx = regs[3];
    info->leaf1Ebx = regs[1];

    __cpuid(regs, 0x80000000);
    uint32_t maxExtLeaf = regs[0];

    if (maxExtLeaf > 0x80000000) {
        level = 2;
        info->maxExtLeaf = maxExtLeaf;

        if (maxExtLeaf > 0x80000007) {
            __cpuid(regs, 0x80000008);
            info->ext8Ecx = regs[2];
        }

        if (maxExtLeaf > 0x80000001) {
            __cpuid(regs, 0x80000002);
            info->brand[0] = regs[0];
            info->brand[1] = regs[1];
            info->brand[2] = regs[2];
            info->brand[3] = regs[3];
        }

        if (maxExtLeaf > 0x80000002) {
            __cpuid(regs, 0x80000003);
            info->brand[4] = regs[0];
            info->brand[5] = regs[1];
            info->brand[6] = regs[2];
            info->brand[7] = regs[3];
        }

        if (maxExtLeaf > 0x80000003) {
            __cpuid(regs, 0x80000004);
            info->brand[8] = regs[0];
            info->brand[9] = regs[1];
            info->brand[10] = regs[2];
            info->brand[11] = regs[3];
        }

        __cpuid(regs, 0x80000001);
        info->ext1Edx = regs[3];
        info->ext1Ecx = regs[2];
    }

    return level;
}

// ref: FUN_0086b240
uint32_t OsGetProcessorCount() {
    SYSTEM_INFO info = {};
    GetSystemInfo(&info);

    if (info.dwNumberOfProcessors == 0) {
        info.dwNumberOfProcessors = 1;
    }

    return info.dwNumberOfProcessors;
}

// ref: FUN_0086b2d0
int32_t OsGetVersion() {
    OSVERSIONINFOEXA version;
    memset(&version, 0, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);

    if (!GetVersionExA(reinterpret_cast<OSVERSIONINFOA*>(&version))) {
        version.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);

        if (!GetVersionExA(reinterpret_cast<OSVERSIONINFOA*>(&version))) {
            return 0;
        }
    }

    if (version.dwPlatformId != VER_PLATFORM_WIN32_WINDOWS) {
        if (version.dwPlatformId != VER_PLATFORM_WIN32_NT) {
            return 0;
        }

        if (version.dwMajorVersion == 4) {
            return 6;
        }

        if (version.dwMajorVersion == 5) {
            if (version.dwMinorVersion == 0) {
                return 7;
            }

            if (version.dwMinorVersion == 1) {
                return 8;
            }

            if (version.dwMinorVersion == 2) {
                return 9;
            }
        } else if (version.dwMajorVersion == 6 && version.dwMinorVersion == 0) {
            return 15;
        }

        return 11;
    }

    if (version.dwMajorVersion == 4) {
        if (version.dwMinorVersion == 0) {
            if (version.szCSDVersion[1] != 'C' && version.szCSDVersion[1] != 'B') {
                return 1;
            }

            return 2;
        }

        if (version.dwMinorVersion == 10) {
            if (version.szCSDVersion[1] != 'A') {
                return 3;
            }

            return 4;
        }

        if (version.dwMinorVersion == 90) {
            return 5;
        }
    }

    return 10;
}

// ref: FUN_0086b480
int32_t OsGetComputerName(char* buffer, uint32_t* size) {
    return GetComputerNameA(buffer, reinterpret_cast<LPDWORD>(size));
}

// ref: FUN_0086b4a0
int32_t OsGetUserName(char* buffer, uint32_t* size) {
    return GetUserNameA(buffer, reinterpret_cast<LPDWORD>(size));
}

// ref: FUN_0086b4c0
uint64_t OsGetPhysicalMemory() {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&status);

    return status.ullTotalPhys;
}

// Reads the processor's maximum clock from powrprof into the CPU speed, in Hz.
// ref: FUN_0086b710
int32_t OsQueryCpuSpeedFromPowerInfo() {
    int32_t result = 0;

    auto module = LoadLibraryA("powrprof.dll");

    if (module) {
        using CallNtPowerInformationFn = LONG (WINAPI*)(int32_t, void*, ULONG, void*, ULONG);
        auto callNtPowerInformation = reinterpret_cast<CallNtPowerInformationFn>(GetProcAddress(module, "CallNtPowerInformation"));

        if (callNtPowerInformation) {
            // Four PROCESSOR_POWER_INFORMATION records; the second field of the first is MaxMhz
            uint32_t info[0x60 / sizeof(uint32_t)];

            if (callNtPowerInformation(11, nullptr, 0, info, 0x60) == 0) {
                s_cpuSpeed = static_cast<uint64_t>(info[1]) * 1000000;
                result = 1;
            }
        }

        FreeLibrary(module);
    }

    return result;
}
