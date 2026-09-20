#include "object/client/NameCache.hpp"
#include "object/client/ObjMgr.hpp"
#include "glue/CharacterSelectionDisplay.hpp"
#include "glue/CCharacterSelection.hpp"
#include "ui/game/ScriptEvents.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "ui/game/CGRaidInfo.hpp"
#include <cmath>
#include "object/client/CGPlayer_C.hpp"
#include <storm/String.hpp>
#include "object/client/CGUnit_C.hpp"
#include "object/client/CGItem_C.hpp"
#include "db/Db.hpp"
#include "object/Client.hpp"
#include "ui/FrameScript.hpp"
#include "ui/ScriptFunctionsSystem.hpp"
#include "ui/Util.hpp"
#include "db/Db.hpp"
#include "object/client/AuraCache.hpp"
#include "object/client/CastCache.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "ui/game/Types.hpp"
#include "util/GUID.hpp"
#include <cstddef>
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"

#define NUM_SCRIPT_EVENTS 722

const char* g_scriptEvents[NUM_SCRIPT_EVENTS];

namespace {

// The character sheet's combat percentages. Each reference function is the same ninety-five bytes:
// resolve the active player, push one field, and push 0.0 rather than nil when there is no player.
//
// The reference addresses these fields by absolute offset into CGPlayerData. The static_asserts
// below hold frozen's struct to the same layout, so a drift shows up as a build failure instead of
// the character sheet quietly showing dodge where it means parry.
static_assert(offsetof(CGPlayerData, blockPercentage) == 0xdb0, "CGPlayerData layout");
static_assert(offsetof(CGPlayerData, dodgePercentage) == 0xdb4, "CGPlayerData layout");
static_assert(offsetof(CGPlayerData, parryPercentage) == 0xdb8, "CGPlayerData layout");
static_assert(offsetof(CGPlayerData, critPercentage) == 0xdc4, "CGPlayerData layout");
static_assert(offsetof(CGPlayerData, rangedCritPercentage) == 0xdc8, "CGPlayerData layout");
static_assert(offsetof(CGPlayerData, modHealingDonePos) == 0x1050, "CGPlayerData layout");
static_assert(offsetof(CGPlayerData, expertise) == 0xdbc, "CGPlayerData layout");
static_assert(offsetof(CGPlayerData, offhandExpertise) == 0xdc0, "CGPlayerData layout");
static_assert(offsetof(CGPlayerData, spellCritPercentage) == 0xdd0, "CGPlayerData layout");

// The same guard for CGUnitData, which the unit stat bindings address the same way. These were
// read off the struct when those bindings landed and then taken on trust; asserting them is what
// makes the reading durable. Each array's offset is also the previous one plus its own width,
// which is the property that identified them in the first place.
static_assert(offsetof(CGUnitData, flags) == 0xd4, "CGUnitData layout");
static_assert(offsetof(CGUnitData, dynamicFlags) == 0x124, "CGUnitData layout");
static_assert(offsetof(CGUnitData, pad3) == 0x1d0, "CGUnitData layout");
static_assert(offsetof(CGUnitData, attackRoundBaseTime) == 0xe0, "CGUnitData layout");
static_assert(offsetof(CGUnitData, minDamage) == 0x100, "CGUnitData layout");
static_assert(offsetof(CGUnitData, maxDamage) == 0x104, "CGUnitData layout");
static_assert(offsetof(CGUnitData, minOffhandDamage) == 0x108, "CGUnitData layout");
static_assert(offsetof(CGUnitData, maxOffhandDamage) == 0x10c, "CGUnitData layout");
static_assert(offsetof(CGUnitData, rangedAttackTime) == 0xe8, "CGUnitData layout");
static_assert(offsetof(CGUnitData, minRangedDamage) == 0x1ec, "CGUnitData layout");
static_assert(offsetof(CGUnitData, maxRangedDamage) == 0x1f0, "CGUnitData layout");
static_assert(offsetof(CGUnitData, stats) == 0x138, "CGUnitData layout");
static_assert(offsetof(CGUnitData, posStats) == 0x14c, "CGUnitData layout");
static_assert(offsetof(CGUnitData, negStats) == 0x160, "CGUnitData layout");
static_assert(offsetof(CGUnitData, resistance) == 0x174, "CGUnitData layout");
static_assert(offsetof(CGUnitData, resistanceBuffModsPositive) == 0x190, "CGUnitData layout");
static_assert(offsetof(CGUnitData, resistanceBuffModsNegative) == 0x1ac, "CGUnitData layout");
static_assert(offsetof(CGUnitData, attackPower) == 0x1d4, "CGUnitData layout");
static_assert(offsetof(CGUnitData, attackPowerMods) == 0x1d8, "CGUnitData layout");
static_assert(offsetof(CGUnitData, attackPowerMultiplier) == 0x1dc, "CGUnitData layout");
static_assert(offsetof(CGUnitData, rangedAttackPower) == 0x1e0, "CGUnitData layout");
static_assert(offsetof(CGUnitData, rangedAttackPowerMods) == 0x1e4, "CGUnitData layout");
static_assert(offsetof(CGUnitData, rangedAttackPowerMultiplier) == 0x1e8, "CGUnitData layout");
static_assert(offsetof(CGPlayerData, modTargetResistance) == 0x105c, "CGPlayerData layout");


// The player's own data, or null. Every function below answers 0.0 without it, which is what the
// reference pushes -- the character sheet shows a zero rather than going blank.
static CGPlayerData* ActivePlayerData() {
    auto player = CGPlayer_C::GetActivePtr();

    return player ? player->Player() : nullptr;
}


int32_t Script_UnitExists(lua_State* L) {
    auto token = lua_tostring(L, 1);
    WOWGUID guid = 0;
    Script_GetGUIDFromToken(token, guid, false);

    auto object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, __FILE__, __LINE__);

