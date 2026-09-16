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
    lua_pushstring(L, "General");
    lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark");
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 6;
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
    { "IsPassiveSpell",                      &Script_ReturnNil },
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
