#include "util/Lua.hpp"
#include "ui/game/BattlefieldInfoScript.hpp"
#include "ui/game/CGBattlefieldInfo.hpp"
#include <common/DataStore.hpp>
#include <common/Time.hpp>
#include <cmath>
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "net/Types.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "util/Unimplemented.hpp"

namespace {

// ref: FUN_0054baa0
int32_t Script_GetNumBattlefields(lua_State* L) {
    lua_pushnumber(L, CGBattlefieldInfo::s_numInstances);

    return 1;
}

int32_t Script_GetBattlefieldInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0054d8f0
int32_t Script_GetBattlefieldInstanceInfo(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetBattlefieldInfo(index)");

        return 0;
    }

    auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

    if (player) {
        auto index = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1)))) - 1;

        if (index < CGBattlefieldInfo::s_numInstances) {
            lua_pushnumber(L, CGBattlefieldInfo::s_instanceIDs[index]);

            return 1;
        }
    }

    return 0;
}

int32_t Script_IsBattlefieldArena(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsActiveBattlefieldArena(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_JoinBattlefield(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0054bb40
int32_t Script_SetSelectedBattlefield(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetSelectedBattlefield(index)");

        return 0;
    }

    auto index = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1)))) - 1;

    if (index < CGBattlefieldInfo::s_numInstances) {
        CGBattlefieldInfo::s_selectedInstance = CGBattlefieldInfo::s_instanceIDs[index];

        return 0;
    }

    CGBattlefieldInfo::s_selectedInstance = 0;

    return 0;
}

// ref: FUN_0054bbd0
int32_t Script_GetSelectedBattlefield(lua_State* L) {
    uint32_t index = 0;

    while (index < CGBattlefieldInfo::s_numInstances) {
        if (CGBattlefieldInfo::s_instanceIDs[index] == CGBattlefieldInfo::s_selectedInstance) {
            break;
        }

        index++;
    }

    if (index >= CGBattlefieldInfo::s_numInstances) {
        index = 0xFFFFFFFF;
    }

    lua_pushnumber(L, static_cast<int32_t>(index + 1));

    return 1;
}

int32_t Script_AcceptBattlefieldPort(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetBattlefieldStatus(lua_State* L) {
    lua_pushstring(L, "none");

    return 1;
}

// ref: FUN_00549b80
int32_t Script_GetBattlefieldPortExpiration(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetBattlefieldPortExpiration(index)");

        return 0;
    }

    auto index = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1)))) - 1;
    auto slot = index < 2 ? &CGBattlefieldInfo::s_queueSlots[index] : nullptr;

    if (slot && slot->m_portExpireTime) {
        auto now = static_cast<int32_t>(OsGetAsyncTimeMs());

        if (now - slot->m_portExpireTime < 0) {
            lua_pushnumber(L, static_cast<uint32_t>(slot->m_portExpireTime - now) / 1000);

            return 1;
        }
    }

    lua_pushnumber(L, 0.0);

    return 1;
}

// ref: FUN_00549c40
// Milliseconds, unlike the port expiration above: the reference does not divide here.
int32_t Script_GetBattlefieldInstanceExpiration(lua_State* L) {
    if (!CGBattlefieldInfo::s_instanceExpireTime) {
        lua_pushnumber(L, 0.0);

        return 1;
    }

    auto now = static_cast<int32_t>(OsGetAsyncTimeMs());

    if (now - CGBattlefieldInfo::s_instanceExpireTime >= 0) {
        lua_pushnumber(L, 0.0);

        return 1;
    }

    lua_pushnumber(L, static_cast<uint32_t>(CGBattlefieldInfo::s_instanceExpireTime - now));

    return 1;
}

// ref: FUN_00549cd0
int32_t Script_GetBattlefieldInstanceRunTime(lua_State* L) {
    auto startTime = CGBattlefieldInfo::s_instanceStartTime;

    if (startTime) {
        auto now = static_cast<int32_t>(OsGetAsyncTimeMs());

        lua_pushnumber(L, static_cast<uint32_t>(now - startTime));

        return 1;
    }

    lua_pushnumber(L, 0.0);

    return 1;
}

// ref: FUN_00549d30
int32_t Script_GetBattlefieldEstimatedWaitTime(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetBattlefieldEstimatedWaitTime(index)");

        return 0;
    }

    auto index = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1)))) - 1;
    auto slot = index < 2 ? &CGBattlefieldInfo::s_queueSlots[index] : nullptr;

    if (slot) {
        lua_pushnumber(L, slot->m_estimatedWaitTime);
    } else {
        lua_pushnumber(L, 0.0);
    }

    return 1;
}

