#include "util/OsSystem.hpp"
#include "util/Filesystem.hpp"
#include <common/Time.hpp>
#include <storm/Log.hpp>
#include <cstdlib>
#include <cstring>
#include <intrin.h>
#include <windows.h>

static uint64_t s_cpuSpeed; // ref: DAT_00d415c8

// What OsGetCpuInfo found, worked out once (reference 0x00d415a0..0x00d415b4).
static int32_t s_cpuInfoQueried;
static uint32_t s_cpuVendor;
static uint32_t s_cpuFeatures;
static uint32_t s_cpuSockets;
static uint32_t s_cpuCores;
static uint32_t s_cpuProcessors;

// Whether OsGetCpuInfo writes Logs\\cpu.log (reference 0x00d415c0).
static int32_t s_cpuLogEnabled;

// OsQueryCpuSpeedFromPowerInfo's result, asked for once (reference 0x00d415d0, 0x00d415d4).
static int32_t s_powerInfoSpeed;
static int32_t s_powerInfoQueried;

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

// ref: FUN_0086b0c0
void OsEnableCpuLog() {
    s_cpuLogEnabled = 1;
}

// ref: FUN_0086b0d0
static void OsLogCpuInfo(const OsCpuidInfo* info) {
    if (!s_cpuLogEnabled) {
        return;
    }

    OsCreateDirectory("Logs", 0);

    HSLOG log;
    SLogCreate("Logs\\cpu.log", 0, &log);

    if (s_cpuVendor == 4) {
        SLogWrite(log, "UNABLE TO IDENTIFY CPU");
        SLogClose(log);
        return;
    }

    SLogWrite(log, "vendor: %d", s_cpuVendor);
    SLogWrite(log, "features: %08X", s_cpuFeatures & 0x7FFFFFFF);
    SLogWrite(log, "sockets: %d", s_cpuSockets);
    SLogWrite(log, "cores: %d", s_cpuCores);
    SLogWrite(log, "processors: %d", s_cpuProcessors);

    char vendor[13];
    strncpy(vendor, reinterpret_cast<const char*>(info->vendor), 12);
    vendor[12] = 0;
    SLogWrite(log, "vendor id string= %s", vendor);

    SLogWrite(log, "standard (%d): 1b=%08X 1d=%08x 4a=%08X", info->maxLeaf, info->leaf1Ebx, info->leaf1Edx, info->leaf4Eax);
    SLogWrite(log, "extended (%d): 1c=%08X 1d=%08x 8c=%08X", info->maxExtLeaf & 0x7FFFFFFF, info->ext1Ecx, info->ext1Edx, info->ext8Ecx);

    auto brand = reinterpret_cast<const char*>(info->brand);

    while (*brand && *brand == ' ') {
        brand++;
    }

    SLogWrite(log, "processor brand string= %s", brand);
    SLogClose(log);
}

// ref: FUN_0086b600
// Reads the clock from the end of the brand string ("... @ 2.67GHz"): the digits and points
// before a GHz, MHz or THz suffix are copied right-aligned into a field of spaces and scaled.
static int32_t OsParseCpuSpeedFromBrand(const char* brand) {
    if (!brand) {
        return 0;
    }

    const char* end = brand;

    while (*end) {
        end++;
    }

    if (!(brand < end - 1 && end[-1] == 'z' && brand < end - 2 && end[-2] == 'H' && brand < end - 3)) {
        return 0;
    }

    double multiplier;

    if (end[-3] == 'G') {
        multiplier = 1000000000.0;
    } else if (end[-3] == 'M') {
        multiplier = 1000000.0;
    } else if (end[-3] == 'T') {
        multiplier = 1000000000000.0;
    } else {
        return 0;
    }

    char number[48];
    memset(number, ' ', sizeof(number));
    number[47] = 0;

    char* dst = &number[46];

    for (const char* src = end - 4; brand < src && ((*src >= '0' && *src <= '9') || *src == '.'); src--) {
        *dst = *src;
        dst--;
    }

    double value = atof(number);

    if (value == 0.0) {
        return 0;
    }

    s_cpuSpeed = static_cast<uint64_t>(static_cast<int64_t>(value * multiplier));
    return 1;
}

// ref: FUN_0086b9a0
// Identifies the processor once and keeps the answer: vendor, feature word, cores per package
// and processor count, then the clock -- from the brand string, else the power information,
// else the timer's own rate.
uint32_t OsGetCpuInfo(uint32_t* vendor) {
    if (s_cpuInfoQueried) {
        *vendor = s_cpuVendor;
        return s_cpuFeatures;
    }

    s_cpuInfoQueried = 1;

    uint32_t features = 0;

    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);

    s_cpuProcessors = systemInfo.dwNumberOfProcessors;

    if (s_cpuProcessors == 0) {
        s_cpuProcessors = 1;
    }

    s_cpuCores = 1;
    s_cpuSockets = 1;

    OsCpuidInfo info;

    if (!OsGetCpuidInfo(&info)) {
        s_cpuVendor = 4;
    } else {
        features = (info.leaf1Edx & 0x10) ? 0x1 : 0x0;

        if (info.leaf1Edx & 0x800000) {
            features |= 0x2;
        }

        if (info.leaf1Edx & 0x2000000) {
            features |= 0x4;
        }

        if (info.ext1Edx & 0x80000000) {
            features |= 0x8;
        }

        if (info.leaf1Edx & 0x4000000) {
            features |= 0x10;
        }

        auto vendorId = reinterpret_cast<const char*>(info.vendor);

        if (strncmp(vendorId, "GenuineIntel", 12) == 0) {
            s_cpuVendor = 1;

            if (info.maxLeaf > 3 && (info.leaf4Eax >> 26) != 0) {
                features |= 0x40;
                s_cpuCores = (info.leaf4Eax >> 26) + 1;
            }
        } else if (strncmp(vendorId, "AuthenticAMD", 12) == 0) {
            s_cpuVendor = 2;

            if (static_cast<uint8_t>(info.ext8Ecx) != 0) {
                features |= 0x40;
                s_cpuCores = static_cast<uint8_t>(info.ext8Ecx) + 1;
            }
        }

        // The reference goes on to compare against CyrixInstead and CentaurHauls and does
        // nothing with either answer.
    }

    s_cpuFeatures = features | 0x80000000;

    OsLogCpuInfo(&info);

    if (!OsParseCpuSpeedFromBrand(reinterpret_cast<const char*>(info.brand))) {
        if (!s_powerInfoQueried) {
            s_powerInfoSpeed = OsQueryCpuSpeedFromPowerInfo();
            s_powerInfoQueried = 1;
        }

        if (!s_powerInfoSpeed) {
            s_cpuSpeed = OsGetAsyncClocksPerSecond();
        }
    }

    *vendor = s_cpuVendor;
    return s_cpuFeatures;
}

// ref: FUN_0086bb80
uint32_t OsGetCpuFeatures() {
    uint32_t vendor;
    return OsGetCpuInfo(&vendor);
}

// ref: FUN_0086bba0
// Clocks per second, worked out by OsGetCpuInfo the first time it is asked for.
uint64_t OsGetCpuSpeed() {
    if (!s_cpuSpeed) {
        uint32_t vendor;
        OsGetCpuInfo(&vendor);
    }

    return s_cpuSpeed;
}

// ref: FUN_0086b780
bool OsIsRemoteSession() {
    return GetSystemMetrics(SM_REMOTESESSION) != 0;
}
