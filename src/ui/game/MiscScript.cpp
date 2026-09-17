// Script functions whose subsystems are not ported yet. Each answers the way a client with
// nothing going on would (no voice chat, no pets, no arena teams, ...), so the FrameXML code
// that calls them at load or on entering the world runs to completion instead of failing on a
// missing global. They move to their subsystem files as those get ported.

#include "ui/game/MiscScript.hpp"
#include <cstdio>
#include "ui/FrameScript.hpp"
#include "object/client/SpellBook.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/Types.hpp"
#include "db/Db.hpp"
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

    // Background colour and alpha. These were 1,1,1,1 -- an opaque WHITE panel, which is exactly
    // how the chat frame rendered. FrameXML's own defaults are a black backdrop at
    // DEFAULT_CHATFRAME_ALPHA, which FloatingChatFrame.lua defines as 0.25.
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.25);

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

// wipe(table) -- empties a table in place and hands it back. A stock Lua global in the 3.3.5a
// client, used all over FrameXML to recycle scratch tables; without it those call sites error and
// take the rest of their function with them.
//
// Clearing fields during a next() traversal is explicitly allowed in Lua 5.1: setting an existing
// key to nil is fine, only adding new keys is not.
int32_t Script_Wipe(lua_State* L) {
    if (!lua_istable(L, 1)) {
        return luaL_error(L, "Usage: wipe(table)");
    }

    lua_pushnil(L);

    while (lua_next(L, 1)) {
        lua_pop(L, 1);          // drop the value, leaving the key on top
        lua_pushvalue(L, -1);   // duplicate it: one copy for rawset, one for the next iteration
        lua_pushnil(L);
        lua_rawset(L, 1);
    }

    lua_pushvalue(L, 1);

    return 1;
}

// Talents are not ported. These answer the way a character with no talent data does, rather than
// nothing at all: every one of them is destructured straight into arithmetic or an index, so a stub
// that pushed no values was taking down the talent frame and all three spec tabs.

// GetUnspentTalentPoints(inspect, pet, talentGroup) -> points
int32_t Script_GetUnspentTalentPoints(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

// GetPreviewTalentPointsSpent(pet, talentGroup) -> points
int32_t Script_GetPreviewTalentPointsSpent(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

// GetActiveTalentGroup(inspect, pet) -> which of the two specs is active, numbered from 1.
int32_t Script_GetActiveTalentGroup(lua_State* L) {
    lua_pushnumber(L, 1.0);

    return 1;
}

// GetTalentTabInfo(tab, inspect, pet, talentGroup)
//   -> name, iconTexture, pointsSpent, background, previewPointsSpent
// The two counts are summed by the caller; the three strings are only displayed, and nil is how the
// reference reports a tab it has no data for.
int32_t Script_GetTalentTabInfo(lua_State* L) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);

    return 5;
}