// ref: FUN_00549dd0
int32_t Script_GetBattlefieldTimeWaited(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetBattlefieldTimeWaited(index)");

        return 0;
    }

    auto index = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1)))) - 1;
    auto slot = index < 2 ? &CGBattlefieldInfo::s_queueSlots[index] : nullptr;

    if (slot && slot->m_queueJoinTime) {
        auto now = static_cast<int32_t>(OsGetAsyncTimeMs());

        lua_pushnumber(L, static_cast<uint32_t>(now - slot->m_queueJoinTime));
    } else {
        lua_pushnumber(L, 0.0);
    }

    return 1;
}

int32_t Script_CloseBattlefield(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RequestBattlefieldScoreData(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00549e80
int32_t Script_GetNumBattlefieldScores(lua_State* L) {
    lua_pushnumber(L, CGBattlefieldInfo::s_numScores);

    return 1;
}

int32_t Script_GetBattlefieldScore(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00549ec0
int32_t Script_GetBattlefieldWinner(lua_State* L) {
    if (!CGBattlefieldInfo::s_hasWinner) {
        lua_pushnil(L);

        return 1;
    }

    lua_pushnumber(L, CGBattlefieldInfo::s_winner);

    return 1;
}

int32_t Script_SetBattlefieldScoreFaction(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_LeaveBattlefield(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetNumBattlefieldStats(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetBattlefieldStatInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00549f60
int32_t Script_GetBattlefieldStatData(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: GetBattlefieldStatData(playerIndex, statIndex)");

        return 0;
    }

    auto playerIndex = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1)))) - 1;
    CGBattlefieldInfo::ScoreEntry* score;

    if (playerIndex < CGBattlefieldInfo::s_scoreListCount && (score = CGBattlefieldInfo::s_scoreList[playerIndex])) {
        auto statIndex = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 2)))) - 1;

        if (statIndex < 9) {
            lua_pushnumber(L, score->m_stats[statIndex]);

            return 1;
        }
    }

    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_RequestBattlefieldPositions(lua_State* L) {
    CGBattlefieldInfo::RequestPlayerPositions();

    return 0;
}

int32_t Script_GetNumBattlefieldPositions(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetBattlefieldPosition(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0054a0e0
int32_t Script_GetNumBattlefieldFlagPositions(lua_State* L) {
    int32_t count;

    if (CGBattlefieldInfo::s_flagCarriers[1] == 0) {
        count = CGBattlefieldInfo::s_flagCarriers[0] == 0 ? 0 : 1;
    } else {
        count = 2;
    }

    lua_pushnumber(L, count);

    return 1;
}

int32_t Script_GetBattlefieldFlagPosition(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0054a140
int32_t Script_GetNumBattlefieldVehicles(lua_State* L) {
    lua_pushnumber(L, CGBattlefieldInfo::s_numVehicles);

    return 1;
}

int32_t Script_GetBattlefieldVehicleInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CanJoinBattlefieldAsGroup(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

// ref: FUN_0054c740
int32_t Script_GetBattlefieldMapIconScale(lua_State* L) {
    auto mapRec = g_mapDB.GetRecord(CGBattlefieldInfo::s_mapID);

    if (mapRec) {
        lua_pushnumber(L, mapRec->m_minimapIconScale);

        return 1;
    }

    lua_pushnumber(L, 1.0);

    return 1;
}

// ref: FUN_0054a180
// The index is zero-based here, unlike the other battlefield bindings.
int32_t Script_GetBattlefieldTeamInfo(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetBattlefieldTeamInfo(index)");

        return 0;
    }

    auto index = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1))));

    lua_pushstring(L, index < 2 ? CGBattlefieldInfo::s_teamNames[index] : nullptr);
    lua_pushnumber(L, index < 2 ? CGBattlefieldInfo::s_teamOldRating[index] : 0);
    lua_pushnumber(L, index < 2 ? CGBattlefieldInfo::s_teamNewRating[index] : 0);
    lua_pushnumber(L, index < 2 ? CGBattlefieldInfo::s_teamRating[index] : 0);

    return 4;
}

// ref: FUN_0054a280
int32_t Script_GetBattlefieldArenaFaction(lua_State* L) {
    if (CGBattlefieldInfo::s_arenaFactionFlag) {
        lua_pushnumber(L, 1.0);

        return 1;
    }

    lua_pushnil(L);

    return 1;
}

int32_t Script_SortBattlefieldScoreData(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0054c7a0
int32_t Script_HearthAndResurrectFromArea(lua_State* L) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_HEARTH_AND_RESURRECT));
    msg.Finalize();
    ClientServices::Send(&msg);

    return 0;
}

