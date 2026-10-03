#ifndef UTIL_REGISTRY_HPP
#define UTIL_REGISTRY_HPP

#include <cstdint>

// Values kept under "Software\Blizzard Entertainment\<section>" in the current user's registry,
// falling back to the machine's for reads. Flags: 0x1 skips the machine fallback, 0x2 keeps them
// under "Software\Battle.net\" instead, 0x4 uses the machine's hive only, 0x8 flushes a write,
// 0x10 takes the section as the full key path.

int32_t RegistryReadInt(const char* section, const char* key, uint32_t flags, uint32_t* value);

int32_t RegistryWriteInt(const char* section, const char* key, uint32_t flags, uint32_t value);

#endif
