#include "ui/game/CGCooldownScript.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"
#include "ui/game/CGCooldown.hpp"
#include "ui/Util.hpp"
#include <cmath>

namespace {

// ref: FUN_005ece80
int32_t CGCooldown_SetCooldown(lua_State* L) {
    auto type = CGCooldown::GetObjectType();
    auto cooldown = static_cast<CGCooldown*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        luaL_error(L, "Usage: %s:SetCooldown(start, duration)", cooldown->GetDisplayName());
        return 0;
    }

    // Seconds to milliseconds, rounded to nearest as the reference's fistp does, then cut to the
    // 32 bits the frame keeps.
    auto start = static_cast<uint32_t>(static_cast<int64_t>(std::nearbyint(lua_tonumber(L, 2) * 1000.0)));
    auto duration = static_cast<uint32_t>(static_cast<int64_t>(std::nearbyint(lua_tonumber(L, 3) * 1000.0)));
    cooldown->SetCooldown(start, duration);

    return 0;
}

// ref: FUN_005ec9a0
int32_t CGCooldown_SetReverse(lua_State* L) {
    auto type = CGCooldown::GetObjectType();
    auto cooldown = static_cast<CGCooldown*>(FrameScript_GetObjectThis(L, type));

    // Absent argument means true, not false: the reference defaults this one on.
    cooldown->m_reverse = StringToBOOL(L, 2, 1);

    return 0;
}

// ref: FUN_005ec9f0
int32_t CGCooldown_GetReverse(lua_State* L) {
    auto type = CGCooldown::GetObjectType();
    auto cooldown = static_cast<CGCooldown*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, cooldown->m_reverse);

    return 1;
}

// ref: FUN_005eca30
int32_t CGCooldown_SetDrawEdge(lua_State* L) {
    auto type = CGCooldown::GetObjectType();
    auto cooldown = static_cast<CGCooldown*>(FrameScript_GetObjectThis(L, type));

    // Absent argument means true here too.
    cooldown->m_drawEdge = StringToBOOL(L, 2, 1);

    return 0;
}

// ref: FUN_005eca80
int32_t CGCooldown_GetDrawEdge(lua_State* L) {
    auto type = CGCooldown::GetObjectType();
    auto cooldown = static_cast<CGCooldown*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, cooldown->m_drawEdge);

    return 1;
}

}

FrameScript_Method CGCooldownMethods[] = {
    { "SetCooldown",    &CGCooldown_SetCooldown },
    { "SetReverse",     &CGCooldown_SetReverse },
    { "GetReverse",     &CGCooldown_GetReverse },
    { "SetDrawEdge",    &CGCooldown_SetDrawEdge },
    { "GetDrawEdge",    &CGCooldown_GetDrawEdge },
};
