#include "ui/game/QuestFrameScript.hpp"
#include "db/Db.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"
#include <storm/String.hpp>

// The quest frame's state: the quest the giver is showing, as its bindings read it. Nothing fills
// it yet -- the quest giver messages are not handled -- so every binding answers as the reference
// does with no quest open.
namespace {

int32_t s_rewardFactionValueOverrides[5];  // ref: DAT_00c01700
int32_t s_rewardFactionValueIDs[5];        // ref: DAT_00c01714
int32_t s_rewardFactionIDs[5];             // ref: DAT_00c01728
int32_t s_rewardFactionTail;               // ref: DAT_00c0d698

// Seven rows: GetQuestItemID accepts indices 0..6.
QUEST_FRAME_ITEM_ROW s_items[7];           // ref: DAT_00c01740

// 3000 bytes each: the two buffers sit 0xbb8 apart, and the next global follows the second at the
// same distance.
char s_progressText[3000];                 // ref: DAT_00c0a920
char s_objectiveText[3000];                // ref: DAT_00c0b4d8

int32_t s_rewardSpell;                     // ref: DAT_00c0d680
int32_t s_rewardSpellCast;                 // ref: DAT_00c0d684

// ref: FUN_0058bd70
int32_t Script_GetObjectiveText(lua_State* L) {
    lua_pushstring(L, s_objectiveText);

    return 1;
}

// ref: FUN_0058bd90
int32_t Script_GetProgressText(lua_State* L) {
    lua_pushstring(L, s_progressText);

    return 1;
}

// ref: FUN_0058d670
// texture, name, isTradeskillSpell, isSpellLearned. The last is whether the spell cast on
// completion teaches a spell (SPELL_EFFECT_LEARN_SPELL 36 or LEARN_PET_SPELL 57 in any of its three
// effects); the reward spell itself is only described.
int32_t Script_GetRewardSpell(lua_State* L) {
    if (s_rewardSpell) {
        auto spell = g_spellDB.GetRecord(s_rewardSpell);

        if (spell) {
            auto icon = g_spellIconDB.GetRecord(spell->m_spellIconID);

            if (icon) {
                lua_pushstring(L, icon->m_textureFilename);
            } else {
                lua_pushnil(L);
            }

            lua_pushstring(L, spell->m_name);

            if (spell->m_attributes & 0x20) {
                lua_pushnumber(L, 1.0);
            } else {
                lua_pushnil(L);
            }

            bool learns = false;

            if (s_rewardSpellCast) {
                auto cast = g_spellDB.GetRecord(s_rewardSpellCast);

                if (cast) {
                    for (uint32_t i = 0; i < 3; i++) {
                        if (cast->m_effect[i] == 36 || cast->m_effect[i] == 57) {
                            learns = true;
                            break;
                        }
                    }
                }

                if (learns) {
                    lua_pushnumber(L, 1.0);

                    return 4;
                }
            }

            lua_pushnil(L);

            return 4;
        }
    }

    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);

    return 4;
}

} // namespace

// ref: FUN_0058bb60
void QuestFrameSetRewardFaction(uint32_t index, int32_t factionID, int32_t valueID, int32_t valueOverride) {
    if (index < 5) {
        s_rewardFactionIDs[index] = factionID;
        s_rewardFactionValueIDs[index] = valueID;
        s_rewardFactionValueOverrides[index] = valueOverride;
    }
}

// ref: FUN_0058bb90
void QuestFrameSetRewardFactionTail(int32_t value) {
    s_rewardFactionTail = value;
}

// ref: FUN_0058bba0
int32_t QuestFrameGetNumChoices() {
    int32_t count = 0;

    for (uint32_t row = 0; row < 6; row++) {
        if (!s_items[row].choice.itemID) {
            return count;
        }

        count++;
    }

    return count;
}

// ref: FUN_0058bbc0
int32_t QuestFrameGetItemID(const char* type, uint32_t index) {
    if (index > 6) {
        return 0;
    }

    if (!SStrCmpI(type, "reward", STORM_MAX_STR)) {
        return s_items[index].reward.itemID;
    }

    if (!SStrCmpI(type, "choice", STORM_MAX_STR)) {
        return s_items[index].choice.itemID;
    }

    if (!SStrCmpI(type, "required", STORM_MAX_STR)) {
        return s_items[index].required.itemID;
    }

    return 0;
}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GetObjectiveText",   &Script_GetObjectiveText },
    { "GetProgressText",    &Script_GetProgressText },
    { "GetRewardSpell",     &Script_GetRewardSpell },
};

void QuestFrameRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
