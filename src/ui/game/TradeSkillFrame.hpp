#ifndef UI_GAME_TRADE_SKILL_FRAME_HPP
#define UI_GAME_TRADE_SKILL_FRAME_HPP

#include <cstdint>

// The reference's TradeSkillFrame.cpp. Only the fields its ported functions read are named;
// offsets are the reference's.

// One line of the trade skill list. A negative m_unk00 marks a sub-class header line, which is
// matched to its TradeSkillSubClassInfo by the pair at +0x08.
struct TradeSkillInfo {
    int32_t m_unk00;                    // +0x00
    int32_t m_unk04;
    int32_t m_unk08;                    // +0x08
    int32_t m_unk0C;                    // +0x0C
};

struct TradeSkillSubClassInfo {
    int32_t m_unk00;                    // +0x00
    int32_t m_unk04;                    // +0x04
};

void TradeSkillClearRecast();

void TradeSkillCancelRecast(int32_t spellID);

void TradeSkillCommitRecast(int32_t spellID);

void TradeSkillFrameClose();

uint32_t TradeSkillGetSubClassIndex(uint32_t index);

#endif
