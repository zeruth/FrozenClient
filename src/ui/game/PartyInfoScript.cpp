#include "ui/game/PartyInfoScript.hpp"
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

int32_t Script_SetLootMethod(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0052c2a0
int32_t Script_GetLootThreshold(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(CGPartyInfo::GetLootThreshold()));

    return 1;
}

int32_t Script_SetLootThreshold(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
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
