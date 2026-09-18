#include "ui/game/ScriptUtil.hpp"
#include "object/Client.hpp"
#include "ui/game/CGGameUI.hpp"
#include "object/client/NameCache.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include <storm/String.hpp>

namespace {

// ref: FUN_0076f190
// Reads the decimal index after a token prefix ("party3" -> 3) and leaves the token pointing at
// whatever follows it, so a trailing "target" or "pet" is still there to parse.
uint32_t ParseIndex(const char*& token) {
    uint32_t value = 0;

    while (*token >= '0' && *token <= '9') {
        value = value * 10 + (*token - '0');
        token++;
    }

    return value;
}

// ref: FUN_0060aaa0
// The suffixes after a resolved unit: "target" follows UNIT_FIELD_TARGET (the locked target when
// the unit is the player), "pet" follows charm-else-summon and is only honoured for the player,
// a party member, or anyone while the player is in a raid. Unknown suffix: guid 0, false.
bool ParseTrailingTokens(const char* token, WOWGUID& guid, CGPlayer_C* player) {
    while (*token) {
        if (*token == '-') {
            token++;
        }

        if (!SStrCmpI(token, "target", 6)) {
            token += 6;

            if (guid == ClntObjMgrGetActivePlayer()) {
                guid = CGGameUI::GetLockedTarget();
            } else {
                auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__));
                guid = unit ? unit->Unit()->target : 0;
            }
        } else if (!SStrCmpI(token, "pet", 3)) {
            // TODO party membership (FUN_00512a00) and the raid flag on the player's PLAYER fields
            // (bit 19 of the dword at +0x1008 +8) widen this to group members; no group data yet
            if (guid != ClntObjMgrGetActivePlayer()) {
                guid = 0;
                return false;
            }

            token += 3;

            auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__));

            if (!unit) {
                guid = 0;
            } else {
                auto data = unit->Unit();
                guid = data->charm ? data->charm : data->summon;
            }
        } else {
            guid = 0;
            return false;
        }
    }

    return true;
}

}

CGUnit_C* Script_GetUnitFromName(const char* name) {
    WOWGUID guid;

    if (!Script_GetGUIDFromToken(name, guid, false)) {
        return nullptr;
    }

    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__));
}

// ref: FUN_0060a630
// A unit named directly ("Thrall", "Thrall-target"): up to 47 characters of name are taken off the
// token, stopping at a "-target"/"-pet" suffix, and compared case-insensitively with the player,
// then the raid, party or arena roster (whichever the player is in), then the same for pets. The
// token is advanced past the name so the suffix parser can continue. Rosters are not ported yet,
// so only the player and the player's pet resolve; the loop structure is the reference's.
bool Script_GetGUIDFromString(const char*& token, WOWGUID& guid) {
    auto activePlayer = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
    char name[52];
    uint32_t len = 0;

    while (*token && len < 0x2F) {
        if (*token == '-') {
            if (!SStrCmpI(token, "-target", 0x7FFFFFFF) || !SStrCmpI(token, "-pet", 0x7FFFFFFF)
                || !SStrCmpI(token, "-target-", 8) || !SStrCmpI(token, "-pet-", 5)) {
                break;
            }
        }

        name[len++] = *token++;
    }

    name[len] = 0;

    // player
    const char* playerName = activePlayer ? NameCacheGetName(activePlayer) : nullptr;

    if (playerName && !SStrCmpI(name, playerName, 0x7FFFFFFF)) {
        guid = activePlayer->GetGUID();
    } else {
        // TODO raid%d / party%d / arena%d rosters (FUN_00512a80, FUN_00512a60, FUN_00608180)
        guid = 0;
    }

    if (!guid) {
        // pet, then raidpet%d / partypet%d / arenapet%d
        WOWGUID petGuid = 0;

        if (activePlayer) {
            auto data = activePlayer->Unit();
            petGuid = data->charm ? data->charm : data->summon;
        }

        auto pet = petGuid ? static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(petGuid, TYPE_UNIT, __FILE__, __LINE__)) : nullptr;
        const char* petName = pet ? NameCacheGetName(pet) : nullptr;

        if (petName && !SStrCmpI(name, petName, 0x7FFFFFFF)) {
            guid = petGuid;
        }

        // TODO raidpet%d / partypet%d / arenapet%d (FUN_00518ce0, FUN_00512a70, FUN_0060a2f0)

        if (!guid) {
            return false;
        }
    }

    return true;
}

