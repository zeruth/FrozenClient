#include "common/Os.hpp"
#include <storm/Error.hpp>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__ANDROID__)
#include <dlfcn.h>
#include <link.h>
#endif

namespace {

// ref: 0x00d415b8, 0x00d415bc
uintptr_t s_textBegin;
uintptr_t s_textEnd;

#if defined(__linux__) || defined(__ANDROID__)
struct TextSearch {
    uintptr_t base;
    bool found;
};

int FindExecutableSegment(dl_phdr_info* info, size_t, void* data) {
    auto search = static_cast<TextSearch*>(data);

    if (info->dlpi_addr != search->base) {
        return 0;
    }

    for (int i = 0; i < info->dlpi_phnum; i++) {
        auto& phdr = info->dlpi_phdr[i];

        if (phdr.p_type == PT_LOAD && (phdr.p_flags & PF_X)) {
            s_textBegin = info->dlpi_addr + phdr.p_vaddr;
            s_textEnd = s_textBegin + phdr.p_memsz;
            search->found = true;
            return 1;
        }
    }

    return 0;
}
#endif

// ref: FUN_0086b510
// The bounds of the client's own code: the main module's ".text" section.
void OsFindTextSection() {
#if defined(_WIN32)
    auto module = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (!strncmp(reinterpret_cast<const char*>(section->Name), ".text", 8)) {
            s_textBegin = reinterpret_cast<uintptr_t>(module) + section->VirtualAddress;
            s_textEnd = s_textBegin + section->Misc.VirtualSize;
            return;
        }
    }

    SErrDisplayAppFatal("Unable to find .text section");
#elif defined(__linux__) || defined(__ANDROID__)
    // The client is a shared object here, so its code is the executable segment of whichever
    // module holds this function.
    Dl_info info;
    TextSearch search = {};

    if (dladdr(reinterpret_cast<const void*>(&OsValidateFunctionPointer), &info)) {
        search.base = reinterpret_cast<uintptr_t>(info.dli_fbase);
        dl_iterate_phdr(FindExecutableSegment, &search);
    }

    if (!search.found) {
        SErrDisplayAppFatal("Unable to find .text section");
    }
#else
    // No bounds lookup on this platform yet: accept every pointer. Recorded as a divergence.
    s_textBegin = 0;
    s_textEnd = UINTPTR_MAX;
#endif
}

}

// ref: FUN_008714e0
// The system's message for GetLastError, allocated by FormatMessage; hand it back to
// OsFreeLastErrorStr.
const char* OsGetLastErrorStr() {
#if defined(_WIN32)
    LPSTR buffer = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    return buffer;
#else
    return "";
#endif
}

// ref: FUN_00871510
void OsFreeLastErrorStr(const char* str) {
#if defined(_WIN32)
    LocalFree(const_cast<char*>(str));
#endif
}

// ref: FUN_0086b5a0
// A callback about to be called must point into the client's own code; anything else is fatal.
// The Lua core runs this before calling a C function, a hook or a protected body, and the
// console before a command or CVar handler.
extern "C" void OsValidateFunctionPointer(const void* function) {
    if (!s_textBegin || !s_textEnd) {
        OsFindTextSection();
    }

    auto address = reinterpret_cast<uintptr_t>(function);

    if (address < s_textBegin || address >= s_textEnd) {
        SErrDisplayAppFatal("Invalid function pointer: %p", function);
    }
}
