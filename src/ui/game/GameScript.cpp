#include "ui/AddOn.hpp"
#include <storm/String.hpp>
#include "ui/game/GameScript.hpp"
#include "event/Event.hpp"
#include "db/Db.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/Types.hpp"
#include "world/Terrain.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "console/CVar.hpp"
#include <common/DataStore.hpp>
#include "client/ClientServices.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "ui/Types.hpp"
#include "console/Command.hpp"
#include "gx/Coordinate.hpp"
#include "ui/FrameScript.hpp"
#include "ui/ScriptFunctionsShared.hpp"
#include "ui/Util.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/Types.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include "gx/Screen.hpp"
#include "util/Filesystem.hpp"
#include "util/Unimplemented.hpp"
#include <ctime>

namespace {

int32_t Script_FrameXML_Debug(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetBuildInfo(lua_State* L) {
    lua_pushstring(L, "3.3.5");
    lua_pushstring(L, "12340");
    lua_pushstring(L, "Jun 24 2010");
    lua_pushnumber(L, 30300.0);

    return 4;
}

int32_t Script_ReloadUI(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RegisterForSave(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RegisterForSavePerCharacter(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetLayoutMode(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsModifierKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_LSHIFT) || EventIsKeyDown(KEY_RSHIFT) || EventIsKeyDown(KEY_LCONTROL) || EventIsKeyDown(KEY_RCONTROL) || EventIsKeyDown(KEY_LALT) || EventIsKeyDown(KEY_RALT)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsLeftShiftKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_LSHIFT)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsRightShiftKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_RSHIFT)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsShiftKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_LSHIFT) || EventIsKeyDown(KEY_RSHIFT)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsLeftControlKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_LCONTROL)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsRightControlKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_RCONTROL)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsControlKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_LCONTROL) || EventIsKeyDown(KEY_RCONTROL)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsLeftAltKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_LALT)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsRightAltKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_RALT)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsAltKeyDown(lua_State* L) {
    if (EventIsKeyDown(KEY_LALT) || EventIsKeyDown(KEY_RALT)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_IsMouseButtonDown(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetMouseButtonName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetMouseButtonClicked(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetConsoleKey(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_Screenshot(lua_State* L) {
    // Screenshots/WoWScrnShot_MMDDYY_HHMMSS.tga, the name the reference writes. The interface calls
    // this from a binding, and FrameXML has no way to name the file, so the name is made here.
    if (!OsDirectoryExists("Screenshots")) {
        OsCreateDirectory("Screenshots", 0);
    }

    time_t now = time(nullptr);
    struct tm parts;

    #if defined(WHOA_SYSTEM_WIN)
        localtime_s(&parts, &now);
    #else
        localtime_r(&now, &parts);
    #endif

    char path[260];

    SStrPrintf(path, sizeof(path), "Screenshots\\WoWScrnShot_%02d%02d%02d_%02d%02d%02d.tga",
               parts.tm_mon + 1, parts.tm_mday, parts.tm_year % 100,
               parts.tm_hour, parts.tm_min, parts.tm_sec);

    // Hand the name to the layer code and let it capture just before the frame is presented. The
    // interface is still drawing at this point, so grabbing the back buffer here would catch a
    // half-composed frame.
    SStrCopy(Screen::s_capturePath, path, sizeof(Screen::s_capturePath));
    Screen::s_captureScreen = 1;

    // Index 171 from g_scriptEvents. The capture has not happened yet, so this reports that one was
    // requested; there is no path by which the layer code can report back a failure today.
    FrameScript_SignalEvent(171, nullptr);

    return 0;
}

int32_t Script_GetFramerate(lua_State* L) {
    // TODO the measured frame rate
    lua_pushnumber(L, 60.0);

    return 1;
}

int32_t Script_TogglePerformanceDisplay(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

int32_t Script_TogglePerformancePause(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

int32_t Script_TogglePerformanceValues(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

int32_t Script_ResetPerformanceValues(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

int32_t Script_GetDebugStats(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

// ref: FUN_0050fe80
// A constant false in the shipped client, not a check: the reference pushes the boolean 0 and
// returns, with nothing behind it to consult.
int32_t Script_IsDebugBuild(lua_State* L) {
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_RegisterCVar(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetCVarInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetCVar(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: SetCVar(\"cvar\", value [, \"scriptCvar\")");
        return 0;
    }

    auto varName = lua_tostring(L, 1);
    auto var = CVar::LookupRegistered(varName);

    if (!var || (var->m_flags & 0x40)) {
        luaL_error(L, "Couldn't find CVar named '%s'", varName);
        return 0;
    }

    if (var->m_flags & 0x4 || var->m_flags & 0x100) {
        luaL_error(L, "\"%s\" is read-only", varName);
        return 0;
    }

    if (!(var->m_flags & 0x8)/* TODO || CSimpleTop::GetInstance()->dword124C */) {
        auto value = lua_tostring(L, 2);
        if (!value) {
            value = "0";
        }

        var->Set(value, true, false, false, true);

        if (lua_isstring(L, 3)) {
            auto scriptVarName = lua_tostring(L, 3);
            FrameScript_SignalEvent(SCRIPT_CVAR_UPDATE, "%s%s", scriptVarName, value);
        }
    } else {
        // TODO CGGameUI::ShowBlockedActionFeedback(nullptr, 2);
    }

    return 0;
}

int32_t Script_GetCVar(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: GetCVar(\"cvar\")");
        return 0;
    }

    auto varName = lua_tostring(L, 1);
    auto var = CVar::LookupRegistered(varName);

    if (var && !(var->m_flags & 0x40)) {
        lua_pushstring(L, var->GetString());
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_GetCVarBool(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: GetCVarBool(\"cvar\")");
        return 0;
    }

    auto varName = lua_tostring(L, 1);
    auto var = CVar::LookupRegistered(varName);

    if (var && !(var->m_flags & 0x40) && StringToBOOL(var->GetString())) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_GetCVarDefault(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: GetCVarDefault(\"cvar\")");
        return 0;
    }

    auto varName = lua_tostring(L, 1);
    auto var = CVar::LookupRegistered(varName);

    if (!var || (var->m_flags & 0x40)) {
        luaL_error(L, "Couldn't find CVar named '%s'", varName);
        return 0;
    }

    lua_pushstring(L, var->GetDefaultValue());

    return 1;
}

int32_t Script_GetCVarMin(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_GetCVarMax(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_GetCVarAbsoluteMin(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetCVarAbsoluteMax(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00608560
int32_t Script_GetWaterDetail(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

// ref: FUN_005101d0
int32_t Script_SetWaterDetail(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetWaterDetail(value)");
    }

    return 0;
}

// ref: FUN_00510200
int32_t Script_GetFarclip(lua_State* L) {
    auto var = CVar::Lookup("farclip");

    lua_pushnumber(L, var->GetFloat());

    return 1;
}

// ref: FUN_00510230
int32_t Script_SetFarclip(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetFarclip(value)");

        return 0;
    }

    auto var = CVar::Lookup("farclip");

    char value[16];
    SStrPrintf(value, sizeof(value), "%f", lua_tonumber(L, 1));

    var->Set(value, true, false, false, true);

    return 0;
}

// ref: FUN_005102b0
int32_t Script_GetTexLodBias(lua_State* L) {
    auto var = CVar::Lookup("texLodBias");

    lua_pushnumber(L, var->GetFloat());

    return 1;
}

// ref: FUN_005102e0
int32_t Script_SetTexLodBias(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetTexLodBias(value)");

        return 0;
    }

    auto var = CVar::Lookup("texLodBias");

    char value[16];
    SStrPrintf(value, sizeof(value), "%f", lua_tonumber(L, 1));

    var->Set(value, true, false, false, true);

    return 0;
}

// ref: FUN_00510390
int32_t Script_SetBaseMip(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetBaseMip(value)");

        return 0;
    }

    auto var = CVar::Lookup("baseMip");

    char value[16];
    SStrPrintf(value, sizeof(value), "%d", static_cast<int32_t>(lua_tonumber(L, 1) + 0.5));

    var->Set(value, true, false, false, true);

    return 0;
}

// ref: FUN_00510360
int32_t Script_GetBaseMip(lua_State* L) {
    auto var = CVar::Lookup("baseMip");

    lua_pushnumber(L, 1.0f - var->GetFloat());

    return 1;
}

int32_t Script_ToggleTris(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

int32_t Script_TogglePortals(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

int32_t Script_ToggleCollision(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

int32_t Script_ToggleCollisionDisplay(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

int32_t Script_TogglePlayerBounds(lua_State* L) {
    // Compiled out of the shipped client: the reference registers this and returns nothing.
    return 0;
}

int32_t Script_Stuck(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_Logout(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_Quit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetCursor(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ResetCursor(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ClearCursor(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CursorHasItem(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_CursorHasSpell(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CursorHasMacro(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CursorHasMoney(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetCursorInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_EquipCursorItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DeleteCursorItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_EquipPendingItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CancelPendingEquip(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetNearest(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetNearestEnemy(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetNearestEnemyPlayer(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetNearestFriend(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetNearestFriendPlayer(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetNearestPartyMember(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetNearestRaidMember(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetDirectionEnemy(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetDirectionFriend(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetDirectionFinished(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetLastTarget(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetLastEnemy(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetLastFriend(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AttackTarget(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AssistUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_FocusUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_FollowUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_InteractUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ClearTarget(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ClearFocus(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AutoEquipCursorItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ToggleSheath(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// The AreaTable row the player is standing on, and its parent zone.
//
// AreaTable is a two-level hierarchy: a subzone ("Acherus: The Ebon Hold") carries a parentAreaID
// pointing at the zone that contains it, and a top-level zone has parentAreaID 0. The four zone-text
// bindings are all views on that pair, which is why they share one resolver.
void ResolveArea(const AreaTableRec** area, const AreaTableRec** zone) {
    *area = nullptr;
    *zone = nullptr;

    auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

    if (!player) {
        return;
    }

    uint32_t areaID = TerrainAreaIDAt(player->GetPosition());

    if (!areaID) {
        return;
    }

    auto rec = g_areaTableDB.GetRecord(static_cast<int32_t>(areaID));

    if (!rec) {
        return;
    }

    *area = rec;
    *zone = rec;

    // Walk up to the top-level zone. Bounded rather than while(true): a malformed DBC with a cycle
    // in parentAreaID would otherwise hang the client on every frame the zone text updates.
    for (int32_t depth = 0; depth < 8 && (*zone)->m_parentAreaID; depth++) {
        auto parent = g_areaTableDB.GetRecord((*zone)->m_parentAreaID);

        if (!parent) {
            break;
        }

        *zone = parent;
    }
}

void PushAreaName(lua_State* L, const AreaTableRec* rec) {
    if (rec && rec->m_areaName && *rec->m_areaName) {
        lua_pushstring(L, rec->m_areaName);
    } else {
        lua_pushstring(L, "");
    }
}

int32_t Script_GetZoneText(lua_State* L) {
    const AreaTableRec* area;
    const AreaTableRec* zone;
    ResolveArea(&area, &zone);

    // The containing zone, which is what the map and the zone banner show.
    PushAreaName(L, zone);

    return 1;
}

int32_t Script_GetRealZoneText(lua_State* L) {
    const AreaTableRec* area;
    const AreaTableRec* zone;
    ResolveArea(&area, &zone);

    // Identical to GetZoneText outside instances, where the reference substitutes the instance name.
    PushAreaName(L, zone);

    return 1;
}

int32_t Script_GetSubZoneText(lua_State* L) {
    const AreaTableRec* area;
    const AreaTableRec* zone;
    ResolveArea(&area, &zone);

    // Empty when the player is standing in the zone itself rather than a subzone of it, which is
    // what FrameXML tests to decide whether to show the second line at all.
    PushAreaName(L, area == zone ? nullptr : area);

    return 1;
}

int32_t Script_GetMinimapZoneText(lua_State* L) {
    const AreaTableRec* area;
    const AreaTableRec* zone;
    ResolveArea(&area, &zone);

    // The minimap label prefers the most specific name available.
    PushAreaName(L, area ? area : zone);

    return 1;
}

int32_t Script_InitiateTrade(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CanInspect(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_NotifyInspect(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_InviteUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UninviteUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RequestTimePlayed(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RepopMe(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AcceptResurrect(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DeclineResurrect(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ResurrectGetOfferer(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ResurrectHasSickness(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_ResurrectHasTimer(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_BeginTrade(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CancelTrade(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AcceptGroup(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DeclineGroup(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AcceptGuild(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DeclineGuild(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AcceptArenaTeam(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DeclineArenaTeam(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CancelLogout(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ForceLogout(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ForceQuit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetCursorMoney(lua_State* L) {
    lua_pushnumber(L, CGGameUI::GetCursorMoney());

    return 1;
}

int32_t Script_DropCursorMoney(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_PickupPlayerMoney(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_HasSoulstone(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UseSoulstone(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_HasKey(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GuildInvite(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GuildUninvite(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GuildPromote(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GuildDemote(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GuildSetLeader(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GuildSetMOTD(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GuildLeave(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GuildDisband(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GuildInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ArenaTeamInviteByName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ArenaTeamLeave(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ArenaTeamUninviteByName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ArenaTeamSetLeaderByName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ArenaTeamDisband(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetScreenWidth(lua_State* L) {
    // The UI's coordinate space is 768 units tall at any window size
    CRect rect;
    GxCapsWindowSize(rect);

    float width = rect.maxX - rect.minX;
    float height = rect.maxY - rect.minY;

    lua_pushnumber(L, height > 0.0f ? 768.0f * width / height : 1024.0f);

    return 1;
}

int32_t Script_GetScreenHeight(lua_State* L) {
    lua_pushnumber(L, 768.0);

    return 1;
}

int32_t Script_GetDamageBonusStat(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetReleaseTimeRemaining(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetCorpseRecoveryDelay(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetInstanceBootTimeRemaining(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetInstanceLockTimeRemaining(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetInstanceLockTimeRemainingEncounter(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetSummonConfirmTimeLeft(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetSummonConfirmSummoner(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetSummonConfirmAreaName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ConfirmSummon(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CancelSummon(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetCursorPosition(lua_State* L) {
    STORM_ASSERT(CSimpleTop::s_instance);

    float ddcX = 0.0f;
    float ddcY = 0.0f;

    NDCToDDC(
        CSimpleTop::s_instance->m_mousePosition.x,
        CSimpleTop::s_instance->m_mousePosition.y,
        &ddcX,
        &ddcY
    );

    float ndcX = DDCToNDCWidth(CoordinateGetAspectCompensation() * 1024.0f * ddcX);
    lua_pushnumber(L, ndcX);

    float ndcY = DDCToNDCWidth(CoordinateGetAspectCompensation() * 1024.0f * ddcY);
    lua_pushnumber(L, ndcY);

    return 2;
}

int32_t Script_GetNetStats(lua_State* L) {
    // bandwidth in, bandwidth out, home latency, world latency
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 4;
}

int32_t Script_SitStandOrDescendStart(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_StopCinematic(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RunScript(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CheckInteractDistance(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RandomRoll(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_OpeningCinematic(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_InCinematic(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_AcceptXPLoss(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CheckSpiritHealerDist(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CheckTalentMasterDist(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CheckBinderDist(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RetrieveCorpse(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_BindEnchant(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ReplaceEnchant(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ReplaceTradeEnchant(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_NotWhileDeadError(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetRestState(lua_State* L) {
    // 1 rested, 2 normal
    lua_pushnumber(L, 2.0);
    lua_pushstring(L, "Normal");
    lua_pushnumber(L, 1.0);

    return 3;
}

int32_t Script_GetXPExhaustion(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_GetTimeToWellRested(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_GMRequestPlayerInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// The icon directory. The reference does not hold this as a literal: it reads row 3 of a table
// through FUN_00634910 and joins it to the icon name with a backslash, skipping the backslash when
// the row is empty. That table has not been identified, so the value is taken from the client's own
// shipped data instead of guessed -- Minimap.xml and QuestFrameTemplates.xml both address icons as
// "Interface\\Icons\\<name>", and one of them names inv_misc_coin_02, which is exactly an icon
// CoinIconFormat below returns. DIVERGENCE: hardcoded where the reference is data-driven.
static const char* ICON_DIRECTORY = "Interface\\Icons";

// ref: FUN_007e7cc0
// Six coins by amount, with the thresholds read out of the decompilation rather than reasoned
// about: under 10, 100, 1000, 10000 and 100000, then everything above.
static void CoinIconFormat(int32_t amount, char* icon, size_t iconSize) {
    const char* name = "INV_Misc_Coin_02";

    if (amount < 10) {
        name = "INV_Misc_Coin_05";
    } else if (amount < 100) {
        name = "INV_Misc_Coin_06";
    } else if (amount < 1000) {
        name = "INV_Misc_Coin_03";
    } else if (amount < 10000) {
        name = "INV_Misc_Coin_04";
    } else if (amount < 100000) {
        name = "INV_Misc_Coin_01";
    }

    SStrPrintf(icon, iconSize, "%s%s%s", ICON_DIRECTORY, *ICON_DIRECTORY ? "\\" : "", name);
}

// ref: FUN_00510bd0
int32_t Script_GetCoinIcon(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetCoinIcon(amount)");

        return 0;
    }

    char icon[260];
    CoinIconFormat(static_cast<int32_t>(lua_tonumber(L, 1) + 0.5), icon, sizeof(icon));

    lua_pushstring(L, icon);

    return 1;
}

// ref: FUN_007e7c70
// Splits a copper amount into its three coin denominations. The reference writes them into one
// struct in the order copper, silver, gold; the caller walks it backwards, which is why the text
// comes out largest first.
static void CoinSplit(int32_t amount, int32_t& gold, int32_t& silver, int32_t& copper) {
    gold = amount / 10000;
    silver = (amount / 100) % 100;
    copper = amount % 100;
}

// ref: FUN_007e7d80
// Gold first, then silver, then copper, each skipped when it is zero, each rendered through its own
// localized global string (GOLD_AMOUNT, SILVER_AMOUNT, COPPER_AMOUNT) and joined by the caller's
// separator. The three global-string names come from the reference's own table at 00af4914.
static void CoinTextFormat(int32_t amount, char* text, size_t textSize, const char* separator) {
    int32_t gold, silver, copper;
    CoinSplit(amount, gold, silver, copper);

    const struct {
        int32_t value;
        const char* globalString;
    } parts[] = {
        { gold,   "GOLD_AMOUNT" },
        { silver, "SILVER_AMOUNT" },
        { copper, "COPPER_AMOUNT" },
    };

    text[0] = '\0';

    for (const auto& part : parts) {
        if (!part.value) {
            continue;
        }

        if (text[0]) {
            SStrPack(text, separator, textSize);
        }

        // TODO the reference passes a fourth argument here that Frozen's FrameScript_GetText does
        // not take. It is either the plural count for the global string or the value substituted
        // into it; the two cannot be told apart from the decompilation, so the count is passed as
        // -1 the way the visible second argument reads, and the value is substituted below.
        auto format = FrameScript_GetText(part.globalString, -1, GENDER_NOT_APPLICABLE);

        char rendered[1024];
        SStrPrintf(rendered, sizeof(rendered), format, part.value);

        SStrPack(text, rendered, textSize);
    }
}

// ref: FUN_00510c60
int32_t Script_GetCoinText(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetCoinText(amount [,separator])");

        return 0;
    }

    // The reference defaults the separator to ", ", the same string its font-flag joiner uses.
    auto separator = luaL_optstring(L, 2, ", ");

    char text[1024];
    CoinTextFormat(static_cast<int32_t>(lua_tonumber(L, 1) + 0.5), text, sizeof(text), separator);

    lua_pushstring(L, text);

    return 1;
}

// ref: FUN_007e7e10
// Same walk as CoinTextFormat, but through the *_AMOUNT_TEXTURE global strings -- which carry the
// inline texture markup -- joined by a single space rather than the caller's separator. Each format
// takes the value and the font height twice, since the markup sizes the icon in both axes. Zero is
// the one special case: it renders as copper rather than as nothing.
static void CoinTextureFormat(int32_t amount, char* text, size_t textSize, int32_t fontHeight) {
    if (!amount) {
        auto format = FrameScript_GetText("COPPER_AMOUNT_TEXTURE", -1, GENDER_NOT_APPLICABLE);
        SStrPrintf(text, textSize, format, 0, fontHeight, fontHeight);

        return;
    }

    int32_t gold, silver, copper;
    CoinSplit(amount, gold, silver, copper);

    const struct {
        int32_t value;
        const char* globalString;
    } parts[] = {
        { gold,   "GOLD_AMOUNT_TEXTURE" },
        { silver, "SILVER_AMOUNT_TEXTURE" },
        { copper, "COPPER_AMOUNT_TEXTURE" },
    };

    text[0] = '\0';

    for (const auto& part : parts) {
        if (!part.value) {
            continue;
        }

        if (text[0]) {
            SStrPack(text, " ", textSize);
        }

        auto format = FrameScript_GetText(part.globalString, -1, GENDER_NOT_APPLICABLE);

        char rendered[1024];
        SStrPrintf(rendered, sizeof(rendered), format, part.value, fontHeight, fontHeight);

        SStrPack(text, rendered, textSize);
    }
}

// ref: FUN_00510d00
int32_t Script_GetCoinTextureString(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        // The reference's own usage string names the wrong function here; kept as it is.
        luaL_error(L, "Usage: GetCoinText(amount, fontHeight)");

        return 0;
    }

    int32_t fontHeight = 14;

    if (lua_isnumber(L, 2)) {
        fontHeight = static_cast<int32_t>(lua_tonumber(L, 2) + 0.5);
    }

    char text[1024];
    CoinTextureFormat(static_cast<int32_t>(lua_tonumber(L, 1) + 0.5), text, sizeof(text), fontHeight);

    lua_pushstring(L, text);

    return 1;
}

int32_t Script_IsSubZonePVPPOI(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GetZonePVPInfo(lua_State* L) {
    // pvpType, isSubZonePVP, factionName.
    //
    // nil for the type is what the reference answers in a normal contested zone, and is the value
    // the zone-text colouring already handles; it is not a placeholder for "unknown".
    lua_pushnil(L);
    lua_pushboolean(L, 0);
    lua_pushnil(L);

    return 3;
}

int32_t Script_TogglePVP(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetPVP(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetPVPDesired(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetPVPTimer(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsPVPTimerRunning(lua_State* L) {
    // No PVP flag timer is tracked, so none can be counting down.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_ConfirmBindOnUse(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetPortraitToTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetLocale(lua_State* L) {
    lua_pushstring(L, "enUS");

    return 1;
}

int32_t Script_GetGMTicketCategories(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DropItemOnUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_004dd400
// The whole binding is one console command. Frozen does not register "gxRestart" yet -- the CVar
// callbacks in console/Device.cpp already accumulate the new mode into s_requestedFormat and print
// "set pending gxRestart", but nothing recreates the device -- so this currently reports an unknown
// command, which is also what the reference does for a command that is not registered. Porting the
// binding is separate from building the restart, and this is the binding.
int32_t Script_RestartGx(lua_State* L) {
    ConsoleCommandExecute("gxRestart", 1);

    return 0;
}

int32_t Script_RestoreVideoResolutionDefaults(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RestoreVideoEffectsDefaults(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RestoreVideoStereoDefaults(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetBindLocation(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ConfirmTalentWipe(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ConfirmBinder(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// The player data flags carry two "hide this piece of gear" bits. Both bindings below report the
// inverse -- whether the piece is SHOWN -- so a set bit means hidden.
static const uint32_t PLAYER_FLAGS_HIDE_HELM  = 0x400;   // bit 10
static const uint32_t PLAYER_FLAGS_HIDE_CLOAK = 0x800;   // bit 11

static int32_t PlayerGearShown(lua_State* L, uint32_t hideFlag) {
    auto player = CGPlayer_C::GetActivePtr();
    auto data = player ? player->Player() : nullptr;

    if (data && !(data->flags & hideFlag)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0051bfd0
int32_t Script_ShowingHelm(lua_State* L) {
    return PlayerGearShown(L, PLAYER_FLAGS_HIDE_HELM);
}

// ref: FUN_0051c040
int32_t Script_ShowingCloak(lua_State* L) {
    return PlayerGearShown(L, PLAYER_FLAGS_HIDE_CLOAK);
}

// The server owns these two bits, so the client asks rather than sets: it sends the opcode and the
// flag comes back in the next object update. Nothing is written locally, which is why the matching
// Showing* bindings keep reading the field rather than a cached answer.
//
// TODO the reference does two further things around this. It drops a pending item effect before
// sending when one is up, and it re-runs the character component afterwards so the model changes
// without waiting for the round trip (FUN_00716e20). Neither is ported, so the helm or cloak
// appears or disappears only once the server's update lands.
static int32_t PlayerSetGearShown(lua_State* L, NETMESSAGE opcode, uint32_t hideFlag) {
    auto player = CGPlayer_C::GetActivePtr();
    auto data = player ? player->Player() : nullptr;

    if (!data) {
        return 0;
    }

    auto show = StringToBOOL(L, 1, 1);
    auto hidden = (data->flags & hideFlag) != 0;

    // Each of the reference's two paths is guarded on the current bit, so nothing is sent unless
    // the request actually changes it: show while hidden, or hide while shown.
    if (show != hidden) {
        return 0;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(opcode));
    msg.Put(static_cast<uint8_t>(show ? 1 : 0));
    msg.Finalize();
    ClientServices::Send(&msg);

    return 0;
}

// ref: FUN_0051c0b0
int32_t Script_ShowHelm(lua_State* L) {
    return PlayerSetGearShown(L, CMSG_SHOWING_HELM, PLAYER_FLAGS_HIDE_HELM);
}

// ref: FUN_0051c100
int32_t Script_ShowCloak(lua_State* L) {
    return PlayerSetGearShown(L, CMSG_SHOWING_CLOAK, PLAYER_FLAGS_HIDE_CLOAK);
}

int32_t Script_SetEuropeanNumbers(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetAreaSpiritHealerTime(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AcceptAreaSpiritHeal(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CancelAreaSpiritHeal(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// The frame the cursor is currently over, or nil.
//
// CSimpleTop already tracks this: OnMouseMove walks the mouse event queue by strata and stores the
// first frame whose hit test passes in m_mouseFocus, firing OnEnter/OnLeave off the same value. The
// binding just had no way to read it.
//
// 3.3.5a FrameXML calls this exactly once (VehicleMenuBar.lua), so it is NOT what drives tooltips --
// those come from each frame's own OnEnter script. Cheap to implement correctly, but do not expect
// it to change anything visible.
int32_t Script_GetMouseFocus(lua_State* L) {
    auto top = CSimpleTop::s_instance;
    auto focus = top ? top->m_mouseFocus : nullptr;

    if (!focus) {
        lua_pushnil(L);

        return 1;
    }

    if (!focus->lua_registered) {
        focus->RegisterScriptObject(0);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, focus->lua_objectRef);

    return 1;
}

// ref: FUN_00510e00
int32_t Script_GetRealmName(lua_State* L) {
    lua_pushstring(L, ClientServices::GetSelectedRealmName());

    return 1;
}

int32_t Script_GetItemQualityColor(lua_State* L) {
    // Poor, common, uncommon, rare, epic, legendary, artifact, heirloom
    static const uint32_t s_colors[] = { 0x9D9D9D, 0xFFFFFF, 0x1EFF00, 0x0070DD, 0xA335EE, 0xFF8000, 0xE6CC80, 0xE6CC80 };

    int32_t quality = lua_isnumber(L, 1) ? static_cast<int32_t>(lua_tonumber(L, 1)) : 1;

    if (quality < 0 || quality > 7) {
        quality = 1;
    }

    uint32_t color = s_colors[quality];
    char hex[16];
    SStrPrintf(hex, sizeof(hex), "|cff%06x", color);

    lua_pushnumber(L, ((color >> 16) & 0xFF) / 255.0);
    lua_pushnumber(L, ((color >> 8) & 0xFF) / 255.0);
    lua_pushnumber(L, (color & 0xFF) / 255.0);
    lua_pushstring(L, hex);

    return 4;
}

int32_t Script_GetItemInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetItemGem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetExtendedItemInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00517020
// Despite the name this never touches the item cache: it looks its argument up directly in
// ItemDisplayInfo and returns that record's first inventory icon, so the argument is a DISPLAY id
// rather than an item entry. Returns no values at all when the id is not in the table, which is
// what the reference does.
//
// DIVERGENCE: the reference passes the icon name through FUN_0070a910 before formatting it. That is
// not a string transform -- it is a resolution cache, keyed on the name, that checks whether the
// file exists and remembers the answer. Frozen has no such cache, so the name goes through as the
// table spells it, which is the same result whenever the file is present under that name.
//
// TODO the string form of the argument goes through the reference's name-or-link resolver
// (FUN_00709de0), which needs the item cache to turn a name into an id. Numbers only for now.
int32_t Script_GetItemIcon(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        return 0;
    }

    auto displayID = static_cast<int32_t>(lua_tonumber(L, 1) + 0.5);
    auto rec = g_itemDisplayInfoDB.GetRecord(displayID);

    if (!rec || !rec->m_inventoryIcon[0] || !rec->m_inventoryIcon[0][0]) {
        return 0;
    }

    char icon[260];
    SStrPrintf(icon, sizeof(icon), "%s%s%s", ICON_DIRECTORY, *ICON_DIRECTORY ? "\\" : "", rec->m_inventoryIcon[0]);

    lua_pushstring(L, icon);

    return 1;
}

int32_t Script_GetItemFamily(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetItemCount(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetItemSpell(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetItemCooldown(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_PickupItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsCurrentItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsUsableItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsHelpfulItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsHarmfulItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsConsumableItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsEquippableItem(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsEquippedItem(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsEquippedItemType(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsDressableItem(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_ItemHasRange(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsItemInRange(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetNumAddOns(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetAddOnInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetAddOnMetadata(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UpdateAddOnMemoryUsage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetAddOnMemoryUsage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetScriptCPUUsage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UpdateAddOnCPUUsage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetAddOnCPUUsage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetFunctionCPUUsage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetFrameCPUUsage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetEventCPUUsage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ResetCPUUsage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetAddOnDependencies(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_EnableAddOn(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_EnableAllAddOns(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DisableAddOn(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DisableAllAddOns(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ResetDisabledAddOns(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsAddOnLoadOnDemand(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsAddOnLoaded(lua_State* L) {
    const char* name = lua_tolstring(L, 1, nullptr);

    if (AddOnIsLoaded(name)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_LoadAddOn(lua_State* L) {
    const char* name = lua_tolstring(L, 1, nullptr);
    const char* reason = nullptr;

    if (AddOnLoad(name, &reason)) {
        lua_pushnumber(L, 1.0);

        return 1;
    }

    lua_pushnil(L);
    lua_pushstring(L, reason ? reason : "MISSING");

    return 2;
}

int32_t Script_PartialPlayTime(lua_State* L) {
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_NoPlayTime(lua_State* L) {
    // The play-time limit is a regional restriction the server never sends, so it can never be on.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GetBillingTimeRested(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_CanShowResetInstances(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ResetInstances(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsInInstance(lua_State* L) {
    lua_pushnil(L);
    lua_pushstring(L, "none");

    return 2;
}

int32_t Script_GetInstanceDifficulty(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetInstanceInfo(lua_State* L) {
    // name, instanceType, difficultyIndex, difficultyName, maxPlayers, dynamicDifficulty, isDynamic
    //
    // FrameXML compares instanceType against the string "none" for the open world, so that has to
    // be a real string rather than nil -- the raid and difficulty UI branches on it.
    lua_pushnil(L);
    lua_pushstring(L, "none");
    lua_pushnumber(L, 0.0);
    lua_pushstring(L, "");
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushboolean(L, 0);

    return 7;
}

int32_t Script_GetDungeonDifficulty(lua_State* L) {
    // 1 is normal. There is no difficulty system to change it, so normal is not a default here --
    // it is the only state this client can be in.
    lua_pushnumber(L, 1.0);

    return 1;
}

int32_t Script_SetDungeonDifficulty(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetRaidDifficulty(lua_State* L) {
    lua_pushnumber(L, 1.0);

    return 1;
}

int32_t Script_SetRaidDifficulty(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ReportBug(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ReportSuggestion(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetMirrorTimerInfo(lua_State* L) {
    // timer, value, maxvalue, scale, paused, label
    lua_pushstring(L, "UNKNOWN");
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushstring(L, "");

    return 6;
}

int32_t Script_GetMirrorTimerProgress(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetNumTitles(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetCurrentTitle(lua_State* L) {
    // The equipped title's id. -1 is the reference's "no title selected", and is what the title
    // dropdown compares against; 0 would select the first real title instead.
    lua_pushnumber(L, -1.0);

    return 1;
}

int32_t Script_SetCurrentTitle(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsTitleKnown(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GetTitleName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UseItemByName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_EquipItemByName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetExistingLocales(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_InCombatLockdown(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_StartAttack(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_StopAttack(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetTaxiBenchmarkMode(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetTaxiBenchmarkMode(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_Dismount(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_VoicePushToTalkStart(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_VoicePushToTalkStop(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetUIVisibility(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsReferAFriendLinked(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_CanGrantLevel(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GrantLevel(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CanSummonFriend(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SummonFriend(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetSummonFriendCooldown(lua_State* L) {
    // Returns start, duration. The interface does `start + duration - GetTime()` on the result
    // without checking it, so returning nothing made every unit dropdown throw as it was built --
    // twelve of them per run. A cooldown that has never been used is (0, 0), which is what the
    // reference reports too until the spell is cast.
    // TODO the real values once the RAF summon cooldown is tracked
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 2;
}

int32_t Script_GetTotemInfo(lua_State* L) {
    // 5 values, typed from what the caller destructures them into:
    //   haveTotem, name, startTime, duration, icon
    // The data behind this is not available yet, so each position takes the neutral
    // value for its type -- 0 where the caller does arithmetic, false where it
    // branches, nil where it expects a name or a texture and already handles absence.
    lua_pushboolean(L, 0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnil(L);

    return 5;
}

int32_t Script_GetTotemTimeLeft(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_TargetTotem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DestroyTotem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetNumDeclensionSets(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DeclineName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AcceptLevelGrant(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DeclineLevelGrant(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UploadSettings(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DownloadSettings(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetMovieResolution(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GameMovieFinished(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsDesaturateSupported(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0061a4f0
// Five colours, read out of the reference's table at 00ad2d70: white, grey, yellow, orange, red.
// The clamp is on an UNSIGNED status, so a negative one -- which is what the server sends for a
// unit that is not on the threat table -- wraps high and lands on grey rather than on white.
static const CImVector& ThreatStatusColor(uint32_t status) {
    static const CImVector s_threatColors[5] = {
        { 0xFF, 0xFF, 0xFF, 0xFF },
        { 0xB0, 0xB0, 0xB0, 0xFF },
        { 0x77, 0xFF, 0xFF, 0xFF },
        { 0x00, 0x99, 0xFF, 0xFF },
        { 0x00, 0x00, 0xFF, 0xFF },
    };

    if (status > 4) {
        status = 1;
    }

    return s_threatColors[status];
}

// ref: FUN_00511fe0
int32_t Script_GetThreatStatusColor(lua_State* L) {
    // Rounded to an int first, then taken as unsigned, so the negative case reaches the clamp
    // above exactly as it does in the reference.
    auto status = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 1) + 0.5));

    const auto& color = ThreatStatusColor(status);

    lua_pushnumber(L, color.r * (1.0f / 255.0f));
    lua_pushnumber(L, color.g * (1.0f / 255.0f));
    lua_pushnumber(L, color.b * (1.0f / 255.0f));

    return 3;
}

int32_t Script_IsThreatWarningEnabled(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_ConsoleAddMessage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetItemUniqueness(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_EndRefund(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_EndBoundTradeable(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CanMapChangeDifficulty(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetExpansionLevel(lua_State* L) {
    // This client is 3.3.5a, which IS Wrath: 0 vanilla, 1 Burning Crusade, 2 Wrath.
    lua_pushnumber(L, 2.0);

    return 1;
}

int32_t Script_GetAllowLowLevelRaid(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetAllowLowLevelRaid(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "FrameXML_Debug",                 &Script_FrameXML_Debug },
    { "GetBuildInfo",                   &Script_GetBuildInfo },
    { "ReloadUI",                       &Script_ReloadUI },
    { "RegisterForSave",                &Script_RegisterForSave },
    { "RegisterForSavePerCharacter",    &Script_RegisterForSavePerCharacter },
    { "SetLayoutMode",                  &Script_SetLayoutMode },
    { "IsModifierKeyDown",              &Script_IsModifierKeyDown },
    { "IsLeftShiftKeyDown",             &Script_IsLeftShiftKeyDown },
    { "IsRightShiftKeyDown",            &Script_IsRightShiftKeyDown },
    { "IsShiftKeyDown",                 &Script_IsShiftKeyDown },
    { "IsLeftControlKeyDown",           &Script_IsLeftControlKeyDown },
    { "IsRightControlKeyDown",          &Script_IsRightControlKeyDown },
    { "IsControlKeyDown",               &Script_IsControlKeyDown },
    { "IsLeftAltKeyDown",               &Script_IsLeftAltKeyDown },
    { "IsRightAltKeyDown",              &Script_IsRightAltKeyDown },
    { "IsAltKeyDown",                   &Script_IsAltKeyDown },
    { "IsMouseButtonDown",              &Script_IsMouseButtonDown },
    { "GetMouseButtonName",             &Script_GetMouseButtonName },
    { "GetMouseButtonClicked",          &Script_GetMouseButtonClicked },
    { "SetConsoleKey",                  &Script_SetConsoleKey },
    { "Screenshot",                     &Script_Screenshot },
    { "GetFramerate",                   &Script_GetFramerate },
    { "TogglePerformanceDisplay",       &Script_TogglePerformanceDisplay },
    { "TogglePerformancePause",         &Script_TogglePerformancePause },
    { "TogglePerformanceValues",        &Script_TogglePerformanceValues },
    { "ResetPerformanceValues",         &Script_ResetPerformanceValues },
    { "GetDebugStats",                  &Script_GetDebugStats },
    { "IsDebugBuild",                   &Script_IsDebugBuild },
    { "RegisterCVar",                   &Script_RegisterCVar },
    { "GetCVarInfo",                    &Script_GetCVarInfo },
    { "SetCVar",                        &Script_SetCVar },
    { "GetCVar",                        &Script_GetCVar },
    { "GetCVarBool",                    &Script_GetCVarBool },
    { "GetCVarDefault",                 &Script_GetCVarDefault },
    { "GetCVarMin",                     &Script_GetCVarMin },
    { "GetCVarMax",                     &Script_GetCVarMax },
    { "GetCVarAbsoluteMin",             &Script_GetCVarAbsoluteMin },
    { "GetCVarAbsoluteMax",             &Script_GetCVarAbsoluteMax },
    { "GetWaterDetail",                 &Script_GetWaterDetail },
    { "SetWaterDetail",                 &Script_SetWaterDetail },
    { "GetFarclip",                     &Script_GetFarclip },
    { "SetFarclip",                     &Script_SetFarclip },
    { "GetTexLodBias",                  &Script_GetTexLodBias },
    { "SetTexLodBias",                  &Script_SetTexLodBias },
    { "SetBaseMip",                     &Script_SetBaseMip },
    { "GetBaseMip",                     &Script_GetBaseMip },
    { "ToggleTris",                     &Script_ToggleTris },
    { "TogglePortals",                  &Script_TogglePortals },
    { "ToggleCollision",                &Script_ToggleCollision },
    { "ToggleCollisionDisplay",         &Script_ToggleCollisionDisplay },
    { "TogglePlayerBounds",             &Script_TogglePlayerBounds },
    { "Stuck",                          &Script_Stuck },
    { "Logout",                         &Script_Logout },
    { "Quit",                           &Script_Quit },
    { "SetCursor",                      &Script_SetCursor },
    { "ResetCursor",                    &Script_ResetCursor },
    { "ClearCursor",                    &Script_ClearCursor },
    { "CursorHasItem",                  &Script_CursorHasItem },
    { "CursorHasSpell",                 &Script_CursorHasSpell },
    { "CursorHasMacro",                 &Script_CursorHasMacro },
    { "CursorHasMoney",                 &Script_CursorHasMoney },
    { "GetCursorInfo",                  &Script_GetCursorInfo },
    { "EquipCursorItem",                &Script_EquipCursorItem },
    { "DeleteCursorItem",               &Script_DeleteCursorItem },
    { "EquipPendingItem",               &Script_EquipPendingItem },
    { "CancelPendingEquip",             &Script_CancelPendingEquip },
    { "TargetUnit",                     &Script_TargetUnit },
    { "TargetNearest",                  &Script_TargetNearest },
    { "TargetNearestEnemy",             &Script_TargetNearestEnemy },
    { "TargetNearestEnemyPlayer",       &Script_TargetNearestEnemyPlayer },
    { "TargetNearestFriend",            &Script_TargetNearestFriend },
    { "TargetNearestFriendPlayer",      &Script_TargetNearestFriendPlayer },
    { "TargetNearestPartyMember",       &Script_TargetNearestPartyMember },
    { "TargetNearestRaidMember",        &Script_TargetNearestRaidMember },
    { "TargetDirectionEnemy",           &Script_TargetDirectionEnemy },
    { "TargetDirectionFriend",          &Script_TargetDirectionFriend },
    { "TargetDirectionFinished",        &Script_TargetDirectionFinished },
    { "TargetLastTarget",               &Script_TargetLastTarget },
    { "TargetLastEnemy",                &Script_TargetLastEnemy },
    { "TargetLastFriend",               &Script_TargetLastFriend },
    { "AttackTarget",                   &Script_AttackTarget },
    { "AssistUnit",                     &Script_AssistUnit },
    { "FocusUnit",                      &Script_FocusUnit },
    { "FollowUnit",                     &Script_FollowUnit },
    { "InteractUnit",                   &Script_InteractUnit },
    { "ClearTarget",                    &Script_ClearTarget },
    { "ClearFocus",                     &Script_ClearFocus },
    { "AutoEquipCursorItem",            &Script_AutoEquipCursorItem },
    { "ToggleSheath",                   &Script_ToggleSheath },
    { "GetZoneText",                    &Script_GetZoneText },
    { "GetRealZoneText",                &Script_GetRealZoneText },
    { "GetSubZoneText",                 &Script_GetSubZoneText },
    { "GetMinimapZoneText",             &Script_GetMinimapZoneText },
    { "InitiateTrade",                  &Script_InitiateTrade },
    { "CanInspect",                     &Script_CanInspect },
    { "NotifyInspect",                  &Script_NotifyInspect },
    { "InviteUnit",                     &Script_InviteUnit },
    { "UninviteUnit",                   &Script_UninviteUnit },
    { "RequestTimePlayed",              &Script_RequestTimePlayed },
    { "RepopMe",                        &Script_RepopMe },
    { "AcceptResurrect",                &Script_AcceptResurrect },
    { "DeclineResurrect",               &Script_DeclineResurrect },
    { "ResurrectGetOfferer",            &Script_ResurrectGetOfferer },
    { "ResurrectHasSickness",           &Script_ResurrectHasSickness },
    { "ResurrectHasTimer",              &Script_ResurrectHasTimer },
    { "BeginTrade",                     &Script_BeginTrade },
    { "CancelTrade",                    &Script_CancelTrade },
    { "AcceptGroup",                    &Script_AcceptGroup },
    { "DeclineGroup",                   &Script_DeclineGroup },
    { "AcceptGuild",                    &Script_AcceptGuild },
    { "DeclineGuild",                   &Script_DeclineGuild },
    { "AcceptArenaTeam",                &Script_AcceptArenaTeam },
    { "DeclineArenaTeam",               &Script_DeclineArenaTeam },
    { "CancelLogout",                   &Script_CancelLogout },
    { "ForceLogout",                    &Script_ForceLogout },
    { "ForceQuit",                      &Script_ForceQuit },
    { "GetCursorMoney",                 &Script_GetCursorMoney },
    { "DropCursorMoney",                &Script_DropCursorMoney },
    { "PickupPlayerMoney",              &Script_PickupPlayerMoney },
    { "HasSoulstone",                   &Script_HasSoulstone },
    { "UseSoulstone",                   &Script_UseSoulstone },
    { "HasKey",                         &Script_HasKey },
    { "GuildInvite",                    &Script_GuildInvite },
    { "GuildUninvite",                  &Script_GuildUninvite },
    { "GuildPromote",                   &Script_GuildPromote },
    { "GuildDemote",                    &Script_GuildDemote },
    { "GuildSetLeader",                 &Script_GuildSetLeader },
    { "GuildSetMOTD",                   &Script_GuildSetMOTD },
    { "GuildLeave",                     &Script_GuildLeave },
    { "GuildDisband",                   &Script_GuildDisband },
    { "GuildInfo",                      &Script_GuildInfo },
    { "ArenaTeamInviteByName",          &Script_ArenaTeamInviteByName },
    { "ArenaTeamLeave",                 &Script_ArenaTeamLeave },
    { "ArenaTeamUninviteByName",        &Script_ArenaTeamUninviteByName },
    { "ArenaTeamSetLeaderByName",       &Script_ArenaTeamSetLeaderByName },
    { "ArenaTeamDisband",               &Script_ArenaTeamDisband },
    { "GetScreenWidth",                 &Script_GetScreenWidth },
    { "GetScreenHeight",                &Script_GetScreenHeight },
    { "GetDamageBonusStat",             &Script_GetDamageBonusStat },
    { "GetReleaseTimeRemaining",        &Script_GetReleaseTimeRemaining },
    { "GetCorpseRecoveryDelay",         &Script_GetCorpseRecoveryDelay },
    { "GetInstanceBootTimeRemaining",   &Script_GetInstanceBootTimeRemaining },
    { "GetInstanceLockTimeRemaining",   &Script_GetInstanceLockTimeRemaining },
    { "GetInstanceLockTimeRemainingEncounter", &Script_GetInstanceLockTimeRemainingEncounter },
    { "GetSummonConfirmTimeLeft",       &Script_GetSummonConfirmTimeLeft },
    { "GetSummonConfirmSummoner",       &Script_GetSummonConfirmSummoner },
    { "GetSummonConfirmAreaName",       &Script_GetSummonConfirmAreaName },
    { "ConfirmSummon",                  &Script_ConfirmSummon },
    { "CancelSummon",                   &Script_CancelSummon },
    { "GetCursorPosition",              &Script_GetCursorPosition },
    { "GetNetStats",                    &Script_GetNetStats },
    { "SitStandOrDescendStart",         &Script_SitStandOrDescendStart },
    { "StopCinematic",                  &Script_StopCinematic },
    { "RunScript",                      &Script_RunScript },
    { "CheckInteractDistance",          &Script_CheckInteractDistance },
    { "RandomRoll",                     &Script_RandomRoll },
    { "OpeningCinematic",               &Script_OpeningCinematic },
    { "InCinematic",                    &Script_InCinematic },
    { "IsWindowsClient",                &Script_IsWindowsClient },
    { "IsMacClient",                    &Script_IsMacClient },
    { "IsLinuxClient",                  &Script_IsLinuxClient },
    { "AcceptXPLoss",                   &Script_AcceptXPLoss },
    { "CheckSpiritHealerDist",          &Script_CheckSpiritHealerDist },
    { "CheckTalentMasterDist",          &Script_CheckTalentMasterDist },
    { "CheckBinderDist",                &Script_CheckBinderDist },
    { "RetrieveCorpse",                 &Script_RetrieveCorpse },
    { "BindEnchant",                    &Script_BindEnchant },
    { "ReplaceEnchant",                 &Script_ReplaceEnchant },
    { "ReplaceTradeEnchant",            &Script_ReplaceTradeEnchant },
    { "NotWhileDeadError",              &Script_NotWhileDeadError },
    { "GetRestState",                   &Script_GetRestState },
    { "GetXPExhaustion",                &Script_GetXPExhaustion },
    { "GetTimeToWellRested",            &Script_GetTimeToWellRested },
    { "GMRequestPlayerInfo",            &Script_GMRequestPlayerInfo },
    { "GetCoinIcon",                    &Script_GetCoinIcon },
    { "GetCoinText",                    &Script_GetCoinText },
    { "GetCoinTextureString",           &Script_GetCoinTextureString },
    { "IsSubZonePVPPOI",                &Script_IsSubZonePVPPOI },
    { "GetZonePVPInfo",                 &Script_GetZonePVPInfo },
    { "TogglePVP",                      &Script_TogglePVP },
    { "SetPVP",                         &Script_SetPVP },
    { "GetPVPDesired",                  &Script_GetPVPDesired },
    { "GetPVPTimer",                    &Script_GetPVPTimer },
    { "IsPVPTimerRunning",              &Script_IsPVPTimerRunning },
    { "ConfirmBindOnUse",               &Script_ConfirmBindOnUse },
    { "SetPortraitToTexture",           &Script_SetPortraitToTexture },
    { "GetLocale",                      &Script_GetLocale },
    { "GetGMTicketCategories",          &Script_GetGMTicketCategories },
    { "DropItemOnUnit",                 &Script_DropItemOnUnit },
    { "RestartGx",                      &Script_RestartGx },
    { "RestoreVideoResolutionDefaults", &Script_RestoreVideoResolutionDefaults },
    { "RestoreVideoEffectsDefaults",    &Script_RestoreVideoEffectsDefaults },
    { "RestoreVideoStereoDefaults",     &Script_RestoreVideoStereoDefaults },
    { "GetBindLocation",                &Script_GetBindLocation },
    { "ConfirmTalentWipe",              &Script_ConfirmTalentWipe },
    { "ConfirmBinder",                  &Script_ConfirmBinder },
    { "ShowingHelm",                    &Script_ShowingHelm },
    { "ShowingCloak",                   &Script_ShowingCloak },
    { "ShowHelm",                       &Script_ShowHelm },
    { "ShowCloak",                      &Script_ShowCloak },
    { "SetEuropeanNumbers",             &Script_SetEuropeanNumbers },
    { "GetAreaSpiritHealerTime",        &Script_GetAreaSpiritHealerTime },
    { "AcceptAreaSpiritHeal",           &Script_AcceptAreaSpiritHeal },
    { "CancelAreaSpiritHeal",           &Script_CancelAreaSpiritHeal },
    { "GetMouseFocus",                  &Script_GetMouseFocus },
    { "GetRealmName",                   &Script_GetRealmName },
    { "GetItemQualityColor",            &Script_GetItemQualityColor },
    { "GetItemInfo",                    &Script_GetItemInfo },
    { "GetItemGem",                     &Script_GetItemGem },
    { "GetExtendedItemInfo",            &Script_GetExtendedItemInfo },
    { "GetItemIcon",                    &Script_GetItemIcon },
    { "GetItemFamily",                  &Script_GetItemFamily },
    { "GetItemCount",                   &Script_GetItemCount },
    { "GetItemSpell",                   &Script_GetItemSpell },
    { "GetItemCooldown",                &Script_GetItemCooldown },
    { "PickupItem",                     &Script_PickupItem },
    { "IsCurrentItem",                  &Script_IsCurrentItem },
    { "IsUsableItem",                   &Script_IsUsableItem },
    { "IsHelpfulItem",                  &Script_IsHelpfulItem },
    { "IsHarmfulItem",                  &Script_IsHarmfulItem },
    { "IsConsumableItem",               &Script_IsConsumableItem },
    { "IsEquippableItem",               &Script_IsEquippableItem },
    { "IsEquippedItem",                 &Script_IsEquippedItem },
    { "IsEquippedItemType",             &Script_IsEquippedItemType },
    { "IsDressableItem",                &Script_IsDressableItem },
    { "ItemHasRange",                   &Script_ItemHasRange },
    { "IsItemInRange",                  &Script_IsItemInRange },
    { "GetNumAddOns",                   &Script_GetNumAddOns },
    { "GetAddOnInfo",                   &Script_GetAddOnInfo },
    { "GetAddOnMetadata",               &Script_GetAddOnMetadata },
    { "UpdateAddOnMemoryUsage",         &Script_UpdateAddOnMemoryUsage },
    { "GetAddOnMemoryUsage",            &Script_GetAddOnMemoryUsage },
    { "GetScriptCPUUsage",              &Script_GetScriptCPUUsage },
    { "UpdateAddOnCPUUsage",            &Script_UpdateAddOnCPUUsage },
    { "GetAddOnCPUUsage",               &Script_GetAddOnCPUUsage },
    { "GetFunctionCPUUsage",            &Script_GetFunctionCPUUsage },
    { "GetFrameCPUUsage",               &Script_GetFrameCPUUsage },
    { "GetEventCPUUsage",               &Script_GetEventCPUUsage },
    { "ResetCPUUsage",                  &Script_ResetCPUUsage },
    { "GetAddOnDependencies",           &Script_GetAddOnDependencies },
    { "EnableAddOn",                    &Script_EnableAddOn },
    { "EnableAllAddOns",                &Script_EnableAllAddOns },
    { "DisableAddOn",                   &Script_DisableAddOn },
    { "DisableAllAddOns",               &Script_DisableAllAddOns },
    { "ResetDisabledAddOns",            &Script_ResetDisabledAddOns },
    { "IsAddOnLoadOnDemand",            &Script_IsAddOnLoadOnDemand },
    { "IsAddOnLoaded",                  &Script_IsAddOnLoaded },
    { "LoadAddOn",                      &Script_LoadAddOn },
    { "PartialPlayTime",                &Script_PartialPlayTime },
    { "NoPlayTime",                     &Script_NoPlayTime },
    { "GetBillingTimeRested",           &Script_GetBillingTimeRested },
    { "CanShowResetInstances",          &Script_CanShowResetInstances },
    { "ResetInstances",                 &Script_ResetInstances },
    { "IsInInstance",                   &Script_IsInInstance },
    { "GetInstanceDifficulty",          &Script_GetInstanceDifficulty },
    { "GetInstanceInfo",                &Script_GetInstanceInfo },
    { "GetDungeonDifficulty",           &Script_GetDungeonDifficulty },
    { "SetDungeonDifficulty",           &Script_SetDungeonDifficulty },
    { "GetRaidDifficulty",              &Script_GetRaidDifficulty },
    { "SetRaidDifficulty",              &Script_SetRaidDifficulty },
    { "ReportBug",                      &Script_ReportBug },
    { "ReportSuggestion",               &Script_ReportSuggestion },
    { "GetMirrorTimerInfo",             &Script_GetMirrorTimerInfo },
    { "GetMirrorTimerProgress",         &Script_GetMirrorTimerProgress },
    { "GetNumTitles",                   &Script_GetNumTitles },
    { "GetCurrentTitle",                &Script_GetCurrentTitle },
    { "SetCurrentTitle",                &Script_SetCurrentTitle },
    { "IsTitleKnown",                   &Script_IsTitleKnown },
    { "GetTitleName",                   &Script_GetTitleName },
    { "UseItemByName",                  &Script_UseItemByName },
    { "EquipItemByName",                &Script_EquipItemByName },
    { "GetExistingLocales",             &Script_GetExistingLocales },
    { "InCombatLockdown",               &Script_InCombatLockdown },
    { "StartAttack",                    &Script_StartAttack },
    { "StopAttack",                     &Script_StopAttack },
    { "SetTaxiBenchmarkMode",           &Script_SetTaxiBenchmarkMode },
    { "GetTaxiBenchmarkMode",           &Script_GetTaxiBenchmarkMode },
    { "Dismount",                       &Script_Dismount },
    { "VoicePushToTalkStart",           &Script_VoicePushToTalkStart },
    { "VoicePushToTalkStop",            &Script_VoicePushToTalkStop },
    { "SetUIVisibility",                &Script_SetUIVisibility },
    { "IsReferAFriendLinked",           &Script_IsReferAFriendLinked },
    { "CanGrantLevel",                  &Script_CanGrantLevel },
    { "GrantLevel",                     &Script_GrantLevel },
    { "CanSummonFriend",                &Script_CanSummonFriend },
    { "SummonFriend",                   &Script_SummonFriend },
    { "GetSummonFriendCooldown",        &Script_GetSummonFriendCooldown },
    { "GetTotemInfo",                   &Script_GetTotemInfo },
    { "GetTotemTimeLeft",               &Script_GetTotemTimeLeft },
    { "TargetTotem",                    &Script_TargetTotem },
    { "DestroyTotem",                   &Script_DestroyTotem },
    { "GetNumDeclensionSets",           &Script_GetNumDeclensionSets },
    { "DeclineName",                    &Script_DeclineName },
    { "AcceptLevelGrant",               &Script_AcceptLevelGrant },
    { "DeclineLevelGrant",              &Script_DeclineLevelGrant },
    { "UploadSettings",                 &Script_UploadSettings },
    { "DownloadSettings",               &Script_DownloadSettings },
    { "GetMovieResolution",             &Script_GetMovieResolution },
    { "GameMovieFinished",              &Script_GameMovieFinished },
    { "IsDesaturateSupported",          &Script_IsDesaturateSupported },
    { "GetThreatStatusColor",           &Script_GetThreatStatusColor },
    { "IsThreatWarningEnabled",         &Script_IsThreatWarningEnabled },
    { "ConsoleAddMessage",              &Script_ConsoleAddMessage },
    { "GetItemUniqueness",              &Script_GetItemUniqueness },
    { "EndRefund",                      &Script_EndRefund },
    { "EndBoundTradeable",              &Script_EndBoundTradeable },
    { "CanMapChangeDifficulty",         &Script_CanMapChangeDifficulty },
    { "GetExpansionLevel",              &Script_GetExpansionLevel },
    { "GetAllowLowLevelRaid",           &Script_GetAllowLowLevelRaid },
    { "SetAllowLowLevelRaid",           &Script_SetAllowLowLevelRaid },
};

void GameScriptRegisterFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
