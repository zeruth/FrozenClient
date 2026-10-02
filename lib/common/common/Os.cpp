#include "common/Os.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

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
