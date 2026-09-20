#include "ui/game/RaidInfoScript.hpp"
#include "ui/game/RaidTarget.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "client/ClientServices.hpp"
#include <common/DataStore.hpp>
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

// ref: FUN_00574ab0
int32_t Script_SetRaidTarget(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: SetRaidTarget(unit, index)");
        return 0;
    }

    WOWGUID guid = 0;

    if (!Script_GetGUIDFromToken(lua_tostring(L, 1), guid, false) || !guid) {
        return 0;
    }

    // The interface counts icons from 1 and the table from 0, and index 0 means "clear". The
    // reference takes the low byte before subtracting, so 256 is 0 rather than out of range.
    uint32_t requested = static_cast<uint32_t>(lua_tonumber(L, 2));
    uint32_t index = (requested & 0xFF) - 1;

    if (index >= static_cast<uint32_t>(RAID_TARGET_COUNT)) {
        // Clearing: find what the unit currently holds and drop that slot instead.
        index = static_cast<uint32_t>(RaidTargetGetIndex(guid));
        guid = 0;

        if (index >= static_cast<uint32_t>(RAID_TARGET_COUNT)) {
            return 0;
        }
    }

    // PARTIAL PORT. The reference decides here whether it may act alone: with no raid and no
    // party it applies the change locally and signals, and otherwise sends and waits for the
    // server to echo. Frozen always sends -- the local path needs the group predicates that are
    // not ported yet -- so setting a marker while solo does nothing until a server replies.
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(MSG_RAID_TARGET_UPDATE));
    msg.Put(static_cast<uint8_t>(index));
    msg.Put(static_cast<uint64_t>(guid));
    msg.Finalize();
    ClientServices::Send(&msg);

    return 0;
}

// ref: FUN_00572ab0
int32_t Script_GetRaidTargetIndex(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: GetRaidTargetIndex(unit)");
        return 0;
    }

    WOWGUID guid = 0;
    Script_GetGUIDFromToken(lua_tostring(L, 1), guid, false);

    auto index = RaidTargetGetIndex(guid);

    // Not marked answers nil, not zero -- the table's "none" is the count itself.
    if (index == RAID_TARGET_COUNT) {
        lua_pushnil(L);
    } else {
        lua_pushnumber(L, index + 1);
    }

    return 1;
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
