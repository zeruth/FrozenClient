#include "ui/game/TradeSkillFrame.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/Types.hpp"
#include <storm/Array.hpp>

// The repeat-cast state: the spell being repeated and how many casts remain, and the pair a new
// request has asked for, which becomes the repeat once its cast goes through.
static int32_t s_recastSpell;                   // ref: DAT_00c235d4
static int32_t s_recastCount;                   // ref: DAT_00c235d8
static int32_t s_pendingRecastSpell;            // ref: DAT_00c235dc
static int32_t s_pendingRecastCount;            // ref: DAT_00c235e0

// Cleared when the frame closes; written by the trade skill show handler, which is not ported.
static uint32_t s_unkC235A0;                    // ref: DAT_00c235a0
static uint32_t s_unkC235A4;                    // ref: DAT_00c235a4

// The list lines and the sub-class headers, each with the count currently shown. Filled by the
// list builder, which is not ported, so both lists are empty.
static TSFixedArray<TradeSkillInfo*> s_tradeSkills;                 // ref: DAT_00c235f0
static TSFixedArray<TradeSkillSubClassInfo*> s_tradeSkillSubClasses; // ref: DAT_00c23600
static uint32_t s_numTradeSkills;               // ref: DAT_00c235b0
static uint32_t s_numTradeSkillSubClasses;      // ref: DAT_00c235b4

// ref: FUN_005d9ff0
void TradeSkillClearRecast() {
    if (s_recastSpell || s_pendingRecastSpell) {
        s_recastSpell = 0;
        s_pendingRecastSpell = 0;
        FrameScript_SignalEvent(SCRIPT_UPDATE_TRADESKILL_RECAST, nullptr);
    }
}

// ref: FUN_005da020
void TradeSkillCancelRecast(int32_t spellID) {
    if (spellID == s_recastSpell) {
        s_recastSpell = 0;
        FrameScript_SignalEvent(SCRIPT_UPDATE_TRADESKILL_RECAST, nullptr);
    }

    if (spellID == s_pendingRecastSpell) {
        s_pendingRecastSpell = 0;
        FrameScript_SignalEvent(SCRIPT_UPDATE_TRADESKILL_RECAST, nullptr);
    }
}

// ref: FUN_005da0f0
void TradeSkillCommitRecast(int32_t spellID) {
    if (spellID == s_pendingRecastSpell) {
        s_recastSpell = s_pendingRecastSpell;
        s_recastCount = s_pendingRecastCount;
    }

    s_pendingRecastCount = 0;
    s_pendingRecastSpell = 0;
}

// ref: FUN_005da5a0
void TradeSkillFrameClose() {
    s_unkC235A0 = 0;
    s_unkC235A4 = 0;

    if (s_recastSpell || s_pendingRecastSpell) {
        s_recastSpell = 0;
        s_pendingRecastSpell = 0;
        FrameScript_SignalEvent(SCRIPT_UPDATE_TRADESKILL_RECAST, nullptr);
    }

    FrameScript_SignalEvent(SCRIPT_TRADE_SKILL_CLOSE, nullptr);
}

// The sub-class a header line stands for, or -1 for a line that is not a header.
// ref: FUN_005da7d0
uint32_t TradeSkillGetSubClassIndex(uint32_t index) {
    if (index >= s_numTradeSkills) {
        return 0xFFFFFFFF;
    }

    auto skill = s_tradeSkills.m_data[index];

    if (skill->m_unk00 < 0) {
        for (uint32_t i = 0; i < s_numTradeSkillSubClasses; i++) {
            auto subClass = s_tradeSkillSubClasses.m_data[i];

            if (subClass->m_unk00 == skill->m_unk08 && subClass->m_unk04 == skill->m_unk0C) {
                return i;
            }
        }

        return 0xFFFFFFFF;
    }

    return 0xFFFFFFFF;
}
