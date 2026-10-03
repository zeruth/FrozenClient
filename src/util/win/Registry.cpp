#include "util/Registry.hpp"
#include <storm/Error.hpp>
#include <storm/String.hpp>
#include <windows.h>
#include <cstdlib>

static void RegistryKeyPath(char* path, const char* section, uint32_t flags) {
    path[0] = '\0';

    if (!(flags & 0x10)) {
        SStrCopy(path, (flags & 0x2) ? "Software\\Battle.net\\" : "Software\\Blizzard Entertainment\\", MAX_PATH);
    }

    SStrPack(path, section, MAX_PATH);
}

// ref: FUN_007703a0
static LSTATUS RegistryQueryKey(HKEY root, const char* path, const char* key, DWORD* type, void* data, DWORD size) {
    HKEY handle;

    auto status = RegOpenKeyExA(root, path, 0, KEY_READ, &handle);

    if (status == ERROR_SUCCESS) {
        status = RegQueryValueExA(handle, key, nullptr, type, static_cast<LPBYTE>(data), &size);
        RegCloseKey(handle);
    }

    return status;
}

// ref: FUN_00770490
// The value under the current user, then under the machine.
static int32_t RegistryQuery(const char* section, const char* key, uint32_t flags, DWORD* type, void* data, DWORD size) {
    char path[MAX_PATH];
    RegistryKeyPath(path, section, flags);

    *type = REG_DWORD;

    LSTATUS status = ERROR_FILE_NOT_FOUND;

    if ((flags & 0x4) || (status = RegistryQueryKey(HKEY_CURRENT_USER, path, key, type, data, size)) != ERROR_SUCCESS) {
        if (!(flags & 0x1)) {
            status = RegistryQueryKey(HKEY_LOCAL_MACHINE, path, key, type, data, size);
        }

        if (status != ERROR_SUCCESS) {
            SetLastError(status);
            return 0;
        }
    }

    return 1;
}

// ref: FUN_00770580
static int32_t RegistryStore(const char* section, const char* key, uint32_t flags, DWORD type, const void* data, DWORD size) {
    char path[MAX_PATH];
    RegistryKeyPath(path, section, flags);

    HKEY handle;
    DWORD disposition;

    auto status = RegCreateKeyExA((flags & 0x4) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_WRITE, nullptr, &handle, &disposition);

    if (status != ERROR_SUCCESS) {
        SetLastError(status);
        return 0;
    }

    status = RegSetValueExA(handle, key, 0, type, static_cast<const BYTE*>(data), size);

    if (status == ERROR_SUCCESS && (flags & 0x8)) {
        status = RegFlushKey(handle);
    }

    if (status == ERROR_SUCCESS) {
        status = RegCloseKey(handle);

        if (status == ERROR_SUCCESS) {
            return 1;
        }
    } else {
        RegCloseKey(handle);
    }

    SetLastError(status);
    return 0;
}

// ref: FUN_00770840
// A number stored either as a DWORD or as its text.
int32_t RegistryReadInt(const char* section, const char* key, uint32_t flags, uint32_t* value) {
    if (!section || !*section || !key || !*key || !value) {
        SErrSetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    char data[256];
    data[0] = '\0';

    DWORD type;

    if (!RegistryQuery(section, key, flags, &type, data, sizeof(data))) {
        return 0;
    }

    if (type == REG_SZ) {
        *value = strtoul(data, nullptr, 0);
    } else if (type == REG_DWORD) {
        *value = *reinterpret_cast<uint32_t*>(data);
    }

    return 1;
}

// ref: FUN_007709a0
int32_t RegistryWriteInt(const char* section, const char* key, uint32_t flags, uint32_t value) {
    if (!section || !*section || !key || !*key) {
        SErrSetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    return RegistryStore(section, key, flags, REG_DWORD, &value, sizeof(value));
}
