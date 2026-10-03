#include "ui/ScriptFunctionsShared.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>
#include "console/Device.hpp"

int32_t Script_GetAccountExpansionLevel(lua_State* L) {
    // The highest expansion the account may play. Nothing raises the cap above the build, so for a
    // 3.3.5a client it is Wrath.
    lua_pushnumber(L, 2.0);

    return 1;
}

int32_t Script_IsLinuxClient(lua_State* L) {
#if defined(WHOA_SYSTEM_LINUX)
    lua_pushnumber(L, 1.0);
#else
    lua_pushnil(L);
#endif

    return 1;
}

int32_t Script_IsMacClient(lua_State* L) {
#if defined(WHOA_SYSTEM_MAC)
    lua_pushnumber(L, 1.0);
#else
    lua_pushnil(L);
#endif

    return 1;
}

int32_t Script_IsWindowsClient(lua_State* L) {
#if defined(WHOA_SYSTEM_WIN)
    lua_pushnumber(L, 1.0);
#else
    lua_pushnil(L);
#endif

    return 1;
}

// ref: FUN_00510dd0
int32_t Script_RestoreVideoEffectsDefaults(lua_State* L) {
    ConsoleDeviceSetDefaults(1);

    return 0;
}

// ref: FUN_00510dc0
int32_t Script_RestoreVideoResolutionDefaults(lua_State* L) {
    ConsoleDeviceSetDefaults(0);

    return 0;
}

// ref: FUN_004dd420
int32_t Script_RestoreVideoStereoDefaults(lua_State* L) {
    ConsoleDeviceSetDefaults(2);

    return 0;
}
