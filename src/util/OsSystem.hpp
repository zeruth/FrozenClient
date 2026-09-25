#ifndef UTIL_OS_SYSTEM_HPP
#define UTIL_OS_SYSTEM_HPP

#include <cstdint>

// Raw CPUID results, laid out as the reference fills them (0x5C bytes).
struct OsCpuidInfo {
    uint32_t vendor[3];                 // +0x00 leaf 0 EBX, EDX, ECX
    uint32_t maxLeaf;                   // +0x0C leaf 0 EAX
    uint32_t leaf1Ebx;                  // +0x10
    uint32_t leaf1Edx;                  // +0x14
    uint32_t leaf4Eax;                  // +0x18
    uint32_t maxExtLeaf;                // +0x1C leaf 0x80000000 EAX
    uint32_t ext1Ecx;                   // +0x20
    uint32_t ext1Edx;                   // +0x24
    uint32_t ext8Ecx;                   // +0x28
    uint32_t brand[12];                 // +0x2C leaves 0x80000002-0x80000004
};

static_assert(sizeof(OsCpuidInfo) == 0x5C, "OsCpuidInfo is 0x5C bytes");

int32_t OsGetCpuidInfo(OsCpuidInfo* info);

uint32_t OsGetProcessorCount();

int32_t OsGetVersion();

int32_t OsGetComputerName(char* buffer, uint32_t* size);

int32_t OsGetUserName(char* buffer, uint32_t* size);

uint64_t OsGetPhysicalMemory();

int32_t OsQueryCpuSpeedFromPowerInfo();

#endif
