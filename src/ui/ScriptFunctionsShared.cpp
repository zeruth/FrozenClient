#include "ui/ScriptFunctionsShared.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>

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
