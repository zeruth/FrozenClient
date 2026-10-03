#include "util/Registry.hpp"
#include "util/OsSystem.hpp"

#if !defined(WHOA_SYSTEM_WIN)

// No registry off Windows: nothing is stored, and nothing is found.

int32_t RegistryReadInt(const char* section, const char* key, uint32_t flags, uint32_t* value) {
    return 0;
}

int32_t RegistryWriteInt(const char* section, const char* key, uint32_t flags, uint32_t value) {
    return 0;
}


// The CPU queries are Windows-only too; a speed of zero puts the CPU in the lower class.
uint64_t OsGetCpuSpeed() {
    return 0;
}

bool OsIsRemoteSession() {
    return false;
}

#endif
