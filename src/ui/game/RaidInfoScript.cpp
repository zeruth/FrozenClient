#include "ui/game/RaidInfoScript.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGRaidInfo.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"

namespace {

int32_t Script_GetNumRaidMembers(lua_State* L) {
    lua_pushnumber(L, CGRaidInfo::NumMembers());

    return 1;
}

// ref: FUN_00572b80
// The earlier note here was right about the shape and can now say what it is for: the dword after
// the one GetNumRaidMembers reads is the REAL raid count, and it is separately maintained because
// a battleground group list updates the effective count and deliberately not this one. So in a
// battleground the two disagree, which is the whole reason both exist.
int32_t Script_GetRealNumRaidMembers(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(CGRaidInfo::GetRealNumMembers()));

    return 1;
}

int32_t Script_GetRaidRosterInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetRaidRosterSelection(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetRaidRosterSelection(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsRaidLeader(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsRealRaidLeader(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsRaidOfficer(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_SetRaidSubgroup(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SwapRaidSubgroup(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ConvertToRaid(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_PromoteToLeader(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_PromoteToAssistant(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DemoteAssistant(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetRaidTarget(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetRaidTargetIndex(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DoReadyCheck(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ConfirmReadyCheck(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetReadyCheckTimeLeft(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetReadyCheckStatus(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GetNumRaidMembers",          &Script_GetNumRaidMembers },
    { "GetRealNumRaidMembers",      &Script_GetRealNumRaidMembers },
    { "GetRaidRosterInfo",          &Script_GetRaidRosterInfo },
    { "SetRaidRosterSelection",     &Script_SetRaidRosterSelection },
    { "GetRaidRosterSelection",     &Script_GetRaidRosterSelection },
    { "IsRaidLeader",               &Script_IsRaidLeader },
    { "IsRealRaidLeader",           &Script_IsRealRaidLeader },
    { "IsRaidOfficer",              &Script_IsRaidOfficer },
    { "SetRaidSubgroup",            &Script_SetRaidSubgroup },
    { "SwapRaidSubgroup",           &Script_SwapRaidSubgroup },
    { "ConvertToRaid",              &Script_ConvertToRaid },
    { "PromoteToLeader",            &Script_PromoteToLeader },
    { "PromoteToAssistant",         &Script_PromoteToAssistant },
    { "DemoteAssistant",            &Script_DemoteAssistant },
    { "SetRaidTarget",              &Script_SetRaidTarget },
    { "GetRaidTargetIndex",         &Script_GetRaidTargetIndex },
    { "DoReadyCheck",               &Script_DoReadyCheck },
    { "ConfirmReadyCheck",          &Script_ConfirmReadyCheck },
    { "GetReadyCheckTimeLeft",      &Script_GetReadyCheckTimeLeft },
    { "GetReadyCheckStatus",        &Script_GetReadyCheckStatus },
};

void RaidInfoRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
