#include "ui/game/RaidInfoScript.hpp"
#include "ui/game/RaidTarget.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "client/ClientServices.hpp"
#include <common/DataStore.hpp>
#include "ui/FrameScript.hpp"
#include "ui/game/CGRaidInfo.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "ui/Util.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <common/Time.hpp>
#include <cmath>

namespace {

// The ready check's deadline in milliseconds, 0 when none is running. Written by the ready-check
// message handlers, which are not ported yet, so it reads 0 -- no check in progress.
uint32_t s_readyCheckDeadlineMs = 0; // ref: DAT_00beb61c

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

// ref: FUN_00572bc0
int32_t Script_SetRaidRosterSelection(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetRaidRosterSelection(index)");
        return 0;
    }

    int32_t index = static_cast<int32_t>(llrint(lua_tonumber(L, 1)));

    if (static_cast<uint32_t>(index - 1) < CGRaidInfo::NumMembers()) {
        CGRaidInfo::s_selection = CGRaidInfo::GetMember(index);
        return 0;
    }

    CGRaidInfo::s_selection = 0;

    return 0;
}

int32_t Script_GetRaidRosterSelection(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsRaidLeader(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

// ref: FUN_00573ab0
// Compares against the party's REAL leader, and without IsRealPartyLeader's non-zero test.
int32_t Script_IsRealRaidLeader(lua_State* L) {
    if (ClntObjMgrGetActivePlayer() != CGPartyInfo::GetRealLeader()) {
        lua_pushnil(L);

        return 1;
    }

    lua_pushnumber(L, 1.0);

    return 1;
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

// ref: FUN_00574a00
// Only the party leader, with at least one member, and at level 10 or above.
int32_t Script_ConvertToRaid(lua_State* L) {
    auto leader = CGPartyInfo::GetLeader();

    if (!CGPartyInfo::GetMember(1) || ClntObjMgrGetActivePlayer() != leader) {
        return 0;
    }

    auto player = CGPlayer_C::GetActivePtr();

    if (player && player->Unit()->level > 9) {
        CDataStore msg;
        msg.Put(static_cast<uint32_t>(CMSG_GROUP_RAID_CONVERT));
        msg.Finalize();
        ClientServices::Send(&msg);
    }

    return 0;
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

// ref: FUN_005740c0
int32_t Script_ConfirmReadyCheck(lua_State* L) {
    if (!CGRaidInfo::NumMembers() && !CGPartyInfo::GetMember(1)) {
        return 0;
    }

    auto now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    if (s_readyCheckDeadlineMs && static_cast<int32_t>(now - s_readyCheckDeadlineMs) < 0
        && s_readyCheckDeadlineMs != now) {
        auto ready = static_cast<uint8_t>(StringToBOOL(L, 1, 0));

        CDataStore msg;
        msg.Put(static_cast<uint32_t>(MSG_RAID_READY_CHECK));
        msg.Put(ready);
        msg.Finalize();
        ClientServices::Send(&msg);
    }

    return 0;
}

// ref: FUN_00572c80
int32_t Script_GetReadyCheckTimeLeft(lua_State* L) {
    if (!CGRaidInfo::NumMembers() && !CGPartyInfo::GetMember(1)) {
        lua_pushnumber(L, 0.0);

        return 1;
    }

    auto now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    uint32_t remaining;

    if (!s_readyCheckDeadlineMs || static_cast<int32_t>(now - s_readyCheckDeadlineMs) >= 0) {
        remaining = 0;
    } else {
        remaining = s_readyCheckDeadlineMs - now;
    }

    lua_pushnumber(L, static_cast<double>(remaining / 1000));

    return 1;
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