int32_t Script_ReturnNothing(lua_State* L) {
    // 11 values, typed from what the caller destructures them into:
    //   teamName, teamSize, teamRating, teamPlayed, teamWins, seasonTeamPlayed, seasonTeamWins, playerPlayed, seasonPlayerPlayed, teamRank, personalRating
    // The data behind this is not available yet, so each position takes the neutral
    // value for its type -- 0 where the caller does arithmetic, false where it
    // branches, nil where it expects a name or a texture and already handles absence.
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 11;
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
// GetSpellTabInfo(tab)
//   -> name, texture, offset, numSpells, highestRankOffset, highestRankNumSpells
//
// SIX values, not four. FrameXML's SpellBook_GetTabInfo destructures all six and then does
//
//     if ( not GetCVarBool("ShowAllSpellRanks") ) then
//         offset = highestRankOffset; numSpells = highestRankNumSpells;
//
// so with only four returned, both became nil on the default setting and the spellbook raised
// "attempt to perform arithmetic on local 'numSpells'" and, through
// SpellBookFrame.selectedSkillLineOffset, "arithmetic on field 'selectedSkillLineOffset'". Both
// errors were in the run log; the returns are the cause.
//
// The spellbook itself is not populated yet -- there is no known-spell list -- so the counts are
// zero. That is a tab with no spells in it, which the interface handles; nil is what it cannot.
int32_t Script_GetSpellTabInfo(lua_State* L) {
    // -> name, texture, offset, numSpells, highestRankOffset, highestRankNumSpells
    //
    // Offsets are 0-based slot bases the way the reference reports them; FrameXML adds 1 when it
    // asks for a slot. The highest-rank view sits after the all-ranks view in slot space, so its
    // offsets are shifted by the book size.
    if (!lua_isnumber(L, 1)) {
        return 0;
    }

    auto tab = SpellBookTabAt(static_cast<int32_t>(lua_tonumber(L, 1)) - 1);

    if (!tab) {
        return 0;
    }

    const char* icon = "Interface\\Icons\\INV_Misc_QuestionMark";

    if (tab->iconID) {
        auto rec = g_spellIconDB.GetRecord(tab->iconID);

        if (rec && rec->m_textureFilename && *rec->m_textureFilename) {
            icon = rec->m_textureFilename;
        }
    }

    lua_pushstring(L, tab->name);
    lua_pushstring(L, icon);
    lua_pushnumber(L, tab->offset);
    lua_pushnumber(L, tab->count);
    lua_pushnumber(L, SpellBookCount() + tab->highestOffset);
    lua_pushnumber(L, tab->highestCount);

    return 6;
}

int32_t Script_GetNumSpellTabs(lua_State* L) {
    lua_pushnumber(L, SpellBookTabCount());

    return 1;
}

// Spellbook slot -> Spell.dbc row. Lua slots count from 1; the book from 0. Returns null for an
// empty slot or an id the DBC does not know.
const SpellRec* SpellAtSlot(lua_State* L, int32_t index) {
    if (!lua_isnumber(L, index)) {
        return nullptr;
    }

    uint32_t id = SpellBookSpellAt(static_cast<int32_t>(lua_tonumber(L, index)) - 1);

    return id ? g_spellDB.GetRecord(static_cast<int32_t>(id)) : nullptr;
}

const char* SpellIconPath(const SpellRec* spell) {
    if (!spell || !spell->m_spellIconID) {
        return nullptr;
    }

    auto icon = g_spellIconDB.GetRecord(spell->m_spellIconID);

    return icon && icon->m_textureFilename && *icon->m_textureFilename ? icon->m_textureFilename : nullptr;
}

// GetSpellName(slot, bookType) -> name, rank
int32_t Script_GetSpellName(lua_State* L) {
    auto spell = SpellAtSlot(L, 1);

    if (!spell) {
        lua_pushnil(L);
        lua_pushnil(L);

        return 2;
    }

    lua_pushstring(L, spell->m_name);

    // An unranked spell hands back nil rather than "", which is what FrameXML tests for.
    if (spell->m_rank && *spell->m_rank) {
        lua_pushstring(L, spell->m_rank);
    } else {
        lua_pushnil(L);
    }

    return 2;
}

// GetSpellTexture(slot, bookType) -> texture path
int32_t Script_GetSpellTexture(lua_State* L) {
    const char* path = SpellIconPath(SpellAtSlot(L, 1));

    if (path) {
        lua_pushstring(L, path);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// GetSpellInfo(spellId | "name") -> name, rank, icon, cost, isFunnel, powerType, castTime,
//                                    minRange, maxRange
// Costs, cast time and range live in Spell.dbc columns that are not read yet; those report as zero.
int32_t Script_GetSpellInfo(lua_State* L) {
    const SpellRec* spell = nullptr;

    if (lua_isnumber(L, 1)) {
        spell = g_spellDB.GetRecord(static_cast<int32_t>(lua_tonumber(L, 1)));
    } else if (lua_isstring(L, 1)) {
        // By name: the highest known rank, the way the reference resolves a bare name.
        const char* name = lua_tostring(L, 1);

        for (int32_t i = 0; i < SpellBookHighestRankCount(); i++) {
            auto candidate = g_spellDB.GetRecord(static_cast<int32_t>(SpellBookSpellAt(SpellBookCount() + i)));

            if (candidate && candidate->m_name && !SStrCmpI(candidate->m_name, name, STORM_MAX_STR)) {
                spell = candidate;
                break;
            }
        }
    }

    if (!spell) {
        return 0;
    }

    lua_pushstring(L, spell->m_name);

    if (spell->m_rank && *spell->m_rank) {
        lua_pushstring(L, spell->m_rank);
    } else {
        lua_pushnil(L);
    }

    const char* icon = SpellIconPath(spell);

    if (icon) {
        lua_pushstring(L, icon);
    } else {
        lua_pushnil(L);
    }

    lua_pushnumber(L, 0.0);   // cost
    lua_pushboolean(L, 0);    // isFunnel
    lua_pushnumber(L, 0.0);   // powerType
    lua_pushnumber(L, 0.0);   // castTime (ms)
    lua_pushnumber(L, 0.0);   // minRange
    lua_pushnumber(L, 0.0);   // maxRange

    return 9;
}

// GetSpellCooldown(slot, bookType) -> start, duration, enable. No cooldown tracking yet, so
// every spell reads as ready -- the enable flag set with a zero duration is "ready" in FrameXML.
int32_t Script_GetSpellCooldown(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 1.0);

    return 3;
}

// GetSpellAutocast(slot, bookType) -> autocastable, autostate. Only pet spells autocast.
int32_t Script_GetSpellAutocast(lua_State* L) {
    lua_pushnil(L);
    lua_pushnil(L);

    return 2;
}

// IsPassiveSpell(slot | spellId, bookType) -> 1 or nil. SPELL_ATTR0_PASSIVE is bit 0x40 of
// Spell.dbc Attributes.
int32_t Script_IsPassiveSpell(lua_State* L) {
    const SpellRec* spell = SpellAtSlot(L, 1);

    // FrameXML also calls this with a spell id when no bookType is given.
    if (!spell && lua_isnumber(L, 1) && lua_isnoneornil(L, 2)) {
        spell = g_spellDB.GetRecord(static_cast<int32_t>(lua_tonumber(L, 1)));
    }

    if (spell && (spell->m_attributes & 0x40)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// CastSpell(slot, bookType): cast on the current target, or self when there is none.
int32_t Script_CastSpell(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        return 0;
    }

    uint32_t id = SpellBookSpellAt(static_cast<int32_t>(lua_tonumber(L, 1)) - 1);

    if (!id) {
        return 0;
    }

    uint64_t target = 0;
    auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, __FILE__, __LINE__);

    if (player) {
        auto unit = static_cast<CGUnit_C*>(player)->Unit();

        if (unit) {
            target = unit->target;
        }
    }

    SpellBookCast(id, target);

    return 0;
}

// GetSpellLink(slot, bookType) -> "|cff71d5ff|Hspell:ID|h[Name]|h|r", the chat link for a spell.
int32_t Script_GetSpellLink(lua_State* L) {
    auto spell = SpellAtSlot(L, 1);

    if (!spell || !spell->m_name) {
        lua_pushnil(L);

        return 1;
    }

    char link[512];
    SStrPrintf(link, sizeof(link), "|cff71d5ff|Hspell:%d|h[%s]|h|r", spell->m_ID, spell->m_name);
    lua_pushstring(L, link);

    return 1;
}

// UpdateSpells(): FrameXML calls this after turning a spellbook page and expects the book to be
// redrawn in response -- the reference answers it with SPELLS_CHANGED, and SpellBookFrame_OnEvent
// redraws on that. Without the event the page number changed and the buttons did not.
int32_t Script_UpdateSpells(lua_State* L) {
    FrameScript_SignalEvent(242, nullptr); // SPELLS_CHANGED

    return 0;
}

// GetKnownSlotFromHighestRankSlot(slot) -> the same spell's slot in the all-ranks view.
int32_t Script_GetKnownSlotFromHighestRankSlot(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        return 0;
    }

    int32_t slot = static_cast<int32_t>(lua_tonumber(L, 1)) - 1;
    lua_pushnumber(L, SpellBookKnownSlotFromHighestRankSlot(slot) + 1);

    return 1;
}

// CombatLog_Object_IsA(unitFlags, mask) -> true when the flags fall inside the mask. A client
// function the combat log addon captures as an upvalue at load, so its absence was a nil call
// rather than a missing global.
int32_t Script_CombatLog_Object_IsA(lua_State* L) {
    uint32_t flags = lua_isnumber(L, 1) ? static_cast<uint32_t>(lua_tonumber(L, 1)) : 0;
    uint32_t mask = lua_isnumber(L, 2) ? static_cast<uint32_t>(lua_tonumber(L, 2)) : 0;

    lua_pushboolean(L, (flags & mask) != 0);

    return 1;
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
    { "GetSpellName",                   &Script_GetSpellName },
    { "GetSpellTexture",                &Script_GetSpellTexture },
    { "GetSpellInfo",                   &Script_GetSpellInfo },
    { "GetSpellCooldown",               &Script_GetSpellCooldown },
    { "GetSpellAutocast",               &Script_GetSpellAutocast },
    { "IsPassiveSpell",                 &Script_IsPassiveSpell },
    { "CastSpell",                      &Script_CastSpell },
    { "GetSpellLink",                   &Script_GetSpellLink },
    { "UpdateSpells",                   &Script_UpdateSpells },
    { "GetKnownSlotFromHighestRankSlot", &Script_GetKnownSlotFromHighestRankSlot },
    { "CombatLog_Object_IsA",           &Script_CombatLog_Object_IsA },
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
    { "GetNumSpellTabs",                &Script_GetNumSpellTabs },

    // Registered because FrameXML CALLS them and a missing global is a hard Lua error, not a
    // no-op: each of these was observed throwing "attempt to call a nil value" in a real session
    // and taking the rest of its script with it. The values are the reference's own answers for
    // the state this client is actually in -- not listed in a raid finder, no pet bar, no skill
    // selected, no quest timers running -- so they are correct today and will need real
    // implementations when those systems land.
    { "IsListedInLFR",                  &Script_ReturnNil },      // 1 or nil; never listed yet
    { "PetHasActionBar",                &Script_ReturnNil },      // 1 or nil; no pet bar yet
    { "GetSelectedSkill",               &Script_ReturnZero },     // index; nothing selected
    { "GetQuestTimers",                 &Script_ReturnNothing },  // varargs; none running
    { "GetCurrentMapContinent",         &Script_ReturnZero },     // continent index; unset

    // Second layer, found the same way: with the five above registered, FrameXML got further and
    // threw on these instead. Each round of this peels back one layer of script that could not
    // run before.
    { "TriggerTutorial",                &Script_ReturnNothing },  // fires a tutorial; returns nothing
    { "GetTrackedAchievements",         &Script_ReturnNothing },  // varargs of ids; none tracked
    { "GetSkillLineInfo",               &Script_ReturnNothing },  // 13 values per skill; no skills
    { "GetPreviousArenaSeason",         &Script_ReturnZero },     // season number
    { "GetLFGRoles",                    &Script_ReturnNothing },  // leader/tank/healer/damage flags
    { "GetCurrentMapDungeonLevel",      &Script_ReturnZero },     // map floor; ground level

    // Predicates for systems this client has no data for: no guild, no auction house,
    // no pet, no vehicle, no petition, no tradeskill, no instance. The reference answers
    // nil (false) for every one of these in that state, so nil is its behaviour and not a
    // placeholder. Each still needs a real implementation when its system lands.
    //
    // They were absent rather than stubbed, and a missing global is a hard Lua error that
    // takes down the rest of the script that touched it.
    { "CanAlterSkin",                        &Script_ReturnNil },
    { "CanCancelAuction",                    &Script_ReturnNil },
    { "CanComplainChat",                     &Script_ReturnNil },
    { "CanComplainInboxItem",                &Script_ReturnNil },
    { "CanEditGuildEvent",                   &Script_ReturnNil },
    { "CanEditGuildInfo",                    &Script_ReturnNil },
    { "CanEditGuildTabInfo",                 &Script_ReturnNil },
    { "CanEditMOTD",                         &Script_ReturnNil },
    { "CanEditOfficerNote",                  &Script_ReturnNil },
    { "CanEditPublicNote",                   &Script_ReturnNil },
    { "CanExitVehicle",                      &Script_ReturnNil },
    { "CanGuildBankRepair",                  &Script_ReturnNil },
    { "CanGuildDemote",                      &Script_ReturnNil },
    { "CanGuildInvite",                      &Script_ReturnNil },
    { "CanGuildPromote",                     &Script_ReturnNil },
    { "CanGuildRemove",                      &Script_ReturnNil },
    { "CanMerchantRepair",                   &Script_ReturnNil },
    { "CanPartyLFGBackfill",                 &Script_ReturnNil },
    { "CanQueueForWintergrasp",              &Script_ReturnNil },
    { "CanResetTutorials",                   &Script_ReturnNil },
    { "CanSendAuctionQuery",                 &Script_ReturnNil },
    { "CanShowAchievementUI",                &Script_ReturnNil },
    { "CanSignPetition",                     &Script_ReturnNil },
    { "CanSwitchVehicleSeats",               &Script_ReturnNil },
    { "CanUseEquipmentSets",                 &Script_ReturnNil },
    { "CanViewOfficerNote",                  &Script_ReturnNil },
    { "CanWithdrawGuildBankMoney",           &Script_ReturnNil },
    { "CannotBeResurrected",                 &Script_ReturnNil },
    { "HasDebugZoneMap",                     &Script_ReturnNil },
    { "HasFilledPetition",                   &Script_ReturnNil },
    { "HasNewMail",                          &Script_ReturnNil },
    { "HasPetUI",                            &Script_ReturnNil },
    { "IsActiveQuestTrivial",                &Script_ReturnNil },
    { "IsAtStableMaster",                    &Script_ReturnNil },
    { "IsAttackSpell",                       &Script_ReturnNil },
    { "IsAuctionSortReversed",               &Script_ReturnNil },
    { "IsAutoRepeatSpell",                   &Script_ReturnNil },
    { "IsAvailableQuestTrivial",             &Script_ReturnNil },
    { "IsConsumableSpell",                   &Script_ReturnNil },
    { "IsCurrentQuestFailed",                &Script_ReturnNil },
    { "IsCurrentSpell",                      &Script_ReturnNil },
    { "IsDisplayChannelModerator",           &Script_ReturnNil },
    { "IsDisplayChannelOwner",               &Script_ReturnNil },
    { "IsFactionInactive",                   &Script_ReturnNil },
    { "IsFishingLoot",                       &Script_ReturnNil },
    { "IsHarmfulSpell",                      &Script_ReturnNil },
    { "IsHelpfulSpell",                      &Script_ReturnNil },
    { "IsIgnored",                           &Script_ReturnNil },
    { "IsIgnoredOrMuted",                    &Script_ReturnNil },
    { "IsInLFGDungeon",                      &Script_ReturnNil },
    { "IsLFGDungeonJoinable",                &Script_ReturnNil },
    { "IsMuted",                             &Script_ReturnNil },
    { "IsPetAttackActive",                   &Script_ReturnNil },
    { "IsQuestCompletable",                  &Script_ReturnNil },
    { "IsQuestLogSpecialItemInRange",        &Script_ReturnNil },
    { "IsQuestWatched",                      &Script_ReturnNil },
    { "IsSelectedSpell",                     &Script_ReturnNil },
    { "IsSilenced",                          &Script_ReturnNil },
    { "IsSpellInRange",                      &Script_ReturnNil },
    { "IsSpellKnown",                        &Script_ReturnNil },
    { "IsTrackedAchievement",                &Script_ReturnNil },
    { "IsTradeSkillLinked",                  &Script_ReturnNil },
    { "IsTradeskillTrainer",                 &Script_ReturnNil },
    { "IsTrainerServiceSkillStep",           &Script_ReturnNil },
    { "IsTutorialFlagged",                   &Script_ReturnNil },
    { "IsUnitOnQuest",                       &Script_ReturnNil },
    { "IsUsableSpell",                       &Script_ReturnNil },
    { "IsUsingVehicleControls",              &Script_ReturnNil },
    { "IsVehicleAimAngleAdjustable",         &Script_ReturnNil },
    { "IsVehicleAimPowerAdjustable",         &Script_ReturnNil },
    { "IsVoiceChatAllowed",                  &Script_ReturnNil },
    { "IsZoomOutAvailable",                  &Script_ReturnNil },

    // Cancel* are ACTIONS, not predicates: they return nothing.
    { "CancelAuction",                       &Script_ReturnNothing },
    { "CancelBarberShop",                    &Script_ReturnNothing },
    { "CancelDuel",                          &Script_ReturnNothing },
    { "CancelItemTempEnchantment",           &Script_ReturnNothing },
    { "CancelSell",                          &Script_ReturnNothing },
    { "CancelShapeshiftForm",                &Script_ReturnNothing },
    { "CancelSkillUps",                      &Script_ReturnNothing },
    { "CancelUnitBuff",                      &Script_ReturnNothing },

    // Counts for systems this client has no data for. The reference returns 0 for every one
    // of these when the underlying list is empty, which is exactly this client's state, so
    // 0 is its behaviour rather than a stand-in. They also gate the Get*Info calls that
    // follow them: a caller that is told there are none does not then ask for item 1.
    { "GetNumActiveQuests",                  &Script_ReturnZero },
    { "GetNumArenaTeamMembers",              &Script_ReturnZero },
    { "GetNumAuctionItems",                  &Script_ReturnZero },
    { "GetNumAvailableQuests",               &Script_ReturnZero },
    { "GetNumBuybackItems",                  &Script_ReturnZero },
    { "GetNumChannelMembers",                &Script_ReturnZero },
    { "GetNumCompanions",                    &Script_ReturnZero },
    { "GetNumComparisonCompletedAchievements", &Script_ReturnZero },
    { "GetNumCompletedAchievements",         &Script_ReturnZero },
    { "GetNumDungeonMapLevels",              &Script_ReturnZero },
    { "GetNumEquipmentSets",                 &Script_ReturnZero },
    { "GetNumFactions",                      &Script_ReturnZero },
    { "GetNumGlyphSockets",                  &Script_ReturnZero },
    { "GetNumGossipActiveQuests",            &Script_ReturnZero },
    { "GetNumGossipAvailableQuests",         &Script_ReturnZero },
    { "GetNumGossipOptions",                 &Script_ReturnZero },
    { "GetNumGuildBankMoneyTransactions",    &Script_ReturnZero },
    { "GetNumGuildBankTabs",                 &Script_ReturnZero },
    { "GetNumGuildBankTransactions",         &Script_ReturnZero },
    { "GetNumGuildEvents",                   &Script_ReturnZero },
    { "GetNumGuildMembers",                  &Script_ReturnZero },
    { "GetNumLootItems",                     &Script_ReturnZero },
    { "GetNumMacroIcons",                    &Script_ReturnZero },
    { "GetNumMacroItemIcons",                &Script_ReturnZero },
    { "GetNumMacros",                        &Script_ReturnZero },
    { "GetNumMapDebugObjects",               &Script_ReturnZero },
    { "GetNumMapLandmarks",                  &Script_ReturnZero },
    { "GetNumMapOverlays",                   &Script_ReturnZero },
    { "GetNumPackages",                      &Script_ReturnZero },
    { "GetNumPetitionItems",                 &Script_ReturnZero },
    { "GetNumPetitionNames",                 &Script_ReturnZero },
    { "GetNumQuestChoices",                  &Script_ReturnZero },
    { "GetNumQuestItemDrops",                &Script_ReturnZero },
    { "GetNumQuestItems",                    &Script_ReturnZero },
    { "GetNumQuestLeaderBoards",             &Script_ReturnZero },
    { "GetNumQuestLogChoices",               &Script_ReturnZero },
    { "GetNumQuestLogEntries",               &Script_ReturnZero },
    { "GetNumQuestLogRewardFactions",        &Script_ReturnZero },
    { "GetNumQuestLogRewards",               &Script_ReturnZero },
    { "GetNumQuestRewards",                  &Script_ReturnZero },
    { "GetNumQuestWatches",                  &Script_ReturnZero },
    { "GetNumRandomDungeons",                &Script_ReturnZero },
    { "GetNumRoutes",                        &Script_ReturnZero },
    { "GetNumSockets",                       &Script_ReturnZero },
    { "GetNumStablePets",                    &Script_ReturnZero },
    { "GetNumStableSlots",                   &Script_ReturnZero },
    { "GetNumStationeries",                  &Script_ReturnZero },
    { "GetNumTalentGroups",                  &Script_ReturnZero },
    { "wipe",                                &Script_Wipe },
    { "GetUnspentTalentPoints",              &Script_GetUnspentTalentPoints },
    { "GetPreviewTalentPointsSpent",         &Script_GetPreviewTalentPointsSpent },
    { "GetGroupPreviewTalentPointsSpent",    &Script_GetPreviewTalentPointsSpent },
    { "GetActiveTalentGroup",                &Script_GetActiveTalentGroup },
    { "GetTalentTabInfo",                    &Script_GetTalentTabInfo },
    { "GetNumTalentTabs",                    &Script_ReturnZero },
    { "GetNumTalents",                       &Script_ReturnZero },
    { "GetNumTrackedAchievements",           &Script_ReturnZero },
    { "GetNumTradeSkills",                   &Script_ReturnZero },
    { "GetNumTrainerServices",               &Script_ReturnZero },
    { "GetNumVoiceSessionMembersBySessionID", &Script_ReturnZero },
    { "GetNumWatchedTokens",                 &Script_ReturnZero },
    { "GetNumWhoResults",                    &Script_ReturnZero },
};

} // namespace

void MiscScriptRegisterFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
