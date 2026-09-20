#include "ui/game/PartyInfoScript.hpp"
#include <storm/String.hpp>
#include "ui/game/CGGameUI.hpp"
#include "ui/game/CGRaidInfo.hpp"
#include "object/client/ObjMgr.hpp"
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
// ref: FUN_0052c190
int32_t Script_GetRealNumPartyMembers(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(CGPartyInfo::GetRealNumMembers()));

    return 1;
}

// ref: FUN_0052c1d0
// Whether party slot 1-4 is occupied. 1 or nil, and an index outside the range is a usage error
// rather than a nil -- the reference names the range in the message.
int32_t Script_GetPartyMember(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetPartyMember(1-4)");

        return 0;
    }

    auto index = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 1))) - 1;

    if (index >= 4) {
        luaL_error(L, "Usage: GetPartyMember(1-4)");

        return 0;
    }

    if (CGPartyInfo::GetMember(index + 1)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0052c270
// Which party slot holds the leader, or 0 when the leader is the player or there is no party.
//
// The reference keeps this as an index that starts at -1 and adds one on the way out, so "nobody"
// and "slot 0" are the same answer by construction. Derived here instead, from the leader guid
// against the roster, which gives the same 0 for both.
int32_t Script_GetPartyLeaderIndex(lua_State* L) {
    auto leader = CGPartyInfo::GetLeader();
    uint32_t index = 0;

    if (leader) {
        for (uint32_t slot = 1; slot <= 4; slot++) {
            if (CGPartyInfo::GetMember(slot) == leader) {
                index = slot;

                break;
            }
        }
    }

    lua_pushnumber(L, static_cast<double>(index));

    return 1;
}

// ref: FUN_0052ccd0
// The leader guid has to be set AND be the player's: an empty guid is not "you lead a party of
// one", it is "there is no party", and the reference tests both.
int32_t Script_IsPartyLeader(lua_State* L) {
    auto leader = CGPartyInfo::GetLeader();

    if (leader && leader == ClntObjMgrGetActivePlayer()) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0052cd30
// IsPartyLeader with the real leader instead of the one in force: in a battleground you can lead
// the battleground group without leading your own party, and these two then disagree.
int32_t Script_IsRealPartyLeader(lua_State* L) {
    auto leader = CGPartyInfo::GetRealLeader();

    if (leader && leader == ClntObjMgrGetActivePlayer()) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_LeaveParty(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0052cd90
// method, master looter's party index, master looter's raid index.
//
// The five names and their order come from the switch's own jump table, not from memory: 0 is
// freeforall, 1 roundrobin, 2 master, 3 group, 4 needbeforegreed, and anything else is the literal
// string "ERROR!" rather than a nil or an empty string.
//
// The party index is 0 when the player is the looter, 1-4 for a party member, and -1 for neither.
// Zero is a real answer there, not "none".
int32_t Script_GetLootMethod(lua_State* L) {
    static const char* s_methods[] = {
        "freeforall", "roundrobin", "master", "group", "needbeforegreed"
    };

    auto method = CGPartyInfo::GetLootMethod();

    lua_pushstring(L, method < 5 ? s_methods[method] : "ERROR!");

    auto looter = CGPartyInfo::GetMasterLooter();
    int32_t partyIndex = -1;

    if (looter) {
        if (looter == ClntObjMgrGetActivePlayer()) {
            partyIndex = 0;
        } else {
            for (uint32_t slot = 1; slot <= 4; slot++) {
                if (CGPartyInfo::GetMember(slot) == looter) {
                    partyIndex = static_cast<int32_t>(slot);

                    break;
                }
            }
        }
    }

    lua_pushnumber(L, static_cast<double>(partyIndex));

    // TODO the raid index. The reference walks its raid array for the looter, but frozen's raid
    // roster has no established numbering -- the player is somewhere in it and the packet does not
    // say where -- so reporting a position would be a guess. -1 reads as "not a raid member",
    // which is at least the right shape.
    lua_pushnumber(L, -1.0);

    return 3;
}

// Both loot setters share these two guards, and both are the reference's: you must be IN a group,
// and you must lead it. They report through the game's own error messages rather than raising a
// Lua error, so a UI that offers the option to a non-leader gets a red line, not a script failure.
static bool CanSetLoot() {
    if (!CGPartyInfo::GetMember(1) && !CGRaidInfo::NumMembers()) {
        CGGameUI::DisplayError(0x50);

        return false;
    }

    auto leader = CGPartyInfo::GetLeader();

    if (!leader || leader != ClntObjMgrGetActivePlayer()) {
        CGGameUI::DisplayError(0x54);

        return false;
    }

    return true;
}

// ref: FUN_0052dc20
// SetLootMethod("method" [, master]). The names are matched case-insensitively against the same
// five GetLootMethod reports, and an unrecognised one is a Lua error rather than a silent no-op.
//
// Master loot names a PLAYER, not a unit token: the second argument is looked up in the roster by
// name. Choosing master without naming anyone is the game's own error 0xfc.
int32_t Script_SetLootMethod(lua_State* L) {
    if (!CanSetLoot()) {
        return 0;
    }

    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: SetLootMethod(\"method\" [,master])");

        return 0;
    }

    static const char* s_methods[] = {
        "freeforall", "roundrobin", "master", "group", "needbeforegreed"
    };

    auto name = lua_tostring(L, 1);
    int32_t method = -1;

    for (int32_t i = 0; i < 5; i++) {
        if (!SStrCmpI(name, s_methods[i], STORM_MAX_STR)) {
            method = i;

            break;
        }
    }

    if (method < 0) {
        luaL_error(L, "Invalid loot method");

        return 0;
    }

    WOWGUID looter = 0;

    if (method == 2) {
        auto master = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;

        if (!master || !*master) {
            CGGameUI::DisplayError(0xfc);

            return 0;
        }

        looter = CGPartyInfo::FindByName(master);

        if (!looter) {
            CGGameUI::DisplayError(0xfc);

            return 0;
        }
    }

    CGPartyInfo::SendLootSettings(method, looter, CGPartyInfo::GetLootThreshold());

    return 0;
}

// ref: FUN_0052c2a0
int32_t Script_GetLootThreshold(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(CGPartyInfo::GetLootThreshold()));

    return 1;
}

// ref: FUN_0052de60
// Thresholds run 2 to 6 -- uncommon through artifact. Below uncommon there is nothing to roll for,
// which is why the range does not start at 0.
//
// The usage string carries the reference's own typo, "SetLooThreshold". That spelling is the only
// one in the binary; the corrected one appears nowhere, so this is the text the original prints.
int32_t Script_SetLootThreshold(lua_State* L) {
    if (!CanSetLoot()) {
        return 0;
    }

    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetLooThreshold(threshold)");

        return 0;
    }

    auto threshold = static_cast<int32_t>(lua_tonumber(L, 1));

    if (threshold < 2 || threshold > 6) {
        luaL_error(L, "SetLootThreshold(): threshold must be between %d and %d", 2, 6);

        return 0;
    }

    // The whole triple goes out together, so the method and master looter are resent unchanged.
    CGPartyInfo::SendLootSettings(
        CGPartyInfo::GetLootMethod(), CGPartyInfo::GetMasterLooter(), threshold
    );

    return 0;
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