    if ((object && object->CanBeTargetted()) || CGGameUI::IsRaidMemberOrPet(guid)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsVisible(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsVisible(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (unit != nullptr) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsUnit(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        luaL_error(L, "Usage: UnitIsUnit(\"unit\", \"otherUnit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto other = Script_GetUnitFromName(lua_tostring(L, 2));

    if (unit && unit == other) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsPlayer(lua_State* L) {
    auto token = lua_tostring(L, 1);
    WOWGUID guid = 0;
    Script_GetGUIDFromToken(token, guid, false);

    auto object = ClntObjMgrObjectPtr(guid, TYPE_PLAYER, __FILE__, __LINE__);

    if (object || CGGameUI::IsRaidMember(guid)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsInMyGuild(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsInMyGuild(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));

    // No guild data is received, so the client cannot know of any shared guild.
    (void)unit;

    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_UnitIsCorpse(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsCorpse(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsPartyLeader(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsPartyLeader(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitGroupRolesAssigned(lua_State* L) {
    // Three booleans, not the single role string the later API uses: both call sites in this
    // FrameXML destructure `local isTank, isHealer, isDamage = UnitGroupRolesAssigned(unit)`
    // (PartyMemberFrame.lua:223, PlayerFrame.lua:250). Roles are assigned through the LFG system,
    // which does not exist here, so nobody holds one.
    lua_pushboolean(L, 0);
    lua_pushboolean(L, 0);
    lua_pushboolean(L, 0);

    return 3;
}

int32_t Script_UnitIsRaidOfficer(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsRaidOfficer(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitInParty(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitInParty(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0060c9a0
// Whether the named unit is in your party, counting party members' pets. It does NOT count you or
// your own pet -- the name is about what KIND of unit qualifies, not about including the player.
//
// No usage check: the reference reads the argument straight through lua_tostring, so a non-string
// resolves to nothing and answers nil.
int32_t Script_UnitPlayerOrPetInParty(lua_State* L) {
    auto token = lua_tostring(L, 1);

    WOWGUID guid = 0;

    if (token) {
        Script_GetGUIDFromToken(token, guid, false);
    }

    if (CGPartyInfo::IsMemberOrPet(guid)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitInRaid(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitInRaid(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0060cb20
// The raid counterpart of UnitPlayerOrPetInParty, and the same shape: no usage check, and the
// player is not on the roster because the packet does not list them.
int32_t Script_UnitPlayerOrPetInRaid(lua_State* L) {
    auto token = lua_tostring(L, 1);

    WOWGUID guid = 0;

    if (token) {
        Script_GetGUIDFromToken(token, guid, false);
    }

    if (CGRaidInfo::IsMemberOrPet(guid)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitPlayerControlled(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitPlayerControlled(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (unit && unit->IsA(TYPE_PLAYER)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsAFK(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsAFK(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsDND(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsDND(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsPVP(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsPVP(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (data && (data->flags & 0x1000)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0060cf20
// Bit 3 of the SECOND byte of pad3, which is the unit's bytes-2 field -- the same dword that
// carries the sheath state and shapeshift form. The reference reads it as a byte at +0x1d1, so
// the shift is part of the field's meaning rather than an encoding detail.
//
// No usage check, unlike its neighbours: the reference goes straight to lua_tostring, so a
// non-string argument simply fails to resolve and answers nil.
int32_t Script_UnitIsPVPSanctuary(lua_State* L) {
    auto token = lua_tostring(L, 1);
    auto unit = token ? Script_GetUnitFromName(token) : nullptr;
    auto data = unit ? unit->Unit() : nullptr;

    if (data && ((data->pad3 >> 8) & 0x8)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsPVPFreeForAll(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_UnitFactionGroup(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitFactionGroup(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    auto raceRec = data ? g_chrRacesDB.GetRecord(data->pad1 & 0xFF) : nullptr;

    if (!raceRec) {
        lua_pushnil(L);
        lua_pushnil(L);

        return 2;
    }

    if (raceRec->m_alliance) {
        lua_pushstring(L, "Horde");
        lua_pushstring(L, "Horde");
    } else {
        lua_pushstring(L, "Alliance");
        lua_pushstring(L, "Alliance");
    }

    return 2;
}

// ref: FUN_0060d280
// Takes TWO units and reports how the first regards the second. The reference resolves both and
// answers nil unless both resolve, then pushes its internal reaction plus one.
int32_t Script_UnitReaction(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        luaL_error(L, "Usage: UnitReaction(\"unit\", \"otherUnit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto other = Script_GetUnitFromName(lua_tostring(L, 2));

    if (!unit || !other) {
        lua_pushnil(L);

        return 1;
    }

    lua_pushnumber(L, unit->GetReaction(other) + 1);

    return 1;
}

// ref: FUN_0060d330
int32_t Script_UnitIsEnemy(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        luaL_error(L, "Usage: UnitIsEnemy(\"unit\", \"otherUnit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto other = Script_GetUnitFromName(lua_tostring(L, 2));

    if (unit && other && unit->GetReaction(other) < 2) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0060d3d0
int32_t Script_UnitIsFriend(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        luaL_error(L, "Usage: UnitIsFriend(\"unit\", \"otherUnit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto other = Script_GetUnitFromName(lua_tostring(L, 2));

    if (!unit || !other) {
        lua_pushnil(L);

        return 1;
    }

    if (unit->GetReaction(other) > 3) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitCanCooperate(lua_State* L) {
    lua_pushnumber(L, 1.0);

    return 1;
}

int32_t Script_UnitCanAssist(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_UnitCanAttack(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitCanAttack(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    // TODO faction reactions
    lua_pushnil(L);

    return 1;
}

int32_t Script_UnitIsCharmed(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsCharmed(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (data && data->charmedBy != 0) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsPossessed(lua_State* L) {
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_PlayerCanTeleport(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_UnitClassification(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitClassification(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    lua_pushstring(L, "normal");

    return 1;
}

int32_t Script_UnitSelectionColor(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitSelectionColor(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));

    if (!unit) {
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushnil(L);

        return 3;
    }

    // The standard reaction palette FrameXML mirrors in FACTION_BAR_COLORS: hostile red, neutral
    // yellow, friendly green. UnitReaction currently answers 5 (neutral) for everything, so this
    // will track it for free once real faction reactions land.
    Script_UnitReaction(L);

    int32_t reaction = lua_isnumber(L, -1) ? static_cast<int32_t>(lua_tonumber(L, -1)) : 5;
    lua_settop(L, -2);

    float r = 1.0f;
    float g = 1.0f;
    float b = 0.0f;

    if (reaction <= 2) {
        r = 1.0f; g = 0.0f; b = 0.0f;
    } else if (reaction >= 5) {
        r = 0.0f; g = 1.0f; b = 0.0f;
    }

    lua_pushnumber(L, r);
    lua_pushnumber(L, g);
    lua_pushnumber(L, b);

    return 3;
}

int32_t Script_UnitGUID(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitGUID(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (!unit) {
        lua_pushnil(L);
        return 1;
    }

    char guid[32];
    SStrPrintf(guid, sizeof(guid), "0x%016llX", static_cast<unsigned long long>(unit->GetGUID()));
    lua_pushstring(L, guid);

    return 1;
}

// ref: FUN_0060e740
int32_t Script_UnitName(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitName(\"unit\")");
        return 0;
    }

    auto token = lua_tostring(L, 1);

    // The reference answers "player" from its own copy of the logged-in character before it ever
    // touches the object manager, so the player frame has a name whether or not the player object
    // resolves. The second return is the realm, which is nil for a same-realm character.
    if (!SStrCmpI(token, "player", STORM_MAX_STR)) {
        auto localName = CGPlayer_C::GetLocalPlayerName();

        if (localName) {
            lua_pushstring(L, localName);
            lua_pushnil(L);

            return 2;
        }
    }

    auto unit = Script_GetUnitFromName(token);

    if (!unit) {
        lua_pushnil(L);
        lua_pushnil(L);

        return 2;
    }

    // Everyone, the player included, resolves through the name cache: it is keyed by GUID, and the
    // server answers a name query for the player's own GUID like any other.
    //
    // The player used to be special-cased to the glue's selected character, which broke as soon as
    // the world loaded: entering the world runs CGlueMgr::Suspend -> CCharacterSelection::Shutdown,
    // which clears s_characterList, so GetSelectedCharacter() returns null and every lookup fell
    // back to "Unknown" -- the name shown on the player frame for the whole session.
    //
    // The selection is still consulted first, because it is authoritative and immediate while the
    // glue is up, whereas the cache only fills once the query reply lands.
    // The reference falls back to the localized UNKNOWNOBJECT global string rather than a literal,
    // so this reads "Unknown" only in enUS; every other locale gets its own word.
    const char* name = FrameScript_GetText("UNKNOWNOBJECT", -1, GENDER_NOT_APPLICABLE);
    const char* resolved = nullptr;

    if (unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
        auto selected = CCharacterSelection::GetSelectedCharacter();

        if (selected && selected->m_info.name[0]) {
            resolved = selected->m_info.name;
        }
    }

    if (!resolved) {
        resolved = NameCacheGetName(unit);
    }

    if (resolved) {
        name = resolved;
    }

    lua_pushstring(L, name);
    lua_pushnil(L);

    return 2;
}

int32_t Script_UnitPVPName(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitPVPName(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));

    if (!unit) {
        lua_pushnil(L);

        return 1;
    }

    // With no PVP title system, this is the plain name -- which is also what the reference returns
    // for a character who has not earned a title, so the fallback is the common case rather than a
    // stand-in. Calling the binding directly rather than through the Lua global, which an addon can
    // replace.
    return Script_UnitName(L);
}

int32_t Script_UnitXP(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitXP(\"unit\")");
        return 0;
    }

    auto name = lua_tostring(L, 1);
    auto unit = Script_GetUnitFromName(name);

    float xp = 0.0f;

    if (unit && unit->IsA(TYPE_PLAYER)) {
        xp = static_cast<CGPlayer_C*>(unit)->GetXP();
    }

    lua_pushnumber(L, xp);

    return 1;
}

int32_t Script_UnitXPMax(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitXPMax(\"unit\")");
        return 0;
    }

    auto name = lua_tostring(L, 1);
    auto unit = Script_GetUnitFromName(name);

    float xpMax = 0.0f;

    if (unit && unit->IsA(TYPE_PLAYER)) {
        xpMax = static_cast<CGPlayer_C*>(unit)->GetNextLevelXP();
    }

    lua_pushnumber(L, xpMax);

    return 1;
}

// Race, class, gender, and power type share UNIT_FIELD_BYTES_0
static int32_t Script_UnitPowerTypeOf(const CGUnitData* data) {
    return (data->pad1 >> 24) & 0xFF;
}

int32_t Script_UnitHealth(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitHealth(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    lua_pushnumber(L, data ? data->health : 0);

    return 1;
}

int32_t Script_UnitHealthMax(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitHealthMax(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    lua_pushnumber(L, data ? data->maxHealth : 0);

    return 1;
}

// Defined below; the mana names are aliases for these.
int32_t Script_UnitPower(lua_State* L);
int32_t Script_UnitPowerMax(lua_State* L);

// ref: FUN_0060ed40
// The reference registers one function under both names, so UnitMana is UnitPower -- including its
// usage string. It is NOT "the mana field": asking a warrior through this name answers with rage,
// because the underlying function reads whichever power the unit actually uses.
int32_t Script_UnitMana(lua_State* L) {
    return Script_UnitPower(L);
}

// ref: FUN_0060ef40
// One function under both names again; see Script_UnitMana.
int32_t Script_UnitManaMax(lua_State* L) {
    return Script_UnitPowerMax(L);
}

// ref: FUN_0060ed40
int32_t Script_UnitPower(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitPower(\"unit\"[, type])");
        return 0;
    }

    // 7 is the reference's sentinel for "the power this unit actually uses".
    int32_t type = 7;

    if (lua_isnumber(L, 2)) {
        type = static_cast<int32_t>(lua_tointeger(L, 2));

        if (type < 0 || type > 7) {
            lua_pushnumber(L, 0.0);

            return 1;
        }
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (type == 7) {
        type = data ? Script_UnitPowerTypeOf(data) : 0;
    }

    lua_pushnumber(L, data && type >= 0 && type < 7 ? data->power[type] : 0);

    return 1;
}

// ref: FUN_0060ef40
int32_t Script_UnitPowerMax(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitPowerMax(\"unit\"[, type])");
        return 0;
    }

    int32_t type = 7;

    if (lua_isnumber(L, 2)) {
        type = static_cast<int32_t>(lua_tointeger(L, 2));

        if (type < 0 || type > 7) {
            lua_pushnumber(L, 0.0);

            return 1;
        }
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (type == 7) {
        type = data ? Script_UnitPowerTypeOf(data) : 0;
    }

    lua_pushnumber(L, data && type >= 0 && type < 7 ? data->maxPower[type] : 0);

    return 1;
}

// ref: FUN_0060f100
int32_t Script_UnitPowerType(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitPowerType(\"unit\"[, index])");
        return 0;
    }

    // The optional index selects one of the alternate power displays a vehicle exposes, 1-based on
    // the Lua side. When one is present the reference answers with five values -- type, token and
    // an r,g,b taken from the display record. Frozen has no vehicle or alternate power, so that
    // branch cannot be reached, and any index but the default gets the reference's own "no such
    // power" answer: zero and an empty token.
    int32_t index = 0;

    if (lua_isnumber(L, 2)) {
        index = static_cast<int32_t>(lua_tointeger(L, 2)) - 1;
    }

    if (index != 0) {
        lua_pushnumber(L, 0.0);
        lua_pushstring(L, "");

        return 2;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    static const char* s_powerTokens[] = { "MANA", "RAGE", "FOCUS", "ENERGY", "HAPPINESS", "RUNES", "RUNIC_POWER" };

    int32_t type = data ? Script_UnitPowerTypeOf(data) : 0;

    if (type < 0 || type >= 7) {
        type = 0;
    }

    lua_pushnumber(L, type);
    lua_pushstring(L, s_powerTokens[type]);

    return 2;
}

// ref: FUN_0060f350
// Bit 20 of the unit flags. 1 or nil, never false.
int32_t Script_UnitOnTaxi(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitOnTaxi(\"unit\")");

        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (data && (data->flags & 0x00100000)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// TODO FUN_0060f3d0 tests bit 5 of a dword at +0x124 of whatever the unit keeps at +0xd0.
// That is not the descriptor: the same +0xd0 holds a power-type byte at +0x47, which no
// CGUnitData offset matches. Identify that member before reading flags out of it.
// ref: FUN_00512a30
// The party test falling through to the raid one. Note the party half here is the WIDER of the two
// party tests -- it matches the player and the player's pet, where the one behind
// UnitPlayerOrPetInParty does not.
static bool InPartyOrRaid(WOWGUID guid) {
    return CGPartyInfo::IsPlayerOrMemberOrPet(guid) || CGRaidInfo::IsMemberOrPet(guid);
}

// ref: FUN_0060f3d0
// Feign death is dynamicFlags bit 5, but the flag alone is not the answer: the reference reports it
// only for a unit in your group. A hostile hunter feigning is not something you are told about.
int32_t Script_UnitIsFeignDeath(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsFeignDeath(\"unit\")");

        return 0;
    }

    auto token = lua_tostring(L, 1);
    auto unit = Script_GetUnitFromName(token);
    auto data = unit ? unit->Unit() : nullptr;

    if (data && InPartyOrRaid(unit->GetGUID()) && (data->dynamicFlags & 0x20)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsDead(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsDead(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (data && data->health == 0) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsGhost(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsGhost(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsDeadOrGhost(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsDeadOrGhost(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (data && data->health == 0) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsConnected(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsConnected(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (unit != nullptr) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitAffectingCombat(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitAffectingCombat(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (data && (data->flags & 0x80000)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0060f8e0
int32_t Script_UnitSex(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitSex(\"unit\")");
        return 0;
    }

    auto token = lua_tostring(L, 1);

    // The reference maps the sex through a three-entry table rather than doing arithmetic on it:
    // male to 2, female to 3, and none to 1 -- and 1 is also what it reports for a unit it cannot
    // resolve. Arithmetic agrees for male and female but not for none, which it would call 4.
    static const int32_t s_sexValues[] = { 2, 3, 1 };

    int32_t sex = -1;

    if (!SStrCmpI(token, "player", STORM_MAX_STR)) {
        sex = CGPlayer_C::GetLocalPlayerSex();
    } else {
        auto unit = Script_GetUnitFromName(token);
        auto data = unit ? unit->Unit() : nullptr;

        if (data) {
            sex = (data->pad1 >> 16) & 0xFF;
        }
    }

    // The reference indexes without a bound; a sex outside the table cannot occur on the wire.
    auto value = s_sexValues[2];

    if (sex >= 0 && sex < static_cast<int32_t>(sizeof(s_sexValues) / sizeof(s_sexValues[0]))) {
        value = s_sexValues[sex];
    }

    lua_pushnumber(L, value);

    return 1;
}

// ref: FUN_0060f9e0
int32_t Script_UnitLevel(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitLevel(\"unit\")");
        return 0;
    }

    auto token = lua_tostring(L, 1);
    auto unit = Script_GetUnitFromName(token);
    auto data = unit ? unit->Unit() : nullptr;

    if (data) {
        // TODO the reference does more than report the field. It answers -1 for a unit far enough
        // above the player to display as "??", and on another branch subtracts an offset and
        // clamps to 1. Neither condition has been identified -- both hang off helpers that are
        // still unlinked -- so the plain level is reported for every resolvable unit.
        lua_pushnumber(L, data->level);

        return 1;
    }

    // Only once the object lookup misses does the reference consider the token: "player" is
    // answered from the logged-in character's own record, everything else falls back to 0.
    if (!SStrCmpI(token, "player", STORM_MAX_STR)) {
        lua_pushnumber(L, CGPlayer_C::GetLocalPlayerLevel());

        return 1;
    }

    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetMoney(lua_State* L) {
    auto player = CGPlayer_C::GetActivePtr();

    if (player) {
        lua_pushnumber(L, player->GetMoney());
    } else {
        lua_pushnumber(L, 0.0f);
    }

    return 1;
}

int32_t Script_GetHonorCurrency(lua_State* L) {
    // Honour points held. Compared and formatted as a number throughout the PVP and vendor panes.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetArenaCurrency(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

// ref: FUN_0060fd40
int32_t Script_UnitRace(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitRace(\"unit\")");
        return 0;
    }

    auto token = lua_tostring(L, 1);

    const ChrRacesRec* raceRec = nullptr;
    auto sex = UNITSEX_MALE;

    // "player" is answered from the logged-in character's own record, ahead of the object manager.
    if (!SStrCmpI(token, "player", STORM_MAX_STR)) {
        raceRec = g_chrRacesDB.GetRecord(CGPlayer_C::GetLocalPlayerRace());
        sex = static_cast<UNIT_SEX>(CGPlayer_C::GetLocalPlayerSex());
    } else {
        auto unit = Script_GetUnitFromName(token);
        auto data = unit ? unit->Unit() : nullptr;

        if (data) {
            raceRec = g_chrRacesDB.GetRecord(data->pad1 & 0xFF);
            sex = static_cast<UNIT_SEX>((data->pad1 >> 16) & 0xFF);
        }
    }

    // Two returns, not three: the race id as a third value is a later expansion's signature, and
    // the reference here pushes exactly the display name and the client file string.
    if (!raceRec) {
        lua_pushnil(L);
        lua_pushnil(L);

        return 2;
    }

    lua_pushstring(L, CGUnit_C::GetDisplayRaceNameFromRecord(raceRec, sex, nullptr));
    lua_pushstring(L, raceRec->m_clientFileString);

    return 2;
}

// ref: FUN_0060fec0
int32_t Script_UnitClass(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitClass(\"unit\")");
        return 0;
    }

    auto token = lua_tostring(L, 1);

    const ChrClassesRec* classRec = nullptr;
    auto sex = UNITSEX_MALE;

    if (!SStrCmpI(token, "player", STORM_MAX_STR)) {
        classRec = g_chrClassesDB.GetRecord(CGPlayer_C::GetLocalPlayerClass());
        sex = static_cast<UNIT_SEX>(CGPlayer_C::GetLocalPlayerSex());
    } else {
        auto unit = Script_GetUnitFromName(token);
        auto data = unit ? unit->Unit() : nullptr;

        if (data) {
            classRec = g_chrClassesDB.GetRecord((data->pad1 >> 8) & 0xFF);
            sex = static_cast<UNIT_SEX>((data->pad1 >> 16) & 0xFF);
        }
    }

    // Two returns, not three, for the same reason as UnitRace.
    if (!classRec) {
        lua_pushnil(L);
        lua_pushnil(L);

        return 2;
    }

    lua_pushstring(L, CGUnit_C::GetDisplayClassNameFromRecord(classRec, sex, nullptr));
    lua_pushstring(L, classRec->m_filename);

    return 2;
}

// ref: FUN_00610040
// Two returns, like UnitClass above, and the token is the same. The difference is the first value:
// this one is the record's neutral name, where UnitClass picks the sex-appropriate display name.
// That is the whole reason both bindings exist.
//
// The reference reads the class from the byte at descriptor +0x45, which is what pad1 >> 8 is here
// -- the same byte the modified-click class masks test.
int32_t Script_UnitClassBase(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitClassBase(\"unit\")");

        return 0;
    }

    auto token = lua_tostring(L, 1);

    const ChrClassesRec* classRec = nullptr;

    if (!SStrCmpI(token, "player", STORM_MAX_STR)) {
        classRec = g_chrClassesDB.GetRecord(CGPlayer_C::GetLocalPlayerClass());
    } else {
        auto unit = Script_GetUnitFromName(token);
        auto data = unit ? unit->Unit() : nullptr;

        if (data) {
            classRec = g_chrClassesDB.GetRecord((data->pad1 >> 8) & 0xFF);
        }

        // TODO when the token names something that is not a unit, the reference falls back to the
        // creature display cache and derives a class from it. That cache has no frozen counterpart,
        // so such a token answers nil here instead.
    }

    // Two nils rather than none, so the caller's second return does not come back as a stray value
    // from further up the stack.
    if (!classRec) {
        lua_pushnil(L);
        lua_pushnil(L);

        return 2;
    }

    lua_pushstring(L, classRec->m_name);
    lua_pushstring(L, classRec->m_filename);

    return 2;
}

// ref: FUN_004f54d0
// Splits a resistance into what it would be without buffs and what it is now. The base is computed
// BEFORE the total is clamped, so a resistance debuffed below zero still reports the base it came
// from rather than a base derived from the clamped zero.
static void UnitResistanceBreakdown(const CGUnitData* data, uint32_t index, int32_t* base,
                                    int32_t* total, int32_t* positive, int32_t* negative) {
    *total = data->resistance[index];
    *positive = data->resistanceBuffModsPositive[index];
    *negative = data->resistanceBuffModsNegative[index];
    *base = (*total - *positive) - *negative;

    if (*total < 0) {
        *total = 0;
    }
}

// ref: FUN_006101a0
// base, resistance, positive, negative.
//
// The index is 0-based, where UnitStat below is 1-based. That asymmetry is the reference's, and it
// is the kind of thing that silently reports armour as holy resistance if assumed away.
//
// The full breakdown is only available for the active player; the buff mods are not sent for anyone
// else, so another unit reports its total as its base with no mods, which is what the reference
// does rather than leaving them nil.
int32_t Script_UnitResistance(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: UnitResistance(\"unit\", resistanceIndex)");

        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto index = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 2)));

    if (index > 6) {
        luaL_error(L, "Invalid resistance index in UnitResistance");

        return 0;
    }

    int32_t base = 0;
    int32_t total = 0;
    int32_t positive = 0;
    int32_t negative = 0;

    auto data = unit ? unit->Unit() : nullptr;

    if (data) {
        if (unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
            UnitResistanceBreakdown(data, index, &base, &total, &positive, &negative);
        } else {
            total = data->resistance[index];

            if (total < 0) {
                total = 0;
            }

            base = total;
        }
    }

    lua_pushnumber(L, static_cast<double>(base));
    lua_pushnumber(L, static_cast<double>(total));
    lua_pushnumber(L, static_cast<double>(positive));
    lua_pushnumber(L, static_cast<double>(negative));

    return 4;
}

// ref: FUN_00610300
// base, stat, positive, negative -- and the first two are NOT base-without-buffs and base-with.
// Both read stats[index]; the second is simply the same value with negatives clamped to zero, so
// they differ only for a stat debuffed below zero. Reading them as base-vs-current would be a
// plausible and wrong reading of the same four numbers, which is why it is written down.
//
// The clamp is branchless in the reference (v & ((v < 0) - 1)); this is the same function of v.
int32_t Script_UnitStat(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: UnitStat(\"unit\", statIndex)");

        return 0;
    }

    // Resolved before the index is range-checked, so a bad index on a bad unit still reports the
    // index rather than failing earlier.
    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto index = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 2))) - 1;

    if (index >= 5) {
        luaL_error(L, "Invalid stat index in UnitStat");

        return 0;
    }

    auto data = unit ? unit->Unit() : nullptr;

    if (!data) {
        for (int32_t i = 0; i < 4; i++) {
            lua_pushnumber(L, 0.0);
        }

        return 4;
    }

    auto stat = data->stats[index];

    lua_pushnumber(L, static_cast<double>(stat));
    lua_pushnumber(L, static_cast<double>(stat < 0 ? 0 : stat));
    lua_pushnumber(L, static_cast<double>(data->posStats[index]));
    lua_pushnumber(L, static_cast<double>(data->negStats[index]));

    return 4;
}

int32_t Script_UnitAttackBothHands(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00610860
// Seven returns: the four damage bounds, then the physical damage modifiers.
//
// The modifiers are the same three helpers GetSpellBonusDamage uses, asked for school 0 -- physical
// is just another school here, which is why no separate field exists for it.
//
// A non-player unit reports 0, 0 and 1.0 for the three: one, not zero, because the last is a
// multiplier. Note the asymmetry the reference leaves in place -- a player who is NOT the active
// player takes the helper path, and those helpers answer 0 for anyone but the active player, so
// the multiplier comes back 0 rather than 1. Reproduced rather than smoothed out.
int32_t Script_UnitDamage(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitDamage(\"unit\")");

        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (!data) {
        for (int32_t i = 0; i < 7; i++) {
            lua_pushnumber(L, 0.0);
        }

        return 7;
    }

    lua_pushnumber(L, data->minDamage);
    lua_pushnumber(L, data->maxDamage);
    lua_pushnumber(L, data->minOffhandDamage);
    lua_pushnumber(L, data->maxOffhandDamage);

    if (!unit->IsA(TYPE_PLAYER)) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 1.0);

        return 7;
    }

    auto player = static_cast<CGPlayer_C*>(unit);

    lua_pushnumber(L, static_cast<double>(player->GetModDamageDonePos(0)));
    lua_pushnumber(L, static_cast<double>(player->GetModDamageDoneNeg(0)));
    lua_pushnumber(L, static_cast<double>(player->GetModDamageDonePct(0)));

    return 7;
}

// ref: FUN_00610550
// Six returns: ranged swing time in seconds, the two damage bounds, then the same three damage
// modifiers UnitDamage reports.
//
// DIVERGED, deliberately, in the school those three modifiers are read for. The reference does not
// use physical: it finds the equipped ranged weapon, looks its item record up in the cache, walks
// that record's damage entries for the first with positive damage, and takes THAT entry's school.
// Frozen has neither the equipped-weapon lookup nor damage fields on ItemInfo, so this asks for
// school 0.
//
// That is the right school for any ranged weapon dealing physical damage, which is nearly all of
// them, and wrong for one that deals elemental damage. It is a narrower guess than returning the
// non-player defaults would be -- those would report a multiplier of 1.0 for a player whose
// modifiers are real -- but it is still a guess, so it is recorded in overrides.json rather than
// left to look exact.
int32_t Script_UnitRangedDamage(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitRangedDamage(\"unit\")");

        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (!data) {
        for (int32_t i = 0; i < 6; i++) {
            lua_pushnumber(L, 0.0);
        }

        return 6;
    }

    // Milliseconds, unsigned, scaled to seconds -- the same conversion as UnitAttackSpeed.
    lua_pushnumber(L, static_cast<float>(data->rangedAttackTime) * 0.001f);
    lua_pushnumber(L, data->minRangedDamage);
    lua_pushnumber(L, data->maxRangedDamage);

    if (!unit->IsA(TYPE_PLAYER)) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 1.0);

        return 6;
    }

    auto player = static_cast<CGPlayer_C*>(unit);

    lua_pushnumber(L, static_cast<double>(player->GetModDamageDonePos(0)));
    lua_pushnumber(L, static_cast<double>(player->GetModDamageDoneNeg(0)));
    lua_pushnumber(L, static_cast<double>(player->GetModDamageDonePct(0)));

    return 6;
}

int32_t Script_UnitRangedAttack(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// The class of whatever sits in one of the active player's equipment slots, or -1 when the slot is
// empty or unreadable. The reference gets this from Item.dbc keyed by the item's entry id
// (FUN_00707220), which is what g_itemDB is here.
//
// Only the active player's slots can be answered: nobody else's item guids are sent, so frozen's
// invSlots is the player's alone. The reference reads an array on the unit object itself, which is
// populated for the same one unit, so the reachable answers agree.
static int32_t EquippedItemClass(const CGUnit_C* unit, int32_t slot) {
    if (!unit || unit->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return -1;
    }

    auto player = CGPlayer_C::GetActivePtr();
    auto data = player ? player->Player() : nullptr;

    if (!data) {
        return -1;
    }

    auto object = ClntObjMgrObjectPtr(data->invSlots[slot], TYPE_ITEM, __FILE__, __LINE__);

    if (!object) {
        return -1;
    }

    auto rec = g_itemDB.GetRecord(static_cast<CGItem_C*>(object)->GetEntryID());

    return rec ? rec->m_classID : -1;
}

// ref: FUN_00610a00
// Main-hand and off-hand swing times in seconds. The stored value is milliseconds and UNSIGNED --
// the reference converts it with the uint32-to-float fixup, not the signed one -- then scales by
// the 0.001f at 009e1134.
//
// The off-hand time is reported only when the off-hand slot holds a WEAPON: a shield or an
// off-hand frill is a different item class and leaves the second return nil.
int32_t Script_UnitAttackSpeed(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitAttackSpeed(\"unit\")");

        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    // No unit is 0 and nil, not two nils: the reference pushes a number for the main hand either
    // way and only ever leaves the off-hand empty.
    if (!data) {
        lua_pushnumber(L, 0.0);
        lua_pushnil(L);

        return 2;
    }

    lua_pushnumber(L, static_cast<float>(data->attackRoundBaseTime[0]) * 0.001f);

    // Item class 2 is weapon. The reference compares against the literal too.
    if (EquippedItemClass(unit, INVSLOT_OFFHAND) == 2) {
        lua_pushnumber(L, static_cast<float>(data->attackRoundBaseTime[1]) * 0.001f);
    } else {
        lua_pushnil(L);
    }

    return 2;
}

// ref: FUN_00610b60
// Three returns: attack power and its positive and negative modifiers, each scaled.
//
// Two properties of the fields, both of which yield plausible wrong numbers if assumed rather than
// read. The modifiers are a packed pair of int16 in one dword, positive low and negative high. And
// the multiplier is a float sharing a block with integers -- the scale is that float PLUS ONE, so
// a unit with no multiplier reports its raw values rather than zero.
//
// Rounding is x87 round-to-nearest in the reference; nearbyint keeps that rather than half-away.
// Written out in each binding rather than shared, because the reference has two separate functions
// and a common helper would put a call in frozen's sequence that is not in the reference's.
int32_t Script_UnitAttackPower(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitAttackPower(\"unit\")");

        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (!data) {
        for (int32_t i = 0; i < 3; i++) {
            lua_pushnumber(L, 0.0);
        }

        return 3;
    }

    auto scale = data->attackPowerMultiplier + 1.0f;
    auto positive = static_cast<int16_t>(data->attackPowerMods & 0xFFFF);
    auto negative = static_cast<int16_t>(static_cast<uint32_t>(data->attackPowerMods) >> 16);

    lua_pushnumber(L, nearbyintf(scale * static_cast<float>(data->attackPower)));
    lua_pushnumber(L, nearbyintf(scale * static_cast<float>(positive)));
    lua_pushnumber(L, nearbyintf(scale * static_cast<float>(negative)));

    return 3;
}

// ref: FUN_00610ca0
// The same three values and the same packing as UnitAttackPower above, twelve bytes along in the
// descriptor: 0x1e0, 0x1e4/0x1e6, 0x1e8.
int32_t Script_UnitRangedAttackPower(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitRangedAttackPower(\"unit\")");

        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (!data) {
        for (int32_t i = 0; i < 3; i++) {
            lua_pushnumber(L, 0.0);
        }

        return 3;
    }

    auto scale = data->rangedAttackPowerMultiplier + 1.0f;
    auto positive = static_cast<int16_t>(data->rangedAttackPowerMods & 0xFFFF);
    auto negative = static_cast<int16_t>(static_cast<uint32_t>(data->rangedAttackPowerMods) >> 16);

    lua_pushnumber(L, nearbyintf(scale * static_cast<float>(data->rangedAttackPower)));
    lua_pushnumber(L, nearbyintf(scale * static_cast<float>(positive)));
    lua_pushnumber(L, nearbyintf(scale * static_cast<float>(negative)));

    return 3;
}

// TODO FUN_00610de0 reads its two values through a virtual at vtable+0x140 rather than from
// the descriptor, so the field offsets that made the combat percentages safe do not help.
int32_t Script_UnitDefense(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// TODO FUN_00610ec0 returns five values built by FUN_006337a0 and FUN_004f54d0, neither
// identified. Only the first is the armour itself.
int32_t Script_UnitArmor(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitCharacterPoints(lua_State* L) {
    // Unspent talent points. The talent frame does arithmetic on this the moment it opens, so a
    // stub that returned nothing errored there. Talents are not ported, so the player has none
    // unspent.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_UnitAura(lua_State* L);

// UnitBuff and UnitDebuff are UnitAura with the filter fixed, and return the same list. Rather than
// duplicate the record-to-Lua conversion, each forces the filter argument and defers.
int32_t Script_UnitAuraFiltered(lua_State* L, const char* filter) {
    // UnitBuff takes (unit, index [, filter]); the caller's own filter is replaced, since HELPFUL or
    // HARMFUL is exactly what distinguishes these two from UnitAura.
    lua_settop(L, 2);
    lua_pushstring(L, filter);

    return Script_UnitAura(L);
}

int32_t Script_UnitBuff(lua_State* L) {
    return Script_UnitAuraFiltered(L, "HELPFUL");
}

int32_t Script_UnitDebuff(lua_State* L) {
    return Script_UnitAuraFiltered(L, "HARMFUL");
}

// UnitAura("unit", index [, filter])
//   -> name, rank, texture, count, debuffType, duration, expirationTime,
//      unitCaster, isStealable, shouldConsolidate, spellID
//
// The shape FrameXML destructures in BuffFrame.lua. Durations are in SECONDS, and expirationTime is
// absolute on GetTime()'s clock, which is why AuraCache stamps each record with OsGetAsyncTimeMs()
// when it arrives rather than storing only the remaining time.
int32_t Script_UnitAura(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitAura(\"unit\", index [, filter])");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    int32_t index = lua_isnumber(L, 2) ? static_cast<int32_t>(lua_tonumber(L, 2)) - 1 : 0;

    uint8_t required = 0;
    uint8_t forbidden = 0;

    // The filter is a space or pipe separated list; only the two that select the aura set change
    // which auras are visible, and BuffFrame passes exactly one of them.
    if (lua_isstring(L, 3)) {
        const char* filter = lua_tostring(L, 3);

        if (SStrStrI(filter, "HELPFUL")) {
            required = AURA_FLAG_POSITIVE;
        } else if (SStrStrI(filter, "HARMFUL")) {
            forbidden = AURA_FLAG_POSITIVE;
        }
    }

    const ClientAura* aura = unit
        ? AuraCacheGet(unit->GetGUID(), index, required, forbidden)
        : nullptr;

    if (!aura) {
        lua_pushnil(L);

        return 1;
    }

    auto spell = g_spellDB.GetRecord(aura->spellID);
    const SpellIconRec* icon = spell && spell->m_spellIconID
        ? g_spellIconDB.GetRecord(spell->m_spellIconID)
        : nullptr;

    // name
    if (spell && spell->m_name && *spell->m_name) {
        lua_pushstring(L, spell->m_name);
    } else {
        lua_pushnil(L);
    }

    // rank -- Spell.dbc carries it in a separate column that is not read yet
    lua_pushnil(L);

    // texture
    if (icon && icon->m_textureFilename && *icon->m_textureFilename) {
        lua_pushstring(L, icon->m_textureFilename);
    } else {
        lua_pushnil(L);
    }

    // count: the server sends stacks, or charges when the spell does not stack
    lua_pushnumber(L, aura->stacks);

    // debuffType: the dispel school, which needs a Spell.dbc column that is not read yet
    lua_pushnil(L);

    if (aura->flags & AURA_FLAG_DURATION) {
        lua_pushnumber(L, aura->maxDuration / 1000.0);
        lua_pushnumber(L, (aura->receivedMs + aura->duration) / 1000.0);
    } else {
        // A permanent aura reports 0 / 0, which is what FrameXML tests for to hide the timer.
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
    }

    // unitCaster: resolving a guid back to a unit token needs a reverse lookup that does not exist
    lua_pushnil(L);

    // isStealable, shouldConsolidate: both need spell attributes that are not read yet
    lua_pushboolean(L, 0);
    lua_pushboolean(L, 0);

    lua_pushnumber(L, aura->spellID);

    return 11;
}

int32_t Script_UnitIsTapped(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsTapped(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitIsTappedByPlayer(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_UnitIsTappedByAllThreatList(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_UnitIsTrivial(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsTrivial(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitHasRelicSlot(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetPortraitTexture(lua_State* L) {
    // TODO portraits
    return 0;
}

int32_t Script_HasFullControl(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GetComboPoints(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_IsInGuild(lua_State* L) {
    // There is no guild subsystem: no guild roster or membership is ever received, so from this
    // client's point of view the player is in none.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsGuildLeader(lua_State* L) {
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsArenaTeamCaptain(lua_State* L) {
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsInArenaTeam(lua_State* L) {
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsResting(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GetCombatRating(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetCombatRatingBonus(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetMaxCombatRatingBonus(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0060df30
int32_t Script_GetDodgeChance(lua_State* L) {
    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? data->dodgePercentage : 0.0f);

    return 1;
}

// ref: FUN_0060df90
int32_t Script_GetBlockChance(lua_State* L) {
    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? data->blockPercentage : 0.0f);

    return 1;
}

int32_t Script_GetShieldBlock(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

// ref: FUN_0060e070
int32_t Script_GetParryChance(lua_State* L) {
    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? data->parryPercentage : 0.0f);

    return 1;
}

int32_t Script_GetCritChanceFromAgility(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetSpellCritChanceFromIntellect(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

// ref: FUN_0060e0d0
int32_t Script_GetCritChance(lua_State* L) {
    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? data->critPercentage : 0.0f);

    return 1;
}

// ref: FUN_0060e230
int32_t Script_GetRangedCritChance(lua_State* L) {
    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? data->rangedCritPercentage : 0.0f);

    return 1;
}

// ref: FUN_0060e290
// School is 1-based from Lua and indexes a seven-entry array. The reference tests the converted
// index as unsigned, so school 0 and every negative wrap past the end and take the usage error
// rather than reading in front of the array.
int32_t Script_GetSpellCritChance(lua_State* L) {
    auto school = static_cast<uint32_t>(lua_tonumber(L, 1)) - 1;

    if (school >= 7) {
        return luaL_error(L, "Usage: GetSpellCritChance(school)");
    }

    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? data->spellCritPercentage[school] : 0.0f);

    return 1;
}

// TODO FUN_0060e310 takes the same 1-based school as GetSpellCritChance but does not read a
// field directly: it combines two helpers at 00578210 and 00578250, neither identified.
// The school bounds check and the usage string are the same as the crit one above.
// ref: FUN_0060e310
// Spell power for one school: the positive and negative modifiers added together. The reference
// adds them rather than subtracting, so the field named Neg is expected to arrive already signed;
// this reproduces that rather than second-guessing the sign.
//
// The range is checked before the player is resolved, so a bad school errors even with no player,
// and the 1-based school is tested as unsigned -- school 0 wraps and takes the usage error instead
// of reading in front of the array. Same shape as GetSpellCritChance.
int32_t Script_GetSpellBonusDamage(lua_State* L) {
    auto school = static_cast<uint32_t>(lua_tonumber(L, 1)) - 1;

    if (school >= 7) {
        return luaL_error(L, "Usage: GetSpellBonusDamage(school)");
    }

    auto player = CGPlayer_C::GetActivePtr();

    if (!player) {
        lua_pushnumber(L, 0.0);

        return 1;
    }

    auto bonus = player->GetModDamageDonePos(school) + player->GetModDamageDoneNeg(school);

    lua_pushnumber(L, static_cast<double>(bonus));

    return 1;
}

// ref: FUN_0060e3b0
int32_t Script_GetSpellBonusHealing(lua_State* L) {
    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? data->modHealingDonePos : 0.0f);

    return 1;
}

// TODO FUN_0060e410 reads CGPlayerData + 0x1264, which no named field in frozen's struct
// has been shown to sit at. The five percentages above were safe because three of their
// offsets matched the struct order exactly; this one has no such corroboration yet.
int32_t Script_GetPetSpellBonusDamage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0060e470
// Negated on the way out, which is the whole of the function's content. Spell penetration is held
// as a *negative* modifier to the target's resistance and reported to the interface as a positive
// number, so a port that forwarded the field unchanged would show every value with the wrong sign.
int32_t Script_GetSpellPenetration(lua_State* L) {
    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? -data->modTargetResistance : 0.0f);

    return 1;
}

int32_t Script_GetArmorPenetration(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetAttackPowerForStat(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitCreatureType(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitCreatureType(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    lua_pushnil(L);

    return 1;
}

int32_t Script_UnitCreatureFamily(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetResSicknessDuration(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetPVPSessionStats(lua_State* L) {
    // honorableKills, dishonorableKills
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 2;
}

int32_t Script_GetPVPYesterdayStats(lua_State* L) {
    // honorableKills, dishonorableKills, honorGained
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 3;
}

int32_t Script_GetPVPLifetimeStats(lua_State* L) {
    // honorableKills, dishonorableKills, highestRank
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 3;
}

int32_t Script_UnitPVPRank(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetPVPRankInfo(lua_State* L) {
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);

    return 2;
}

int32_t Script_GetPVPRankProgress(lua_State* L) {
    // A 0..1 fraction the rank bar sets its value from directly.
    lua_pushnumber(L, 0.0);

    return 1;
}

// UnitCastingInfo("unit")  -> name, rank, displayName, icon, startTime, endTime, isTradeSkill,
//                             castID, notInterruptible
// UnitChannelInfo("unit")  -> the same without castID.
//
// startTime and endTime are MILLISECONDS here, not seconds as in UnitAura: CastingBarFrame.lua
// computes `GetTime() - (startTime / 1000)`, which fixes the unit. Getting this wrong would put the
// bar off by three orders of magnitude rather than failing visibly.
int32_t PushCastInfo(lua_State* L, bool channeled, bool withCastID) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: %s(\"unit\")", channeled ? "UnitChannelInfo" : "UnitCastingInfo");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    const ClientCast* cast = unit ? CastCacheGet(unit->GetGUID(), channeled) : nullptr;

    if (!cast) {
        lua_pushnil(L);

        return 1;
    }

    auto spell = g_spellDB.GetRecord(cast->spellID);
    const SpellIconRec* icon = spell && spell->m_spellIconID
        ? g_spellIconDB.GetRecord(spell->m_spellIconID)
        : nullptr;

    const char* name = spell && spell->m_name && *spell->m_name ? spell->m_name : nullptr;

    if (name) {
        lua_pushstring(L, name);
    } else {
        lua_pushnil(L);
    }

    // rank -- a Spell.dbc column that is not read yet
    lua_pushnil(L);

    // displayName is the same string as name outside of a few special cases
    if (name) {
        lua_pushstring(L, name);
    } else {
        lua_pushnil(L);
    }

    if (icon && icon->m_textureFilename && *icon->m_textureFilename) {
        lua_pushstring(L, icon->m_textureFilename);
    } else {
        lua_pushnil(L);
    }

    lua_pushnumber(L, cast->startMs);
    lua_pushnumber(L, cast->endMs);

    // isTradeSkill -- needs a spell attribute that is not read yet
    lua_pushboolean(L, 0);

    if (withCastID) {
        // The cast count byte is read off the wire but not kept; nothing in FrameXML compares it.
        lua_pushnil(L);
    }

    // notInterruptible -- carried in the cast flags, which are not decoded yet
    lua_pushboolean(L, 0);

    return withCastID ? 9 : 8;
}

int32_t Script_UnitCastingInfo(lua_State* L) {
    return PushCastInfo(L, false, true);
}

int32_t Script_UnitChannelInfo(lua_State* L) {
    return PushCastInfo(L, true, false);
}

int32_t Script_IsLoggedIn(lua_State* L) {
    // True once the world is up, which is what FrameXML gates its initial queries on.
    lua_pushboolean(L, CGGameUI::IsInWorld());

    return 1;
}

int32_t Script_IsFlyableArea(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsIndoors(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// TODO FUN_00612360 and FUN_00612300 (IsIndoors) are one predicate and its negation, both
// resting on a CGUnit_C method at 0071b7f0 that frozen has no counterpart for. Answering
// from the map instead would be a guess about what the server considers indoors.
int32_t Script_IsOutdoors(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsOutOfBounds(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsFalling(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsSwimming(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsFlying(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// TODO FUN_006125a0 reads two fields on the object rather than the descriptor -- a count at
// +0x9c0 that must be positive and a flag 0x10000000 at +0xa30 that must be clear. Neither
// has a frozen counterpart, and the mount state is not derivable from the descriptor.
int32_t Script_IsMounted(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsStealthed(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitIsSameServer(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitIsSameServer(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));

    // This client talks to one realm, so any unit it can see is on it.
    lua_pushboolean(L, unit != nullptr);

    return 1;
}

int32_t Script_GetUnitHealthModifier(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetUnitMaxHealthModifier(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetUnitPowerModifier(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetUnitHealthRegenRateFromSpirit(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_GetUnitManaRegenRateFromSpirit(lua_State* L) {
    // The subsystem behind this is not implemented, so the count is genuinely zero. Returning
    // nothing instead raised "attempt to perform arithmetic on a nil value" in the caller.
    lua_pushnumber(L, 0.0);

    return 1;
}

// TODO FUN_00612a90. Two returns, the not-casting and casting rates, each from a regen
// helper at 004f5390 that frozen has no counterpart for -- the fields alone are not the
// answer. This one also gates on the player's power type being mana; GetPowerRegen below
// is the same function without that gate.
int32_t Script_GetManaRegen(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// TODO FUN_00612b40, blocked on the same 004f5390 as GetManaRegen.
int32_t Script_GetPowerRegen(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetRuneCooldown(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 1.0);

    return 3;
}

int32_t Script_GetRuneCount(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetRuneType(lua_State* L) {
    // 1 blood, 2 unholy, 3 frost, 4 death; runes 1-2 blood, 3-4 unholy, 5-6 frost
    int32_t rune = lua_isnumber(L, 1) ? static_cast<int32_t>(lua_tonumber(L, 1)) : 1;
    lua_pushnumber(L, rune <= 2 ? 1 : rune <= 4 ? 2 : 3);

    return 1;
}

int32_t Script_ReportPlayerIsPVPAFK(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_PlayerIsPVPInactive(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

// ref: FUN_00612bf0
// The same two fields GetExpertisePercent reads, unscaled. Both are pushed as zeros with no
// player so the sheet keeps two return values rather than going nil.
int32_t Script_GetExpertise(lua_State* L) {
    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? data->expertise : 0);
    lua_pushnumber(L, data ? data->offhandExpertise : 0);

    return 2;
}

// ref: FUN_00612cb0
// Two values, main hand and off hand, each the raw expertise scaled by the 0.25 at 00a1f6f4 -- four
// points of expertise to one percent. Both are pushed even with no player, as zeros, so the sheet
// keeps two return values rather than going nil.
int32_t Script_GetExpertisePercent(lua_State* L) {
    auto data = ActivePlayerData();

    lua_pushnumber(L, data ? data->expertise * 0.25f : 0.0f);
    lua_pushnumber(L, data ? data->offhandExpertise * 0.25f : 0.0f);

    return 2;
}

int32_t Script_UnitInBattleground(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

// ref: FUN_00612f10
// Whether a group member is close enough to matter -- what greys out a party frame.
//
// It is NOT a plain distance test. The unit has to be in your party or raid first, through the
// same gate UnitIsFeignDeath uses, so a stranger standing next to you is out of range by
// definition. FrameXML only asks about group members, but the gate is what makes "in range" mean
// "in range AND mine".
//
// The threshold is 40 yards, read from the reference rather than assumed from the spell range, and
// the distance is three-dimensional: a group member directly above or below is out of range even
// when the map positions coincide.
int32_t Script_UnitInRange(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitInRange(\"unit\")");

        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto player = CGPlayer_C::GetActivePtr();
    auto inRange = false;

    if (unit && player && InPartyOrRaid(unit->GetGUID())) {
        auto a = unit->GetPosition();
        auto b = player->GetPosition();

        auto dx = b.x - a.x;
        auto dy = b.y - a.y;
        auto dz = b.z - a.z;

        inRange = sqrtf(dx * dx + dy * dy + dz * dz) < 40.0f;
    }

    if (inRange) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_GetUnitSpeed(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetUnitPitch(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitInVehicle(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitInVehicle(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitUsingVehicle(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitControllingVehicle(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitControllingVehicle(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitInVehicleControlSeat(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitHasVehicleUI(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: UnitHasVehicleUI(\"unit\")");
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 1));
    auto data = unit ? unit->Unit() : nullptr;

    if (false) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UnitTargetsVehicleInRaidUI(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitVehicleSkin(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitVehicleSeatCount(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitVehicleSeatInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitSwitchToVehicleSeat(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CanSwitchVehicleSeat(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GetVehicleUIIndicator(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetVehicleUIIndicatorSeat(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitThreatSituation(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_UnitDetailedThreatSituation(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UnitIsControlling(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_EjectPassengerFromSeat(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CanEjectPassengerFromSeat(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_RespondInstanceLock(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetPlayerFacing(lua_State* L) {
    auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

    if (!player) {
        lua_pushnil(L);

        return 1;
    }

    lua_pushnumber(L, player->GetFacing());

    return 1;
}

int32_t Script_GetPlayerInfoByGUID(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetItemStats(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetItemStatDelta(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsXPUserDisabled(lua_State* L) {
    // Turning XP gain off is a server-side toggle this client never receives.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_FillLocalizedClassList(lua_State* L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        luaL_error(L, "Usage: FillLocalizedClassList(classTable[, isFemale])");
        return 0;
    }

    auto isFemale = StringToBOOL(L, 2, 0);
    auto sex = isFemale ? UNITSEX_FEMALE : UNITSEX_MALE;

    lua_settop(L, 1);

    for (int32_t i = 0; i < g_chrClassesDB.GetNumRecords(); ++i) {
        auto classRec = g_chrClassesDB.GetRecordByIndex(i);
        if (classRec) {
            auto displayName = CGUnit_C::GetDisplayClassNameFromRecord(classRec, sex, 0);

            lua_pushstring(L, classRec->m_filename);
            lua_pushstring(L, displayName);

            lua_settable(L, -3);
        }
    }

    return 1;
}

}

static FrameScript_Method s_UnitFunctions[] = {
    { "UnitExists",                     &Script_UnitExists },
    { "UnitIsVisible",                  &Script_UnitIsVisible },
    { "UnitIsUnit",                     &Script_UnitIsUnit },
    { "UnitIsPlayer",                   &Script_UnitIsPlayer },
    { "UnitIsInMyGuild",                &Script_UnitIsInMyGuild },
    { "UnitIsCorpse",                   &Script_UnitIsCorpse },
    { "UnitIsPartyLeader",              &Script_UnitIsPartyLeader },
    { "UnitGroupRolesAssigned",         &Script_UnitGroupRolesAssigned },
    { "UnitIsRaidOfficer",              &Script_UnitIsRaidOfficer },
    { "UnitInParty",                    &Script_UnitInParty },
    { "UnitPlayerOrPetInParty",         &Script_UnitPlayerOrPetInParty },
    { "UnitInRaid",                     &Script_UnitInRaid },
    { "UnitPlayerOrPetInRaid",          &Script_UnitPlayerOrPetInRaid },
    { "UnitPlayerControlled",           &Script_UnitPlayerControlled },
    { "UnitIsAFK",                      &Script_UnitIsAFK },
    { "UnitIsDND",                      &Script_UnitIsDND },
    { "UnitIsPVP",                      &Script_UnitIsPVP },
    { "UnitIsPVPSanctuary",             &Script_UnitIsPVPSanctuary },
    { "UnitIsPVPFreeForAll",            &Script_UnitIsPVPFreeForAll },
    { "UnitFactionGroup",               &Script_UnitFactionGroup },
    { "UnitReaction",                   &Script_UnitReaction },
    { "UnitIsEnemy",                    &Script_UnitIsEnemy },
    { "UnitIsFriend",                   &Script_UnitIsFriend },
    { "UnitCanCooperate",               &Script_UnitCanCooperate },
    { "UnitCanAssist",                  &Script_UnitCanAssist },
    { "UnitCanAttack",                  &Script_UnitCanAttack },
    { "UnitIsCharmed",                  &Script_UnitIsCharmed },
    { "UnitIsPossessed",                &Script_UnitIsPossessed },
    { "PlayerCanTeleport",              &Script_PlayerCanTeleport },
    { "UnitClassification",             &Script_UnitClassification },
    { "UnitSelectionColor",             &Script_UnitSelectionColor },
    { "UnitGUID",                       &Script_UnitGUID },
    { "UnitName",                       &Script_UnitName },
    { "UnitPVPName",                    &Script_UnitPVPName },
    { "UnitXP",                         &Script_UnitXP },
    { "UnitXPMax",                      &Script_UnitXPMax },
    { "UnitHealth",                     &Script_UnitHealth },
    { "UnitHealthMax",                  &Script_UnitHealthMax },
    { "UnitMana",                       &Script_UnitMana },
    { "UnitManaMax",                    &Script_UnitManaMax },
    { "UnitPower",                      &Script_UnitPower },
    { "UnitPowerMax",                   &Script_UnitPowerMax },
    { "UnitPowerType",                  &Script_UnitPowerType },
    { "UnitOnTaxi",                     &Script_UnitOnTaxi },
    { "UnitIsFeignDeath",               &Script_UnitIsFeignDeath },
    { "UnitIsDead",                     &Script_UnitIsDead },
    { "UnitIsGhost",                    &Script_UnitIsGhost },
    { "UnitIsDeadOrGhost",              &Script_UnitIsDeadOrGhost },
    { "UnitIsConnected",                &Script_UnitIsConnected },
    { "UnitAffectingCombat",            &Script_UnitAffectingCombat },
    { "UnitSex",                        &Script_UnitSex },
    { "UnitLevel",                      &Script_UnitLevel },
    { "GetMoney",                       &Script_GetMoney },
    { "GetHonorCurrency",               &Script_GetHonorCurrency },
    { "GetArenaCurrency",               &Script_GetArenaCurrency },
    { "UnitRace",                       &Script_UnitRace },
    { "UnitClass",                      &Script_UnitClass },
    { "UnitClassBase",                  &Script_UnitClassBase },
    { "UnitResistance",                 &Script_UnitResistance },
    { "UnitStat",                       &Script_UnitStat },
    { "UnitAttackBothHands",            &Script_UnitAttackBothHands },
    { "UnitDamage",                     &Script_UnitDamage },
    { "UnitRangedDamage",               &Script_UnitRangedDamage },
    { "UnitRangedAttack",               &Script_UnitRangedAttack },
    { "UnitAttackSpeed",                &Script_UnitAttackSpeed },
    { "UnitAttackPower",                &Script_UnitAttackPower },
    { "UnitRangedAttackPower",          &Script_UnitRangedAttackPower },
    { "UnitDefense",                    &Script_UnitDefense },
    { "UnitArmor",                      &Script_UnitArmor },
    { "UnitCharacterPoints",            &Script_UnitCharacterPoints },
    { "UnitBuff",                       &Script_UnitBuff },
    { "UnitDebuff",                     &Script_UnitDebuff },
    { "UnitAura",                       &Script_UnitAura },
    { "UnitIsTapped",                   &Script_UnitIsTapped },
    { "UnitIsTappedByPlayer",           &Script_UnitIsTappedByPlayer },
    { "UnitIsTappedByAllThreatList",    &Script_UnitIsTappedByAllThreatList },
    { "UnitIsTrivial",                  &Script_UnitIsTrivial },
    { "UnitHasRelicSlot",               &Script_UnitHasRelicSlot },
    { "SetPortraitTexture",             &Script_SetPortraitTexture },
    { "HasFullControl",                 &Script_HasFullControl },
    { "GetComboPoints",                 &Script_GetComboPoints },
    { "IsInGuild",                      &Script_IsInGuild },
    { "IsGuildLeader",                  &Script_IsGuildLeader },
    { "IsArenaTeamCaptain",             &Script_IsArenaTeamCaptain },
    { "IsInArenaTeam",                  &Script_IsInArenaTeam },
    { "IsResting",                      &Script_IsResting },
    { "GetCombatRating",                &Script_GetCombatRating },
    { "GetCombatRatingBonus",           &Script_GetCombatRatingBonus },
    { "GetMaxCombatRatingBonus",        &Script_GetMaxCombatRatingBonus },
    { "GetDodgeChance",                 &Script_GetDodgeChance },
    { "GetBlockChance",                 &Script_GetBlockChance },
    { "GetShieldBlock",                 &Script_GetShieldBlock },
    { "GetParryChance",                 &Script_GetParryChance },
    { "GetCritChanceFromAgility",       &Script_GetCritChanceFromAgility },
    { "GetSpellCritChanceFromIntellect", &Script_GetSpellCritChanceFromIntellect },
    { "GetCritChance",                  &Script_GetCritChance },
    { "GetRangedCritChance",            &Script_GetRangedCritChance },
    { "GetSpellCritChance",             &Script_GetSpellCritChance },
    { "GetSpellBonusDamage",            &Script_GetSpellBonusDamage },
    { "GetSpellBonusHealing",           &Script_GetSpellBonusHealing },
    { "GetPetSpellBonusDamage",         &Script_GetPetSpellBonusDamage },
    { "GetSpellPenetration",            &Script_GetSpellPenetration },
    { "GetArmorPenetration",            &Script_GetArmorPenetration },
    { "GetAttackPowerForStat",          &Script_GetAttackPowerForStat },
    { "UnitCreatureType",               &Script_UnitCreatureType },
    { "UnitCreatureFamily",             &Script_UnitCreatureFamily },
    { "GetResSicknessDuration",         &Script_GetResSicknessDuration },
    { "GetPVPSessionStats",             &Script_GetPVPSessionStats },
    { "GetPVPYesterdayStats",           &Script_GetPVPYesterdayStats },
    { "GetPVPLifetimeStats",            &Script_GetPVPLifetimeStats },
    { "UnitPVPRank",                    &Script_UnitPVPRank },
    { "GetPVPRankInfo",                 &Script_GetPVPRankInfo },
    { "GetPVPRankProgress",             &Script_GetPVPRankProgress },
    { "UnitCastingInfo",                &Script_UnitCastingInfo },
    { "UnitChannelInfo",                &Script_UnitChannelInfo },
    { "IsLoggedIn",                     &Script_IsLoggedIn },
    { "IsFlyableArea",                  &Script_IsFlyableArea },
    { "IsIndoors",                      &Script_IsIndoors },
    { "IsOutdoors",                     &Script_IsOutdoors },
    { "IsOutOfBounds",                  &Script_IsOutOfBounds },
    { "IsFalling",                      &Script_IsFalling },
    { "IsSwimming",                     &Script_IsSwimming },
    { "IsFlying",                       &Script_IsFlying },
    { "IsMounted",                      &Script_IsMounted },
    { "IsStealthed",                    &Script_IsStealthed },
    { "UnitIsSameServer",               &Script_UnitIsSameServer },
    { "GetUnitHealthModifier",          &Script_GetUnitHealthModifier },
    { "GetUnitMaxHealthModifier",       &Script_GetUnitMaxHealthModifier },
    { "GetUnitPowerModifier",           &Script_GetUnitPowerModifier },
    { "GetUnitHealthRegenRateFromSpirit", &Script_GetUnitHealthRegenRateFromSpirit },
    { "GetUnitManaRegenRateFromSpirit", &Script_GetUnitManaRegenRateFromSpirit },
    { "GetManaRegen",                   &Script_GetManaRegen },
    { "GetPowerRegen",                  &Script_GetPowerRegen },
    { "GetRuneCooldown",                &Script_GetRuneCooldown },
    { "GetRuneCount",                   &Script_GetRuneCount },
    { "GetRuneType",                    &Script_GetRuneType },
    { "ReportPlayerIsPVPAFK",           &Script_ReportPlayerIsPVPAFK },
    { "PlayerIsPVPInactive",            &Script_PlayerIsPVPInactive },
    { "GetExpertise",                   &Script_GetExpertise },
    { "GetExpertisePercent",            &Script_GetExpertisePercent },
    { "UnitInBattleground",             &Script_UnitInBattleground },
    { "UnitInRange",                    &Script_UnitInRange },
    { "GetUnitSpeed",                   &Script_GetUnitSpeed },
    { "GetUnitPitch",                   &Script_GetUnitPitch },
    { "UnitInVehicle",                  &Script_UnitInVehicle },
    { "UnitUsingVehicle",               &Script_UnitUsingVehicle },
    { "UnitControllingVehicle",         &Script_UnitControllingVehicle },
    { "UnitInVehicleControlSeat",       &Script_UnitInVehicleControlSeat },
    { "UnitHasVehicleUI",               &Script_UnitHasVehicleUI },
    { "UnitTargetsVehicleInRaidUI",     &Script_UnitTargetsVehicleInRaidUI },
    { "UnitVehicleSkin",                &Script_UnitVehicleSkin },
    { "UnitVehicleSeatCount",           &Script_UnitVehicleSeatCount },
    { "UnitVehicleSeatInfo",            &Script_UnitVehicleSeatInfo },
    { "UnitSwitchToVehicleSeat",        &Script_UnitSwitchToVehicleSeat },
    { "CanSwitchVehicleSeat",           &Script_CanSwitchVehicleSeat },
    { "GetVehicleUIIndicator",          &Script_GetVehicleUIIndicator },
    { "GetVehicleUIIndicatorSeat",      &Script_GetVehicleUIIndicatorSeat },
    { "UnitThreatSituation",            &Script_UnitThreatSituation },
    { "UnitDetailedThreatSituation",    &Script_UnitDetailedThreatSituation },
    { "UnitIsControlling",              &Script_UnitIsControlling },
    { "EjectPassengerFromSeat",         &Script_EjectPassengerFromSeat },
    { "CanEjectPassengerFromSeat",      &Script_CanEjectPassengerFromSeat },
    { "RespondInstanceLock",            &Script_RespondInstanceLock },
    { "GetPlayerFacing",                &Script_GetPlayerFacing },
    { "GetPlayerInfoByGUID",            &Script_GetPlayerInfoByGUID },
    { "GetItemStats",                   &Script_GetItemStats },
    { "GetItemStatDelta",               &Script_GetItemStatDelta },
    { "IsXPUserDisabled",               &Script_IsXPUserDisabled },
    { "FillLocalizedClassList",         &Script_FillLocalizedClassList },
};

void ScriptEventsRegisterFunctions() {
    SystemRegisterFunctions();

    for (auto& func : s_UnitFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}

void ScriptEventsInitialize() {
    g_scriptEvents[0] = "UNIT_PET";
    g_scriptEvents[2] = "UNIT_PET";
    g_scriptEvents[12] = "UNIT_TARGET";
    g_scriptEvents[17] = "UNIT_DISPLAYPOWER";
    g_scriptEvents[18] = "UNIT_HEALTH";
    g_scriptEvents[19] = "UNIT_MANA";
    g_scriptEvents[20] = "UNIT_RAGE";
    g_scriptEvents[21] = "UNIT_FOCUS";
    g_scriptEvents[22] = "UNIT_ENERGY";
    g_scriptEvents[23] = "UNIT_HAPPINESS";
    g_scriptEvents[25] = "UNIT_RUNIC_POWER";
    g_scriptEvents[26] = "UNIT_MAXHEALTH";
    g_scriptEvents[27] = "UNIT_MAXMANA";
    g_scriptEvents[28] = "UNIT_MAXRAGE";
    g_scriptEvents[29] = "UNIT_MAXFOCUS";
    g_scriptEvents[30] = "UNIT_MAXENERGY";
    g_scriptEvents[31] = "UNIT_MAXHAPPINESS";
    g_scriptEvents[33] = "UNIT_MAXRUNIC_POWER";
    g_scriptEvents[48] = "UNIT_LEVEL";
    g_scriptEvents[49] = "UNIT_FACTION";
    g_scriptEvents[53] = "UNIT_FLAGS";
    g_scriptEvents[54] = "UNIT_FLAGS";
    g_scriptEvents[56] = "UNIT_ATTACK_SPEED";
    g_scriptEvents[57] = "UNIT_ATTACK_SPEED";
    g_scriptEvents[58] = "UNIT_RANGEDDAMAGE";
    g_scriptEvents[64] = "UNIT_DAMAGE";
    g_scriptEvents[65] = "UNIT_DAMAGE";
    g_scriptEvents[66] = "UNIT_DAMAGE";
    g_scriptEvents[67] = "UNIT_DAMAGE";
    g_scriptEvents[71] = "UNIT_PET_EXPERIENCE";
    g_scriptEvents[72] = "UNIT_PET_EXPERIENCE";
    g_scriptEvents[73] = "UNIT_DYNAMIC_FLAGS";
    g_scriptEvents[78] = "UNIT_STATS";
    g_scriptEvents[79] = "UNIT_STATS";
    g_scriptEvents[80] = "UNIT_STATS";
    g_scriptEvents[81] = "UNIT_STATS";
    g_scriptEvents[82] = "UNIT_STATS";
    g_scriptEvents[93] = "UNIT_RESISTANCES";
    g_scriptEvents[94] = "UNIT_RESISTANCES";
    g_scriptEvents[95] = "UNIT_RESISTANCES";
    g_scriptEvents[96] = "UNIT_RESISTANCES";
    g_scriptEvents[97] = "UNIT_RESISTANCES";
    g_scriptEvents[98] = "UNIT_RESISTANCES";
    g_scriptEvents[99] = "UNIT_RESISTANCES";
    g_scriptEvents[100] = "UNIT_RESISTANCES";
    g_scriptEvents[101] = "UNIT_RESISTANCES";
    g_scriptEvents[102] = "UNIT_RESISTANCES";
    g_scriptEvents[103] = "UNIT_RESISTANCES";
    g_scriptEvents[104] = "UNIT_RESISTANCES";
    g_scriptEvents[105] = "UNIT_RESISTANCES";
    g_scriptEvents[106] = "UNIT_RESISTANCES";
    g_scriptEvents[107] = "UNIT_RESISTANCES";
    g_scriptEvents[108] = "UNIT_RESISTANCES";
    g_scriptEvents[109] = "UNIT_RESISTANCES";
    g_scriptEvents[110] = "UNIT_RESISTANCES";
    g_scriptEvents[111] = "UNIT_RESISTANCES";
    g_scriptEvents[112] = "UNIT_RESISTANCES";
    g_scriptEvents[113] = "UNIT_RESISTANCES";
    g_scriptEvents[117] = "UNIT_ATTACK_POWER";
    g_scriptEvents[118] = "UNIT_ATTACK_POWER";
    g_scriptEvents[119] = "UNIT_ATTACK_POWER";
    g_scriptEvents[120] = "UNIT_RANGED_ATTACK_POWER";
    g_scriptEvents[121] = "UNIT_RANGED_ATTACK_POWER";
    g_scriptEvents[122] = "UNIT_RANGED_ATTACK_POWER";
    g_scriptEvents[123] = "UNIT_RANGEDDAMAGE";
    g_scriptEvents[124] = "UNIT_RANGEDDAMAGE";
    g_scriptEvents[125] = "UNIT_MANA";
    g_scriptEvents[132] = "UNIT_MANA";
    g_scriptEvents[139] = "UNIT_STATS";
    g_scriptEvents[142] = "UNIT_AURA";
    g_scriptEvents[143] = "UNIT_COMBAT";
    g_scriptEvents[144] = "UNIT_NAME_UPDATE";
    g_scriptEvents[145] = "UNIT_PORTRAIT_UPDATE";
    g_scriptEvents[146] = "UNIT_MODEL_CHANGED";
    g_scriptEvents[147] = "UNIT_INVENTORY_CHANGED";
    g_scriptEvents[148] = "UNIT_CLASSIFICATION_CHANGED";
    g_scriptEvents[149] = "UNIT_COMBO_POINTS";
    g_scriptEvents[150] = "ITEM_LOCK_CHANGED";
    g_scriptEvents[151] = "PLAYER_XP_UPDATE";
    g_scriptEvents[152] = "PLAYER_REGEN_DISABLED";
    g_scriptEvents[153] = "PLAYER_REGEN_ENABLED";
    g_scriptEvents[154] = "PLAYER_AURAS_CHANGED";
    g_scriptEvents[155] = "PLAYER_ENTER_COMBAT";
    g_scriptEvents[156] = "PLAYER_LEAVE_COMBAT";
    g_scriptEvents[157] = "PLAYER_TARGET_CHANGED";
    g_scriptEvents[158] = "PLAYER_FOCUS_CHANGED";
    g_scriptEvents[159] = "PLAYER_CONTROL_LOST";
    g_scriptEvents[160] = "PLAYER_CONTROL_GAINED";
    g_scriptEvents[161] = "PLAYER_FARSIGHT_FOCUS_CHANGED";
    g_scriptEvents[162] = "PLAYER_LEVEL_UP";
    g_scriptEvents[163] = "PLAYER_MONEY";
    g_scriptEvents[164] = "PLAYER_DAMAGE_DONE_MODS";
    g_scriptEvents[165] = "PLAYER_TOTEM_UPDATE";
    g_scriptEvents[166] = "ZONE_CHANGED";
    g_scriptEvents[167] = "ZONE_CHANGED_INDOORS";
    g_scriptEvents[168] = "ZONE_CHANGED_NEW_AREA";
    g_scriptEvents[169] = "MINIMAP_UPDATE_ZOOM";
    g_scriptEvents[170] = "MINIMAP_UPDATE_TRACKING";
    g_scriptEvents[171] = "SCREENSHOT_SUCCEEDED";
    g_scriptEvents[172] = "SCREENSHOT_FAILED";
    g_scriptEvents[173] = "ACTIONBAR_SHOWGRID";
    g_scriptEvents[174] = "ACTIONBAR_HIDEGRID";
    g_scriptEvents[SCRIPT_ACTIONBAR_PAGE_CHANGED] = "ACTIONBAR_PAGE_CHANGED";
    g_scriptEvents[SCRIPT_ACTIONBAR_SLOT_CHANGED] = "ACTIONBAR_SLOT_CHANGED";
    g_scriptEvents[177] = "ACTIONBAR_UPDATE_STATE";
    g_scriptEvents[178] = "ACTIONBAR_UPDATE_USABLE";
    g_scriptEvents[179] = "ACTIONBAR_UPDATE_COOLDOWN";
    g_scriptEvents[180] = "UPDATE_BONUS_ACTIONBAR";
    g_scriptEvents[181] = "PARTY_MEMBERS_CHANGED";
    g_scriptEvents[182] = "PARTY_LEADER_CHANGED";
    g_scriptEvents[183] = "PARTY_MEMBER_ENABLE";
    g_scriptEvents[184] = "PARTY_MEMBER_DISABLE";
    g_scriptEvents[185] = "PARTY_LOOT_METHOD_CHANGED";
    g_scriptEvents[186] = "SYSMSG";
    g_scriptEvents[187] = "UI_ERROR_MESSAGE";
    g_scriptEvents[188] = "UI_INFO_MESSAGE";
    g_scriptEvents[189] = "UPDATE_CHAT_COLOR";
    g_scriptEvents[190] = "CHAT_MSG_ADDON";
    g_scriptEvents[191] = "CHAT_MSG_SYSTEM";
    g_scriptEvents[192] = "CHAT_MSG_SAY";
    g_scriptEvents[193] = "CHAT_MSG_PARTY";
    g_scriptEvents[194] = "CHAT_MSG_RAID";
    g_scriptEvents[195] = "CHAT_MSG_GUILD";
    g_scriptEvents[196] = "CHAT_MSG_OFFICER";
    g_scriptEvents[197] = "CHAT_MSG_YELL";
    g_scriptEvents[198] = "CHAT_MSG_WHISPER";
    g_scriptEvents[199] = "CHAT_MSG_WHISPER_INFORM";
    g_scriptEvents[200] = "CHAT_MSG_EMOTE";
    g_scriptEvents[201] = "CHAT_MSG_TEXT_EMOTE";
    g_scriptEvents[202] = "CHAT_MSG_MONSTER_SAY";
    g_scriptEvents[203] = "CHAT_MSG_MONSTER_PARTY";
    g_scriptEvents[204] = "CHAT_MSG_MONSTER_YELL";
    g_scriptEvents[205] = "CHAT_MSG_MONSTER_WHISPER";
    g_scriptEvents[206] = "CHAT_MSG_MONSTER_EMOTE";
    g_scriptEvents[207] = "CHAT_MSG_CHANNEL";
    g_scriptEvents[208] = "CHAT_MSG_CHANNEL_JOIN";
    g_scriptEvents[209] = "CHAT_MSG_CHANNEL_LEAVE";
    g_scriptEvents[210] = "CHAT_MSG_CHANNEL_LIST";
    g_scriptEvents[211] = "CHAT_MSG_CHANNEL_NOTICE";
    g_scriptEvents[212] = "CHAT_MSG_CHANNEL_NOTICE_USER";
    g_scriptEvents[213] = "CHAT_MSG_AFK";
    g_scriptEvents[214] = "CHAT_MSG_DND";
    g_scriptEvents[215] = "CHAT_MSG_IGNORED";
    g_scriptEvents[216] = "CHAT_MSG_SKILL";
    g_scriptEvents[217] = "CHAT_MSG_LOOT";
    g_scriptEvents[218] = "CHAT_MSG_MONEY";
    g_scriptEvents[219] = "CHAT_MSG_OPENING";
    g_scriptEvents[220] = "CHAT_MSG_TRADESKILLS";
    g_scriptEvents[221] = "CHAT_MSG_PET_INFO";
    g_scriptEvents[222] = "CHAT_MSG_COMBAT_MISC_INFO";
    g_scriptEvents[223] = "CHAT_MSG_COMBAT_XP_GAIN";
    g_scriptEvents[224] = "CHAT_MSG_COMBAT_HONOR_GAIN";
    g_scriptEvents[225] = "CHAT_MSG_COMBAT_FACTION_CHANGE";
    g_scriptEvents[226] = "CHAT_MSG_BG_SYSTEM_NEUTRAL";
    g_scriptEvents[227] = "CHAT_MSG_BG_SYSTEM_ALLIANCE";
    g_scriptEvents[228] = "CHAT_MSG_BG_SYSTEM_HORDE";
    g_scriptEvents[229] = "CHAT_MSG_RAID_LEADER";
    g_scriptEvents[230] = "CHAT_MSG_RAID_WARNING";
    g_scriptEvents[231] = "CHAT_MSG_RAID_BOSS_WHISPER";
    g_scriptEvents[232] = "CHAT_MSG_RAID_BOSS_EMOTE";
    g_scriptEvents[233] = "CHAT_MSG_FILTERED";
    g_scriptEvents[234] = "CHAT_MSG_BATTLEGROUND";
    g_scriptEvents[235] = "CHAT_MSG_BATTLEGROUND_LEADER";
    g_scriptEvents[236] = "CHAT_MSG_RESTRICTED";
    g_scriptEvents[237] = "";
    g_scriptEvents[238] = "CHAT_MSG_ACHIEVEMENT";
    g_scriptEvents[239] = "CHAT_MSG_GUILD_ACHIEVEMENT";
    g_scriptEvents[240] = "LANGUAGE_LIST_CHANGED";
    g_scriptEvents[241] = "TIME_PLAYED_MSG";
    g_scriptEvents[242] = "SPELLS_CHANGED";
    g_scriptEvents[243] = "CURRENT_SPELL_CAST_CHANGED";
    g_scriptEvents[244] = "SPELL_UPDATE_COOLDOWN";
    g_scriptEvents[245] = "SPELL_UPDATE_USABLE";
    g_scriptEvents[246] = "CHARACTER_POINTS_CHANGED";
    g_scriptEvents[247] = "SKILL_LINES_CHANGED";
    g_scriptEvents[248] = "ITEM_PUSH";
    g_scriptEvents[249] = "LOOT_OPENED";
    g_scriptEvents[250] = "LOOT_SLOT_CLEARED";
    g_scriptEvents[251] = "LOOT_SLOT_CHANGED";
    g_scriptEvents[252] = "LOOT_CLOSED";
    g_scriptEvents[SCRIPT_PLAYER_LOGIN] = "PLAYER_LOGIN";
    g_scriptEvents[SCRIPT_PLAYER_LOGOUT] = "PLAYER_LOGOUT";
    g_scriptEvents[SCRIPT_PLAYER_ENTERING_WORLD] = "PLAYER_ENTERING_WORLD";
    g_scriptEvents[256] = "PLAYER_LEAVING_WORLD";
    g_scriptEvents[257] = "PLAYER_ALIVE";
    g_scriptEvents[258] = "PLAYER_DEAD";
    g_scriptEvents[259] = "PLAYER_CAMPING";
    g_scriptEvents[260] = "PLAYER_QUITING";
    g_scriptEvents[261] = "LOGOUT_CANCEL";
    g_scriptEvents[262] = "RESURRECT_REQUEST";
    g_scriptEvents[263] = "PARTY_INVITE_REQUEST";
    g_scriptEvents[264] = "PARTY_INVITE_CANCEL";
    g_scriptEvents[265] = "GUILD_INVITE_REQUEST";
    g_scriptEvents[266] = "GUILD_INVITE_CANCEL";
    g_scriptEvents[267] = "GUILD_MOTD";
    g_scriptEvents[268] = "TRADE_REQUEST";
    g_scriptEvents[269] = "TRADE_REQUEST_CANCEL";
    g_scriptEvents[270] = "LOOT_BIND_CONFIRM";
    g_scriptEvents[271] = "EQUIP_BIND_CONFIRM";
    g_scriptEvents[272] = "AUTOEQUIP_BIND_CONFIRM";
    g_scriptEvents[273] = "USE_BIND_CONFIRM";
    g_scriptEvents[274] = "DELETE_ITEM_CONFIRM";
    g_scriptEvents[275] = "CURSOR_UPDATE";
    g_scriptEvents[276] = "ITEM_TEXT_BEGIN";
    g_scriptEvents[277] = "ITEM_TEXT_TRANSLATION";
    g_scriptEvents[278] = "ITEM_TEXT_READY";
    g_scriptEvents[279] = "ITEM_TEXT_CLOSED";
    g_scriptEvents[280] = "GOSSIP_SHOW";
    g_scriptEvents[281] = "GOSSIP_CONFIRM";
    g_scriptEvents[282] = "GOSSIP_CONFIRM_CANCEL";
    g_scriptEvents[283] = "GOSSIP_ENTER_CODE";
    g_scriptEvents[284] = "GOSSIP_CLOSED";
    g_scriptEvents[285] = "QUEST_GREETING";
    g_scriptEvents[286] = "QUEST_DETAIL";
    g_scriptEvents[287] = "QUEST_PROGRESS";
    g_scriptEvents[288] = "QUEST_COMPLETE";
    g_scriptEvents[289] = "QUEST_FINISHED";
    g_scriptEvents[290] = "QUEST_ITEM_UPDATE";
    g_scriptEvents[291] = "TAXIMAP_OPENED";
    g_scriptEvents[292] = "TAXIMAP_CLOSED";
    g_scriptEvents[293] = "QUEST_LOG_UPDATE";
    g_scriptEvents[294] = "TRAINER_SHOW";
    g_scriptEvents[295] = "TRAINER_UPDATE";
    g_scriptEvents[296] = "TRAINER_DESCRIPTION_UPDATE";
    g_scriptEvents[297] = "TRAINER_CLOSED";
    g_scriptEvents[SCRIPT_CVAR_UPDATE] = "CVAR_UPDATE";
    g_scriptEvents[299] = "TRADE_SKILL_SHOW";
    g_scriptEvents[300] = "TRADE_SKILL_UPDATE";
    g_scriptEvents[301] = "TRADE_SKILL_CLOSE";
    g_scriptEvents[302] = "MERCHANT_SHOW";
    g_scriptEvents[303] = "MERCHANT_UPDATE";
    g_scriptEvents[304] = "MERCHANT_CLOSED";
    g_scriptEvents[305] = "TRADE_SHOW";
    g_scriptEvents[306] = "TRADE_CLOSED";
    g_scriptEvents[307] = "TRADE_UPDATE";
    g_scriptEvents[308] = "TRADE_ACCEPT_UPDATE";
    g_scriptEvents[309] = "TRADE_TARGET_ITEM_CHANGED";
    g_scriptEvents[310] = "TRADE_PLAYER_ITEM_CHANGED";
    g_scriptEvents[311] = "TRADE_MONEY_CHANGED";
    g_scriptEvents[312] = "PLAYER_TRADE_MONEY";
    g_scriptEvents[313] = "BAG_OPEN";
    g_scriptEvents[314] = "BAG_UPDATE";
    g_scriptEvents[315] = "BAG_CLOSED";
    g_scriptEvents[316] = "BAG_UPDATE_COOLDOWN";
    g_scriptEvents[317] = "LOCALPLAYER_PET_RENAMED";
    g_scriptEvents[318] = "UNIT_ATTACK";
    g_scriptEvents[319] = "UNIT_DEFENSE";
    g_scriptEvents[320] = "PET_ATTACK_START";
    g_scriptEvents[321] = "PET_ATTACK_STOP";
    g_scriptEvents[322] = "UPDATE_MOUSEOVER_UNIT";
    g_scriptEvents[323] = "UNIT_SPELLCAST_SENT";
    g_scriptEvents[324] = "UNIT_SPELLCAST_START";
    g_scriptEvents[325] = "UNIT_SPELLCAST_STOP";
    g_scriptEvents[326] = "UNIT_SPELLCAST_FAILED";
    g_scriptEvents[327] = "UNIT_SPELLCAST_FAILED_QUIET";
    g_scriptEvents[328] = "UNIT_SPELLCAST_INTERRUPTED";
    g_scriptEvents[329] = "UNIT_SPELLCAST_DELAYED";
    g_scriptEvents[330] = "UNIT_SPELLCAST_SUCCEEDED";
    g_scriptEvents[331] = "UNIT_SPELLCAST_CHANNEL_START";
    g_scriptEvents[332] = "UNIT_SPELLCAST_CHANNEL_UPDATE";
    g_scriptEvents[333] = "UNIT_SPELLCAST_CHANNEL_STOP";
    g_scriptEvents[334] = "UNIT_SPELLCAST_INTERRUPTIBLE";
    g_scriptEvents[335] = "UNIT_SPELLCAST_NOT_INTERRUPTIBLE";
    g_scriptEvents[336] = "PLAYER_GUILD_UPDATE";
    g_scriptEvents[337] = "QUEST_ACCEPT_CONFIRM";
    g_scriptEvents[338] = "PLAYERBANKSLOTS_CHANGED";
    g_scriptEvents[339] = "BANKFRAME_OPENED";
    g_scriptEvents[340] = "BANKFRAME_CLOSED";
    g_scriptEvents[341] = "PLAYERBANKBAGSLOTS_CHANGED";
    g_scriptEvents[342] = "FRIENDLIST_UPDATE";
    g_scriptEvents[343] = "IGNORELIST_UPDATE";
    g_scriptEvents[344] = "MUTELIST_UPDATE";
    g_scriptEvents[345] = "PET_BAR_UPDATE";
    g_scriptEvents[346] = "PET_BAR_UPDATE_COOLDOWN";
    g_scriptEvents[347] = "PET_BAR_SHOWGRID";
    g_scriptEvents[348] = "PET_BAR_HIDEGRID";
    g_scriptEvents[349] = "PET_BAR_HIDE";
    g_scriptEvents[350] = "PET_BAR_UPDATE_USABLE";
    g_scriptEvents[351] = "MINIMAP_PING";
    g_scriptEvents[352] = "MIRROR_TIMER_START";
    g_scriptEvents[353] = "MIRROR_TIMER_PAUSE";
    g_scriptEvents[354] = "MIRROR_TIMER_STOP";
    g_scriptEvents[355] = "WORLD_MAP_UPDATE";
    g_scriptEvents[356] = "WORLD_MAP_NAME_UPDATE";
    g_scriptEvents[357] = "AUTOFOLLOW_BEGIN";
    g_scriptEvents[358] = "AUTOFOLLOW_END";
    g_scriptEvents[360] = "CINEMATIC_START";
    g_scriptEvents[361] = "CINEMATIC_STOP";
    g_scriptEvents[362] = "UPDATE_FACTION";
    g_scriptEvents[363] = "CLOSE_WORLD_MAP";
    g_scriptEvents[364] = "OPEN_TABARD_FRAME";
    g_scriptEvents[365] = "CLOSE_TABARD_FRAME";
    g_scriptEvents[366] = "TABARD_CANSAVE_CHANGED";
    g_scriptEvents[367] = "GUILD_REGISTRAR_SHOW";
    g_scriptEvents[368] = "GUILD_REGISTRAR_CLOSED";
    g_scriptEvents[369] = "DUEL_REQUESTED";
    g_scriptEvents[370] = "DUEL_OUTOFBOUNDS";
    g_scriptEvents[371] = "DUEL_INBOUNDS";
    g_scriptEvents[372] = "DUEL_FINISHED";
    g_scriptEvents[373] = "TUTORIAL_TRIGGER";
    g_scriptEvents[374] = "PET_DISMISS_START";
    g_scriptEvents[375] = "UPDATE_BINDINGS";
    g_scriptEvents[376] = "UPDATE_SHAPESHIFT_FORMS";
    g_scriptEvents[377] = "UPDATE_SHAPESHIFT_FORM";
    g_scriptEvents[378] = "UPDATE_SHAPESHIFT_USABLE";
    g_scriptEvents[379] = "UPDATE_SHAPESHIFT_COOLDOWN";
    g_scriptEvents[380] = "WHO_LIST_UPDATE";
    g_scriptEvents[381] = "PETITION_SHOW";
    g_scriptEvents[382] = "PETITION_CLOSED";
    g_scriptEvents[383] = "EXECUTE_CHAT_LINE";
    g_scriptEvents[384] = "UPDATE_MACROS";
    g_scriptEvents[385] = "UPDATE_TICKET";
    g_scriptEvents[386] = "UPDATE_CHAT_WINDOWS";
    g_scriptEvents[387] = "CONFIRM_XP_LOSS";
    g_scriptEvents[388] = "CORPSE_IN_RANGE";
    g_scriptEvents[389] = "CORPSE_IN_INSTANCE";
    g_scriptEvents[390] = "CORPSE_OUT_OF_RANGE";
    g_scriptEvents[391] = "UPDATE_GM_STATUS";
    g_scriptEvents[392] = "PLAYER_UNGHOST";
    g_scriptEvents[393] = "BIND_ENCHANT";
    g_scriptEvents[394] = "REPLACE_ENCHANT";
    g_scriptEvents[395] = "TRADE_REPLACE_ENCHANT";
    g_scriptEvents[396] = "TRADE_POTENTIAL_BIND_ENCHANT";
    g_scriptEvents[397] = "PLAYER_UPDATE_RESTING";
    g_scriptEvents[398] = "UPDATE_EXHAUSTION";
    g_scriptEvents[399] = "PLAYER_FLAGS_CHANGED";
    g_scriptEvents[400] = "GUILD_ROSTER_UPDATE";
    g_scriptEvents[401] = "GM_PLAYER_INFO";
    g_scriptEvents[402] = "MAIL_SHOW";
    g_scriptEvents[403] = "MAIL_CLOSED";
    g_scriptEvents[404] = "SEND_MAIL_MONEY_CHANGED";
    g_scriptEvents[405] = "SEND_MAIL_COD_CHANGED";
    g_scriptEvents[406] = "MAIL_SEND_INFO_UPDATE";
    g_scriptEvents[407] = "MAIL_SEND_SUCCESS";
    g_scriptEvents[408] = "MAIL_INBOX_UPDATE";
    g_scriptEvents[409] = "MAIL_LOCK_SEND_ITEMS";
    g_scriptEvents[410] = "MAIL_UNLOCK_SEND_ITEMS";
    g_scriptEvents[411] = "BATTLEFIELDS_SHOW";
    g_scriptEvents[412] = "BATTLEFIELDS_CLOSED";
    g_scriptEvents[413] = "UPDATE_BATTLEFIELD_STATUS";
    g_scriptEvents[414] = "UPDATE_BATTLEFIELD_SCORE";
    g_scriptEvents[415] = "AUCTION_HOUSE_SHOW";
    g_scriptEvents[416] = "AUCTION_HOUSE_CLOSED";
    g_scriptEvents[417] = "NEW_AUCTION_UPDATE";
    g_scriptEvents[418] = "AUCTION_ITEM_LIST_UPDATE";
    g_scriptEvents[419] = "AUCTION_OWNED_LIST_UPDATE";
    g_scriptEvents[420] = "AUCTION_BIDDER_LIST_UPDATE";
    g_scriptEvents[421] = "PET_UI_UPDATE";
    g_scriptEvents[422] = "PET_UI_CLOSE";
    g_scriptEvents[423] = "ADDON_LOADED";
    g_scriptEvents[424] = "VARIABLES_LOADED";
    g_scriptEvents[425] = "MACRO_ACTION_FORBIDDEN";
    g_scriptEvents[426] = "ADDON_ACTION_FORBIDDEN";
    g_scriptEvents[427] = "MACRO_ACTION_BLOCKED";
    g_scriptEvents[428] = "ADDON_ACTION_BLOCKED";
    g_scriptEvents[429] = "START_AUTOREPEAT_SPELL";
    g_scriptEvents[430] = "STOP_AUTOREPEAT_SPELL";
    g_scriptEvents[431] = "PET_STABLE_SHOW";
    g_scriptEvents[432] = "PET_STABLE_UPDATE";
    g_scriptEvents[433] = "PET_STABLE_UPDATE_PAPERDOLL";
    g_scriptEvents[434] = "PET_STABLE_CLOSED";
    g_scriptEvents[435] = "RAID_ROSTER_UPDATE";
    g_scriptEvents[436] = "UPDATE_PENDING_MAIL";
    g_scriptEvents[437] = "UPDATE_INVENTORY_ALERTS";
    g_scriptEvents[438] = "UPDATE_INVENTORY_DURABILITY";
    g_scriptEvents[439] = "UPDATE_TRADESKILL_RECAST";
    g_scriptEvents[440] = "OPEN_MASTER_LOOT_LIST";
    g_scriptEvents[441] = "UPDATE_MASTER_LOOT_LIST";
    g_scriptEvents[442] = "START_LOOT_ROLL";
    g_scriptEvents[443] = "CANCEL_LOOT_ROLL";
    g_scriptEvents[444] = "CONFIRM_LOOT_ROLL";
    g_scriptEvents[445] = "CONFIRM_DISENCHANT_ROLL";
    g_scriptEvents[446] = "INSTANCE_BOOT_START";
    g_scriptEvents[447] = "INSTANCE_BOOT_STOP";
    g_scriptEvents[448] = "LEARNED_SPELL_IN_TAB";
    g_scriptEvents[449] = "DISPLAY_SIZE_CHANGED";
    g_scriptEvents[450] = "CONFIRM_TALENT_WIPE";
    g_scriptEvents[451] = "CONFIRM_BINDER";
    g_scriptEvents[452] = "MAIL_FAILED";
    g_scriptEvents[453] = "CLOSE_INBOX_ITEM";
    g_scriptEvents[454] = "CONFIRM_SUMMON";
    g_scriptEvents[455] = "CANCEL_SUMMON";
    g_scriptEvents[456] = "BILLING_NAG_DIALOG";
    g_scriptEvents[457] = "IGR_BILLING_NAG_DIALOG";
    g_scriptEvents[458] = "PLAYER_SKINNED";
    g_scriptEvents[459] = "TABARD_SAVE_PENDING";
    g_scriptEvents[460] = "UNIT_QUEST_LOG_CHANGED";
    g_scriptEvents[461] = "PLAYER_PVP_KILLS_CHANGED";
    g_scriptEvents[462] = "PLAYER_PVP_RANK_CHANGED";
    g_scriptEvents[463] = "INSPECT_HONOR_UPDATE";
    g_scriptEvents[464] = "UPDATE_WORLD_STATES";
    g_scriptEvents[465] = "AREA_SPIRIT_HEALER_IN_RANGE";
    g_scriptEvents[466] = "AREA_SPIRIT_HEALER_OUT_OF_RANGE";
    g_scriptEvents[467] = "PLAYTIME_CHANGED";
    g_scriptEvents[468] = "UPDATE_LFG_TYPES";
    g_scriptEvents[469] = "UPDATE_LFG_LIST";
    g_scriptEvents[470] = "UPDATE_LFG_LIST_INCREMENTAL";
    g_scriptEvents[471] = "START_MINIGAME";
    g_scriptEvents[472] = "MINIGAME_UPDATE";
    g_scriptEvents[473] = "READY_CHECK";
    g_scriptEvents[474] = "READY_CHECK_CONFIRM";
    g_scriptEvents[475] = "READY_CHECK_FINISHED";
    g_scriptEvents[476] = "RAID_TARGET_UPDATE";
    g_scriptEvents[477] = "GMSURVEY_DISPLAY";
    g_scriptEvents[478] = "UPDATE_INSTANCE_INFO";
    g_scriptEvents[479] = "SOCKET_INFO_UPDATE";
    g_scriptEvents[480] = "SOCKET_INFO_CLOSE";
    g_scriptEvents[481] = "PETITION_VENDOR_SHOW";
    g_scriptEvents[482] = "PETITION_VENDOR_CLOSED";
    g_scriptEvents[483] = "PETITION_VENDOR_UPDATE";
    g_scriptEvents[484] = "COMBAT_TEXT_UPDATE";
    g_scriptEvents[485] = "QUEST_WATCH_UPDATE";
    g_scriptEvents[486] = "KNOWLEDGE_BASE_SETUP_LOAD_SUCCESS";
    g_scriptEvents[487] = "KNOWLEDGE_BASE_SETUP_LOAD_FAILURE";
    g_scriptEvents[488] = "KNOWLEDGE_BASE_QUERY_LOAD_SUCCESS";
    g_scriptEvents[489] = "KNOWLEDGE_BASE_QUERY_LOAD_FAILURE";
    g_scriptEvents[490] = "KNOWLEDGE_BASE_ARTICLE_LOAD_SUCCESS";
    g_scriptEvents[491] = "KNOWLEDGE_BASE_ARTICLE_LOAD_FAILURE";
    g_scriptEvents[492] = "KNOWLEDGE_BASE_SYSTEM_MOTD_UPDATED";
    g_scriptEvents[493] = "KNOWLEDGE_BASE_SERVER_MESSAGE";
    g_scriptEvents[494] = "ARENA_TEAM_UPDATE";
    g_scriptEvents[495] = "ARENA_TEAM_ROSTER_UPDATE";
    g_scriptEvents[496] = "ARENA_TEAM_INVITE_REQUEST";
    g_scriptEvents[497] = "HONOR_CURRENCY_UPDATE";
    g_scriptEvents[498] = "KNOWN_TITLES_UPDATE";
    g_scriptEvents[499] = "NEW_TITLE_EARNED";
    g_scriptEvents[500] = "OLD_TITLE_LOST";
    g_scriptEvents[501] = "LFG_UPDATE";
    g_scriptEvents[502] = "LFG_PROPOSAL_UPDATE";
    g_scriptEvents[503] = "LFG_PROPOSAL_SHOW";
    g_scriptEvents[504] = "LFG_PROPOSAL_FAILED";
    g_scriptEvents[505] = "LFG_PROPOSAL_SUCCEEDED";
    g_scriptEvents[506] = "LFG_ROLE_UPDATE";
    g_scriptEvents[507] = "LFG_ROLE_CHECK_UPDATE";
    g_scriptEvents[508] = "LFG_ROLE_CHECK_SHOW";
    g_scriptEvents[509] = "LFG_ROLE_CHECK_HIDE";
    g_scriptEvents[510] = "LFG_ROLE_CHECK_ROLE_CHOSEN";
    g_scriptEvents[511] = "LFG_QUEUE_STATUS_UPDATE";
    g_scriptEvents[512] = "LFG_BOOT_PROPOSAL_UPDATE";
    g_scriptEvents[513] = "LFG_LOCK_INFO_RECEIVED";
    g_scriptEvents[514] = "LFG_UPDATE_RANDOM_INFO";
    g_scriptEvents[515] = "LFG_OFFER_CONTINUE";
    g_scriptEvents[516] = "LFG_OPEN_FROM_GOSSIP";
    g_scriptEvents[517] = "LFG_COMPLETION_REWARD";
    g_scriptEvents[518] = "PARTY_LFG_RESTRICTED";
    g_scriptEvents[519] = "PLAYER_ROLES_ASSIGNED";
    g_scriptEvents[520] = "COMBAT_RATING_UPDATE";
    g_scriptEvents[521] = "MODIFIER_STATE_CHANGED";
    g_scriptEvents[522] = "UPDATE_STEALTH";
    g_scriptEvents[523] = "ENABLE_TAXI_BENCHMARK";
    g_scriptEvents[524] = "DISABLE_TAXI_BENCHMARK";
    g_scriptEvents[525] = "VOICE_START";
    g_scriptEvents[526] = "VOICE_STOP";
    g_scriptEvents[527] = "VOICE_STATUS_UPDATE";
    g_scriptEvents[528] = "VOICE_CHANNEL_STATUS_UPDATE";
    g_scriptEvents[529] = "UPDATE_FLOATING_CHAT_WINDOWS";
    g_scriptEvents[530] = "RAID_INSTANCE_WELCOME";
    g_scriptEvents[531] = "MOVIE_RECORDING_PROGRESS";
    g_scriptEvents[532] = "MOVIE_COMPRESSING_PROGRESS";
    g_scriptEvents[533] = "MOVIE_UNCOMPRESSED_MOVIE";
    g_scriptEvents[534] = "VOICE_PUSH_TO_TALK_START";
    g_scriptEvents[535] = "VOICE_PUSH_TO_TALK_STOP";
    g_scriptEvents[536] = "GUILDBANKFRAME_OPENED";
    g_scriptEvents[537] = "GUILDBANKFRAME_CLOSED";
    g_scriptEvents[538] = "GUILDBANKBAGSLOTS_CHANGED";
    g_scriptEvents[539] = "GUILDBANK_ITEM_LOCK_CHANGED";
    g_scriptEvents[540] = "GUILDBANK_UPDATE_TABS";
    g_scriptEvents[541] = "GUILDBANK_UPDATE_MONEY";
    g_scriptEvents[542] = "GUILDBANKLOG_UPDATE";
    g_scriptEvents[543] = "GUILDBANK_UPDATE_WITHDRAWMONEY";
    g_scriptEvents[544] = "GUILDBANK_UPDATE_TEXT";
    g_scriptEvents[545] = "GUILDBANK_TEXT_CHANGED";
    g_scriptEvents[546] = "CHANNEL_UI_UPDATE";
    g_scriptEvents[547] = "CHANNEL_COUNT_UPDATE";
    g_scriptEvents[548] = "CHANNEL_ROSTER_UPDATE";
    g_scriptEvents[549] = "CHANNEL_VOICE_UPDATE";
    g_scriptEvents[550] = "CHANNEL_INVITE_REQUEST";
    g_scriptEvents[551] = "CHANNEL_PASSWORD_REQUEST";
    g_scriptEvents[552] = "CHANNEL_FLAGS_UPDATED";
    g_scriptEvents[553] = "VOICE_SESSIONS_UPDATE";
    g_scriptEvents[554] = "VOICE_CHAT_ENABLED_UPDATE";
    g_scriptEvents[555] = "VOICE_LEFT_SESSION";
    g_scriptEvents[556] = "INSPECT_TALENT_READY";
    g_scriptEvents[557] = "VOICE_SELF_MUTE";
    g_scriptEvents[558] = "VOICE_PLATE_START";
    g_scriptEvents[559] = "VOICE_PLATE_STOP";
    g_scriptEvents[560] = "ARENA_SEASON_WORLD_STATE";
    g_scriptEvents[561] = "GUILD_EVENT_LOG_UPDATE";
    g_scriptEvents[562] = "GUILDTABARD_UPDATE";
    g_scriptEvents[563] = "SOUND_DEVICE_UPDATE";
    g_scriptEvents[564] = "COMMENTATOR_MAP_UPDATE";
    g_scriptEvents[565] = "COMMENTATOR_ENTER_WORLD";
    g_scriptEvents[566] = "COMBAT_LOG_EVENT";
    g_scriptEvents[567] = "COMBAT_LOG_EVENT_UNFILTERED";
    g_scriptEvents[568] = "COMMENTATOR_PLAYER_UPDATE";
    g_scriptEvents[569] = "PLAYER_ENTERING_BATTLEGROUND";
    g_scriptEvents[570] = "BARBER_SHOP_OPEN";
    g_scriptEvents[571] = "BARBER_SHOP_CLOSE";
    g_scriptEvents[572] = "BARBER_SHOP_SUCCESS";
    g_scriptEvents[573] = "BARBER_SHOP_APPEARANCE_APPLIED";
    g_scriptEvents[574] = "CALENDAR_UPDATE_INVITE_LIST";
    g_scriptEvents[575] = "CALENDAR_UPDATE_EVENT_LIST";
    g_scriptEvents[576] = "CALENDAR_NEW_EVENT";
    g_scriptEvents[577] = "CALENDAR_OPEN_EVENT";
    g_scriptEvents[578] = "CALENDAR_CLOSE_EVENT";
    g_scriptEvents[579] = "CALENDAR_UPDATE_EVENT";
    g_scriptEvents[580] = "CALENDAR_UPDATE_PENDING_INVITES";
    g_scriptEvents[581] = "CALENDAR_EVENT_ALARM";
    g_scriptEvents[582] = "CALENDAR_UPDATE_ERROR";
    g_scriptEvents[583] = "CALENDAR_ACTION_PENDING";
    g_scriptEvents[584] = "VEHICLE_ANGLE_SHOW";
    g_scriptEvents[585] = "VEHICLE_ANGLE_UPDATE";
    g_scriptEvents[586] = "VEHICLE_POWER_SHOW";
    g_scriptEvents[587] = "UNIT_ENTERING_VEHICLE";
    g_scriptEvents[588] = "UNIT_ENTERED_VEHICLE";
    g_scriptEvents[589] = "UNIT_EXITING_VEHICLE";
    g_scriptEvents[590] = "UNIT_EXITED_VEHICLE";
    g_scriptEvents[591] = "VEHICLE_PASSENGERS_CHANGED";
    g_scriptEvents[592] = "PLAYER_GAINS_VEHICLE_DATA";
    g_scriptEvents[593] = "PLAYER_LOSES_VEHICLE_DATA";
    g_scriptEvents[594] = "PET_FORCE_NAME_DECLENSION";
    g_scriptEvents[595] = "LEVEL_GRANT_PROPOSED";
    g_scriptEvents[596] = "SYNCHRONIZE_SETTINGS";
    g_scriptEvents[597] = "PLAY_MOVIE";
    g_scriptEvents[598] = "RUNE_POWER_UPDATE";
    g_scriptEvents[599] = "RUNE_TYPE_UPDATE";
    g_scriptEvents[600] = "ACHIEVEMENT_EARNED";
    g_scriptEvents[601] = "CRITERIA_UPDATE";
    g_scriptEvents[602] = "RECEIVED_ACHIEVEMENT_LIST";
    g_scriptEvents[603] = "PET_RENAMEABLE";
    g_scriptEvents[604] = "KNOWN_CURRENCY_TYPES_UPDATE";
    g_scriptEvents[605] = "CURRENCY_DISPLAY_UPDATE";
    g_scriptEvents[606] = "COMPANION_LEARNED";
    g_scriptEvents[607] = "COMPANION_UNLEARNED";
    g_scriptEvents[608] = "COMPANION_UPDATE";
    g_scriptEvents[609] = "UNIT_THREAT_LIST_UPDATE";
    g_scriptEvents[610] = "UNIT_THREAT_SITUATION_UPDATE";
    g_scriptEvents[611] = "GLYPH_ADDED";
    g_scriptEvents[612] = "GLYPH_REMOVED";
    g_scriptEvents[613] = "GLYPH_UPDATED";
    g_scriptEvents[614] = "GLYPH_ENABLED";
    g_scriptEvents[615] = "GLYPH_DISABLED";
    g_scriptEvents[616] = "USE_GLYPH";
    g_scriptEvents[617] = "TRACKED_ACHIEVEMENT_UPDATE";
    g_scriptEvents[618] = "ARENA_OPPONENT_UPDATE";
    g_scriptEvents[619] = "INSPECT_ACHIEVEMENT_READY";
    g_scriptEvents[620] = "RAISED_AS_GHOUL";
    g_scriptEvents[621] = "PARTY_CONVERTED_TO_RAID";
    g_scriptEvents[622] = "PVPQUEUE_ANYWHERE_SHOW";
    g_scriptEvents[623] = "PVPQUEUE_ANYWHERE_UPDATE_AVAILABLE";
    g_scriptEvents[624] = "QUEST_ACCEPTED";
    g_scriptEvents[625] = "PLAYER_TALENT_UPDATE";
    g_scriptEvents[626] = "ACTIVE_TALENT_GROUP_CHANGED";
    g_scriptEvents[627] = "PET_TALENT_UPDATE";
    g_scriptEvents[628] = "PREVIEW_TALENT_POINTS_CHANGED";
    g_scriptEvents[629] = "PREVIEW_PET_TALENT_POINTS_CHANGED";
    g_scriptEvents[630] = "WEAR_EQUIPMENT_SET";
    g_scriptEvents[631] = "EQUIPMENT_SETS_CHANGED";
    g_scriptEvents[632] = "INSTANCE_LOCK_START";
    g_scriptEvents[633] = "INSTANCE_LOCK_STOP";
    g_scriptEvents[634] = "PLAYER_EQUIPMENT_CHANGED";
    g_scriptEvents[635] = "ITEM_LOCKED";
    g_scriptEvents[636] = "ITEM_UNLOCKED";
    g_scriptEvents[637] = "TRADE_SKILL_FILTER_UPDATE";
    g_scriptEvents[638] = "EQUIPMENT_SWAP_PENDING";
    g_scriptEvents[639] = "EQUIPMENT_SWAP_FINISHED";
    g_scriptEvents[640] = "NPC_PVPQUEUE_ANYWHERE";
    g_scriptEvents[641] = "UPDATE_MULTI_CAST_ACTIONBAR";
    g_scriptEvents[642] = "ENABLE_XP_GAIN";
    g_scriptEvents[643] = "DISABLE_XP_GAIN";
    g_scriptEvents[644] = "BATTLEFIELD_MGR_ENTRY_INVITE";
    g_scriptEvents[645] = "BATTLEFIELD_MGR_ENTERED";
    g_scriptEvents[646] = "BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE";
    g_scriptEvents[647] = "BATTLEFIELD_MGR_EJECT_PENDING";
    g_scriptEvents[648] = "BATTLEFIELD_MGR_EJECTED";
    g_scriptEvents[649] = "BATTLEFIELD_MGR_QUEUE_INVITE";
    g_scriptEvents[650] = "BATTLEFIELD_MGR_STATE_CHANGE";
    g_scriptEvents[651] = "WORLD_STATE_UI_TIMER_UPDATE";
    g_scriptEvents[652] = "END_REFUND";
    g_scriptEvents[653] = "END_BOUND_TRADEABLE";
    g_scriptEvents[654] = "UPDATE_CHAT_COLOR_NAME_BY_CLASS";
    g_scriptEvents[655] = "GMRESPONSE_RECEIVED";
    g_scriptEvents[656] = "VEHICLE_UPDATE";
    g_scriptEvents[657] = "WOW_MOUSE_NOT_FOUND";
    g_scriptEvents[659] = "MAIL_SUCCESS";
    g_scriptEvents[660] = "TALENTS_INVOLUNTARILY_RESET";
    g_scriptEvents[661] = "INSTANCE_ENCOUNTER_ENGAGE_UNIT";
    g_scriptEvents[662] = "QUEST_QUERY_COMPLETE";
    g_scriptEvents[663] = "QUEST_POI_UPDATE";
    g_scriptEvents[664] = "PLAYER_DIFFICULTY_CHANGED";
    g_scriptEvents[665] = "CHAT_MSG_PARTY_LEADER";
    g_scriptEvents[666] = "VOTE_KICK_REASON_NEEDED";
    g_scriptEvents[667] = "ENABLE_LOW_LEVEL_RAID";
    g_scriptEvents[668] = "DISABLE_LOW_LEVEL_RAID";
    g_scriptEvents[669] = "CHAT_MSG_TARGETICONS";
    g_scriptEvents[670] = "AUCTION_HOUSE_DISABLED";
    g_scriptEvents[671] = "AUCTION_MULTISELL_START";
    g_scriptEvents[672] = "AUCTION_MULTISELL_UPDATE";
    g_scriptEvents[673] = "AUCTION_MULTISELL_FAILURE";
    g_scriptEvents[674] = "PET_SPELL_POWER_UPDATE";
    g_scriptEvents[675] = "BN_CONNECTED";
    g_scriptEvents[676] = "BN_DISCONNECTED";
    g_scriptEvents[677] = "BN_SELF_ONLINE";
    g_scriptEvents[678] = "BN_SELF_OFFLINE";
    g_scriptEvents[679] = "BN_FRIEND_LIST_SIZE_CHANGED";
    g_scriptEvents[680] = "BN_FRIEND_INVITE_LIST_INITIALIZED";
    g_scriptEvents[681] = "BN_FRIEND_INVITE_SEND_RESULT";
    g_scriptEvents[682] = "BN_FRIEND_INVITE_ADDED";
    g_scriptEvents[683] = "BN_FRIEND_INVITE_REMOVED";
    g_scriptEvents[684] = "BN_FRIEND_INFO_CHANGED";
    g_scriptEvents[685] = "BN_CUSTOM_MESSAGE_CHANGED";
    g_scriptEvents[686] = "BN_CUSTOM_MESSAGE_LOADED";
    g_scriptEvents[687] = "CHAT_MSG_BN_WHISPER";
    g_scriptEvents[688] = "CHAT_MSG_BN_WHISPER_INFORM";
    g_scriptEvents[689] = "BN_CHAT_WHISPER_UNDELIVERABLE";
    g_scriptEvents[690] = "BN_CHAT_CHANNEL_JOINED";
    g_scriptEvents[691] = "BN_CHAT_CHANNEL_LEFT";
    g_scriptEvents[692] = "BN_CHAT_CHANNEL_CLOSED";
    g_scriptEvents[693] = "CHAT_MSG_BN_CONVERSATION";
    g_scriptEvents[694] = "CHAT_MSG_BN_CONVERSATION_NOTICE";
    g_scriptEvents[695] = "CHAT_MSG_BN_CONVERSATION_LIST";
    g_scriptEvents[696] = "BN_CHAT_CHANNEL_MESSAGE_UNDELIVERABLE";
    g_scriptEvents[697] = "BN_CHAT_CHANNEL_MESSAGE_BLOCKED";
    g_scriptEvents[698] = "BN_CHAT_CHANNEL_MEMBER_JOINED";
    g_scriptEvents[699] = "BN_CHAT_CHANNEL_MEMBER_LEFT";
    g_scriptEvents[700] = "BN_CHAT_CHANNEL_MEMBER_UPDATED";
    g_scriptEvents[701] = "BN_CHAT_CHANNEL_CREATE_SUCCEEDED";
    g_scriptEvents[702] = "BN_CHAT_CHANNEL_CREATE_FAILED";
    g_scriptEvents[703] = "BN_CHAT_CHANNEL_INVITE_SUCCEEDED";
    g_scriptEvents[704] = "BN_CHAT_CHANNEL_INVITE_FAILED";
    g_scriptEvents[705] = "BN_BLOCK_LIST_UPDATED";
    g_scriptEvents[706] = "BN_SYSTEM_MESSAGE";
    g_scriptEvents[707] = "BN_REQUEST_FOF_SUCCEEDED";
    g_scriptEvents[708] = "BN_REQUEST_FOF_FAILED";
    g_scriptEvents[709] = "BN_NEW_PRESENCE";
    g_scriptEvents[710] = "BN_TOON_NAME_UPDATED";
    g_scriptEvents[711] = "BN_FRIEND_ACCOUNT_ONLINE";
    g_scriptEvents[712] = "BN_FRIEND_ACCOUNT_OFFLINE";
    g_scriptEvents[713] = "BN_FRIEND_TOON_ONLINE";
    g_scriptEvents[714] = "BN_FRIEND_TOON_OFFLINE";
    g_scriptEvents[715] = "BN_MATURE_LANGUAGE_FILTER";
    g_scriptEvents[716] = "COMMENTATOR_SKIRMISH_QUEUE_REQUEST";
    g_scriptEvents[717] = "COMMENTATOR_SKIRMISH_MODE_REQUEST";
    g_scriptEvents[718] = "CHAT_MSG_BN_INLINE_TOAST_ALERT";
    g_scriptEvents[719] = "CHAT_MSG_BN_INLINE_TOAST_BROADCAST";
    g_scriptEvents[720] = "CHAT_MSG_BN_INLINE_TOAST_BROADCAST_INFORM";
    g_scriptEvents[721] = "CHAT_MSG_BN_INLINE_TOAST_CONVERSATION";
}

void ScriptEventsRegisterEvents() {
    FrameScript_CreateEvents(g_scriptEvents, NUM_SCRIPT_EVENTS);
}