int32_t Script_CanHearthAndResurrectFromArea(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GetNumBattlegroundTypes(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetBattlegroundInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RequestBattlegroundInstanceInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetNumArenaOpponents(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_BattlefieldMgrEntryInviteResponse(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_BattlefieldMgrQueueRequest(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_BattlefieldMgrQueueInviteResponse(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_BattlefieldMgrExitRequest(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetWorldPVPQueueStatus(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetHolidayBGHonorCurrencyBonuses(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetRandomBGHonorCurrencyBonuses(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SortBGList(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GetNumBattlefields",                 &Script_GetNumBattlefields },
    { "GetBattlefieldInfo",                 &Script_GetBattlefieldInfo },
    { "GetBattlefieldInstanceInfo",         &Script_GetBattlefieldInstanceInfo },
    { "IsBattlefieldArena",                 &Script_IsBattlefieldArena },
    { "IsActiveBattlefieldArena",           &Script_IsActiveBattlefieldArena },
    { "JoinBattlefield",                    &Script_JoinBattlefield },
    { "SetSelectedBattlefield",             &Script_SetSelectedBattlefield },
    { "GetSelectedBattlefield",             &Script_GetSelectedBattlefield },
    { "AcceptBattlefieldPort",              &Script_AcceptBattlefieldPort },
    { "GetBattlefieldStatus",               &Script_GetBattlefieldStatus },
    { "GetBattlefieldPortExpiration",       &Script_GetBattlefieldPortExpiration },
    { "GetBattlefieldInstanceExpiration",   &Script_GetBattlefieldInstanceExpiration },
    { "GetBattlefieldInstanceRunTime",      &Script_GetBattlefieldInstanceRunTime },
    { "GetBattlefieldEstimatedWaitTime",    &Script_GetBattlefieldEstimatedWaitTime },
    { "GetBattlefieldTimeWaited",           &Script_GetBattlefieldTimeWaited },
    { "CloseBattlefield",                   &Script_CloseBattlefield },
    { "RequestBattlefieldScoreData",        &Script_RequestBattlefieldScoreData },
    { "GetNumBattlefieldScores",            &Script_GetNumBattlefieldScores },
    { "GetBattlefieldScore",                &Script_GetBattlefieldScore },
    { "GetBattlefieldWinner",               &Script_GetBattlefieldWinner },
    { "SetBattlefieldScoreFaction",         &Script_SetBattlefieldScoreFaction },
    { "LeaveBattlefield",                   &Script_LeaveBattlefield },
    { "GetNumBattlefieldStats",             &Script_GetNumBattlefieldStats },
    { "GetBattlefieldStatInfo",             &Script_GetBattlefieldStatInfo },
    { "GetBattlefieldStatData",             &Script_GetBattlefieldStatData },
    { "RequestBattlefieldPositions",        &Script_RequestBattlefieldPositions },
    { "GetNumBattlefieldPositions",         &Script_GetNumBattlefieldPositions },
    { "GetBattlefieldPosition",             &Script_GetBattlefieldPosition },
    { "GetNumBattlefieldFlagPositions",     &Script_GetNumBattlefieldFlagPositions },
    { "GetBattlefieldFlagPosition",         &Script_GetBattlefieldFlagPosition },
    { "GetNumBattlefieldVehicles",          &Script_GetNumBattlefieldVehicles },
    { "GetBattlefieldVehicleInfo",          &Script_GetBattlefieldVehicleInfo },
    { "CanJoinBattlefieldAsGroup",          &Script_CanJoinBattlefieldAsGroup },
    { "GetBattlefieldMapIconScale",         &Script_GetBattlefieldMapIconScale },
    { "GetBattlefieldTeamInfo",             &Script_GetBattlefieldTeamInfo },
    { "GetBattlefieldArenaFaction",         &Script_GetBattlefieldArenaFaction },
    { "SortBattlefieldScoreData",           &Script_SortBattlefieldScoreData },
    { "HearthAndResurrectFromArea",         &Script_HearthAndResurrectFromArea },
    { "CanHearthAndResurrectFromArea",      &Script_CanHearthAndResurrectFromArea },
    { "GetNumBattlegroundTypes",            &Script_GetNumBattlegroundTypes },
    { "GetBattlegroundInfo",                &Script_GetBattlegroundInfo },
    { "RequestBattlegroundInstanceInfo",    &Script_RequestBattlegroundInstanceInfo },
    { "GetNumArenaOpponents",               &Script_GetNumArenaOpponents },
    { "BattlefieldMgrEntryInviteResponse",  &Script_BattlefieldMgrEntryInviteResponse },
    { "BattlefieldMgrQueueRequest",         &Script_BattlefieldMgrQueueRequest },
    { "BattlefieldMgrQueueInviteResponse",  &Script_BattlefieldMgrQueueInviteResponse },
    { "BattlefieldMgrExitRequest",          &Script_BattlefieldMgrExitRequest },
    { "GetWorldPVPQueueStatus",             &Script_GetWorldPVPQueueStatus },
    { "GetHolidayBGHonorCurrencyBonuses",   &Script_GetHolidayBGHonorCurrencyBonuses },
    { "GetRandomBGHonorCurrencyBonuses",    &Script_GetRandomBGHonorCurrencyBonuses },
    { "SortBGList",                         &Script_SortBGList },
};

void BattlefieldInfoRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
