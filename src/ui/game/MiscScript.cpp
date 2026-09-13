// Script functions whose subsystems are not ported yet. Each answers the way a client with
// nothing going on would (no voice chat, no pets, no arena teams, ...), so the FrameXML code
// that calls them at load or on entering the world runs to completion instead of failing on a
// missing global. They move to their subsystem files as those get ported.

#include "ui/game/MiscScript.hpp"
#include "ui/FrameScript.hpp"
#include "ui/Types.hpp"
#include "util/Lua.hpp"
#include <storm/String.hpp>
#include <ctime>
#include <map>
#include <string>

namespace {

// Chat windows: the two default windows of a fresh character; nothing is persisted yet
int32_t Script_GetChatWindowInfo(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        return luaL_error(L, "Usage: GetChatWindowInfo(index)");
    }

    auto index = static_cast<int32_t>(lua_tonumber(L, 1));

    const char* name = "";
    int32_t shown = 0;
    int32_t docked = 0;

    if (index == 1) {
        name = "General";
        shown = 1;
        docked = 1;
    } else if (index == 2) {
        name = "Combat Log";
        shown = 1;
        docked = 2;
    }

    lua_pushstring(L, name);
    lua_pushnumber(L, 14.0);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 1.0);

    if (shown) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    lua_pushnil(L);

    if (docked) {
        lua_pushnumber(L, docked);
    } else {
        lua_pushnil(L);
    }

    lua_pushnil(L);

    return 10;
}

// Chat type indices are handed out in order of first request and stay stable for the session
int32_t Script_GetChatTypeIndex(lua_State* L) {
    static std::map<std::string, int32_t> s_indices;

    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "Usage: GetChatTypeIndex(\"type\")");
    }

    std::string type = lua_tostring(L, 1);
    auto found = s_indices.find(type);

    if (found == s_indices.end()) {
        found = s_indices.insert({ type, static_cast<int32_t>(s_indices.size()) + 1 }).first;
    }

    lua_pushnumber(L, found->second);

    return 1;
}

int32_t Script_ReturnNothing(lua_State* L) {
    return 0;
}

int32_t Script_ReturnZero(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_ReturnOne(lua_State* L) {
    lua_pushnumber(L, 1.0);

    return 1;
}

int32_t Script_ReturnNil(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_ReturnEmptyString(lua_State* L) {
    lua_pushstring(L, "");

    return 1;
}

int32_t Script_ReturnTwo(lua_State* L) {
    lua_pushnumber(L, 2.0);

    return 1;
}

int32_t Script_ReturnThreeZeros(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 3;
}

int32_t Script_ReturnTwoZeros(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 2;
}

// TODO the spell book; one empty general tab
int32_t Script_GetSpellTabInfo(lua_State* L) {
    lua_pushstring(L, "General");
    lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark");
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 4;
}

int32_t Script_GetRepairAllCost(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushboolean(L, 0);

    return 2;
}

int32_t Script_CalendarGetDate(lua_State* L) {
    time_t now = time(nullptr);
    tm local;

#if defined(WHOA_SYSTEM_WIN)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    lua_pushnumber(L, local.tm_wday + 1);
    lua_pushnumber(L, local.tm_mon + 1);
    lua_pushnumber(L, local.tm_mday);
    lua_pushnumber(L, local.tm_year + 1900);

    return 4;
}

int32_t Script_GetSendMailPrice(lua_State* L) {
    lua_pushnumber(L, 30.0);

    return 1;
}

FrameScript_Method s_ScriptFunctions[] = {
    { "GetChatWindowInfo",              &Script_GetChatWindowInfo },
    { "GetChatTypeIndex",               &Script_GetChatTypeIndex },
    { "IsVoiceChatEnabled",             &Script_ReturnNil },
    { "IsVoiceChatAllowedByServer",     &Script_ReturnNil },
    { "GetNumVoiceSessions",            &Script_ReturnZero },
    { "GetRepairAllCost",               &Script_GetRepairAllCost },
    { "GetNumShapeshiftForms",          &Script_ReturnZero },
    { "GetNumTrackingTypes",            &Script_ReturnZero },
    { "GetTrackingTexture",             &Script_ReturnNil },
    { "GetLFGProposal",                 &Script_ReturnNothing },
    { "CalendarGetDate",                &Script_CalendarGetDate },
    { "GetSpellTabInfo",                &Script_GetSpellTabInfo },
    { "GetPetActionInfo",               &Script_ReturnNothing },
    { "GetNumWorldStateUI",             &Script_ReturnZero },
    { "GetCompanionInfo",               &Script_ReturnNothing },
    { "GetArenaTeam",                   &Script_ReturnNothing },
    { "CreateWorldMapArrowFrame",       &Script_ReturnNothing },
    { "GetMasterLootCandidate",         &Script_ReturnNil },
    { "GetSelectedDisplayChannel",      &Script_ReturnZero },
    { "GetSendMailPrice",               &Script_GetSendMailPrice },
    { "GetTabardCreationCost",          &Script_ReturnZero },
    { "SetWhoToUI",                     &Script_ReturnNothing },
    { "SetSelectedSkill",               &Script_ReturnNothing },
    { "SetGuildRosterSelection",        &Script_ReturnNothing },
    { "SelectQuestLogEntry",            &Script_ReturnNothing },
    { "GetVoiceStatus",                 &Script_ReturnNil },
    { "GetNumFriends",                  &Script_ReturnZero },
    { "GetNumIgnores",                  &Script_ReturnZero },
    { "GetNumMutes",                    &Script_ReturnZero },
    { "GetNumDisplayChannels",          &Script_ReturnZero },
    { "GetNumSavedInstances",           &Script_ReturnZero },
    { "GetLFGInfoServer",               &Script_ReturnNothing },
    { "HasCompletedAnyAchievement",     &Script_ReturnNil },
    { "GetVoiceCurrentSessionID",       &Script_ReturnNil },
    { "GetChannelDisplayInfo",          &Script_ReturnNothing },
    { "SetMapToCurrentZone",            &Script_ReturnNothing },
    { "SetAbandonQuest",                &Script_ReturnNothing },
    { "RequestRaidInfo",                &Script_ReturnNothing },
    { "IsPetAttackAction",              &Script_ReturnNil },
    { "InitWorldMapPing",               &Script_ReturnNothing },
    { "HasPetSpells",                   &Script_ReturnNil },
    { "GetVoiceSessionInfo",            &Script_ReturnNothing },
    { "GetPossessInfo",                 &Script_ReturnNothing },
    { "GetNumSkillLines",               &Script_ReturnZero },
    { "GetNumLanguages",                &Script_ReturnZero },
    { "GetNumBankSlots",                &Script_ReturnZero },
    { "GetLFGQueuedList",               &Script_ReturnNothing },
    { "GetLFGDeserterExpiration",       &Script_ReturnNil },
    { "GetGuildRosterMOTD",             &Script_ReturnEmptyString },
    { "GetCurrentArenaSeason",          &Script_ReturnZero },
    { "GetContainerNumFreeSlots",       &Script_ReturnTwoZeros },
    { "CalendarGetNumPendingInvites",   &Script_ReturnZero },
    { "GetLFGRoleUpdate",               &Script_ReturnNothing },
    { "GetQuestLogSelection",           &Script_ReturnZero },
    { "GetPetActionCooldown",           &Script_ReturnThreeZeros },
    { "GetMapInfo",                     &Script_ReturnNil },
    { "GetAdjustedSkillPoints",         &Script_ReturnZero },
    { "GetNumSpellTabs",                &Script_ReturnOne },
};

} // namespace

void MiscScriptRegisterFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
