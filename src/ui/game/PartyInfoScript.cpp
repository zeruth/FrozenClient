#include "ui/game/PartyInfoScript.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"

namespace {

int32_t Script_GetNumPartyMembers(lua_State* L) {
    lua_pushnumber(L, CGPartyInfo::NumMembers());

    return 1;
}

// TODO FUN_0052c190 reads a standalone counter, not the four-guid array that
// GetNumPartyMembers counts, so CGPartyInfo::NumMembers() is NOT the answer here. Whatever
// maintains that counter has to be found first.
int32_t Script_GetRealNumPartyMembers(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetPartyMember(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_GetPartyLeaderIndex(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_IsPartyLeader(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_IsRealPartyLeader(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_LeaveParty(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetLootMethod(lua_State* L) {
    // TODO group loot state; a lone player is on group loot
    lua_pushstring(L, "group");
    lua_pushnil(L);
    lua_pushnil(L);

    return 3;
}

int32_t Script_SetLootMethod(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetLootThreshold(lua_State* L) {
    // Uncommon
    lua_pushnumber(L, 2.0);

    return 1;
}

int32_t Script_SetLootThreshold(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetPartyAssignment(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ClearPartyAssignment(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetPartyAssignment(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SilenceMember(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnSilenceMember(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetOptOutOfLoot(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetOptOutOfLoot(lua_State* L) {
    // No loot system, so the player cannot have opted out of it.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_CanChangePlayerDifficulty(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_ChangePlayerDifficulty(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsPartyLFG(lua_State* L) {
    // There is no LFG system, so no group can have been formed by it.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_HasLFGRestrictions(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GetNumPartyMembers",         &Script_GetNumPartyMembers },
    { "GetRealNumPartyMembers",     &Script_GetRealNumPartyMembers },
    { "GetPartyMember",             &Script_GetPartyMember },
    { "GetPartyLeaderIndex",        &Script_GetPartyLeaderIndex },
    { "IsPartyLeader",              &Script_IsPartyLeader },
    { "IsRealPartyLeader",          &Script_IsRealPartyLeader },
    { "LeaveParty",                 &Script_LeaveParty },
    { "GetLootMethod",              &Script_GetLootMethod },
    { "SetLootMethod",              &Script_SetLootMethod },
    { "GetLootThreshold",           &Script_GetLootThreshold },
    { "SetLootThreshold",           &Script_SetLootThreshold },
    { "SetPartyAssignment",         &Script_SetPartyAssignment },
    { "ClearPartyAssignment",       &Script_ClearPartyAssignment },
    { "GetPartyAssignment",         &Script_GetPartyAssignment },
    { "SilenceMember",              &Script_SilenceMember },
    { "UnSilenceMember",            &Script_UnSilenceMember },
    { "SetOptOutOfLoot",            &Script_SetOptOutOfLoot },
    { "GetOptOutOfLoot",            &Script_GetOptOutOfLoot },
    { "CanChangePlayerDifficulty",  &Script_CanChangePlayerDifficulty },
    { "ChangePlayerDifficulty",     &Script_ChangePlayerDifficulty },
    { "IsPartyLFG",                 &Script_IsPartyLFG },
    { "HasLFGRestrictions",         &Script_HasLFGRestrictions },
};

void PartyInfoRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
