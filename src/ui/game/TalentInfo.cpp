#include "ui/game/TalentInfo.hpp"
#include "util/Lua.hpp"

// The player's active talent group. Written only by code that is not ported yet, so it stays 0,
// the first group.
static uint32_t s_activeTalentGroup;        // ref: DAT_00c20ff4

// ref: FUN_005c57a0
uint32_t TalentInfoDefaultGroup(int32_t pet) {
    return pet ? 0 : s_activeTalentGroup;
}

// ref: FUN_005c57d0
uint32_t TalentInfoGroupArg(lua_State* L, int32_t index) {
    if (lua_isnumber(L, index)) {
        return static_cast<uint32_t>(lua_tointeger(L, index) - 1);
    }

    return s_activeTalentGroup;
}