// ref: FUN_0060abf0
bool Script_GetGUIDFromToken(const char* token, WOWGUID& guid, bool defaultToTarget) {
    auto activePlayer = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    // Null or empty token
    if (token == nullptr || *token == '\0') {
        if (defaultToTarget) {
            guid = CGGameUI::GetLockedTarget();

            return true;
        }

        return false;
    }

    guid = 0;
    auto parseToken = token;

    // player - active player
    if (!SStrCmpI(parseToken, "player", 6)) {
        parseToken += 6;

        if (activePlayer) {
            guid = activePlayer->GetGUID();
        }
    }

    // vehicle - active player's vehicle
    else if (!SStrCmpI(parseToken, "vehicle", 7)) {
        parseToken += 7;

        // TODO
    }

    // pet - active player's pet: the charmed unit, else the summoned one
    else if (!SStrCmpI(parseToken, "pet", 3)) {
        parseToken += 3;

        if (activePlayer) {
            auto data = activePlayer->Unit();
            guid = data->charm ? data->charm : data->summon;
        }
    }

    // target - current locked target
    else if (!SStrCmpI(parseToken, "target", 6)) {
        parseToken += 6;

        guid = CGGameUI::GetLockedTarget();
    }

    // partypet1-4 - party member's pet
    else if (!SStrCmpI(parseToken, "partypet", 8)) {
        parseToken += 8;

        auto index = ParseIndex(parseToken);
        // TODO
    }

    // party1-4 - party member
    else if (!SStrCmpI(parseToken, "party", 5)) {
        parseToken += 5;

        auto index = ParseIndex(parseToken);
        // TODO
    }

    // raidpet1-40 - raid member's pet
    else if (!SStrCmpI(parseToken, "raidpet", 7)) {
        parseToken += 7;

        auto index = ParseIndex(parseToken);
        // TODO
    }

    // raid1-40 - raid member
    else if (!SStrCmpI(parseToken, "raid", 4)) {
        parseToken += 4;

        auto index = ParseIndex(parseToken);
        // TODO
    }

    // boss1-5 - boss unit
    else if (!SStrCmpI(parseToken, "boss", 4)) {
        parseToken += 4;

        auto index = ParseIndex(parseToken);
        // TODO
    }

    // arenapet1-5 - arena opponent's pet
    else if (!SStrCmpI(parseToken, "arenapet", 8)) {
        parseToken += 8;

        auto index = ParseIndex(parseToken);
        // TODO
    }

    // arena1-5 - arena opponent
    else if (!SStrCmpI(parseToken, "arena", 5)) {
        parseToken += 5;

        auto index = ParseIndex(parseToken);
        // TODO
    }

    // commentator1-N - commentator arena member
    else if (!SStrCmpI(parseToken, "commentator", 11)) {
        parseToken += 11;

        auto index = ParseIndex(parseToken);
        // TODO
    }

    // mouseover - object under cursor
    else if (!SStrCmpI(parseToken, "mouseover", 9)) {
        parseToken += 9;

        auto trackedObjectGuid = CGGameUI::GetCurrentObjectTrack();

        if (ClntObjMgrObjectPtr(trackedObjectGuid, TYPE_UNIT, __FILE__, __LINE__) || CGGameUI::IsRaidMemberOrPet(trackedObjectGuid)) {
            guid = trackedObjectGuid;
        }
    }

    // focus - focus target
    else if (!SStrCmpI(parseToken, "focus", 5)) {
        parseToken += 5;

        // TODO
    }

    // npc - NPC interaction target
    else if (!SStrCmpI(parseToken, "npc")) {
        parseToken += 3;

        // TODO
    }

    // questnpc - quest giver NPC
    else if (!SStrCmpI(parseToken, "questnpc")) {
        parseToken += 8;

        // TODO
    }

    // none
    else if (!SStrCmpI(parseToken, "none")) {
        parseToken += 4;

        guid = -1;
    }

    // Token string was fully parsed or GUID was determined and token string potentially includes
    // trailing tokens
    if ((*parseToken == '\0' || guid) && ParseTrailingTokens(parseToken, guid, activePlayer)) {
        if (!guid) {
            guid = -2;
        }

        return true;
    }

    // Token string was either not parsed or only partially parsed and GUID was not determined
    if (!guid && Script_GetGUIDFromString(token, guid) && ParseTrailingTokens(token, guid, activePlayer)) {
        if (!guid) {
            guid = -2;
        }

        return true;
    }

    // GUID was not successfully determined
    return false;
}
