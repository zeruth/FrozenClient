#ifndef UI_GAME_QUEST_LOG_HPP
#define UI_GAME_QUEST_LOG_HPP

#include <cstdint>

// The reference's QuestLog.cpp: one 0x10-byte record per quest log line. Only the fields its
// ported functions read are named; offsets are the reference's.
struct QuestLogEntry {
    int32_t m_questID;                  // +0x00
    int32_t m_unk04;
    int32_t m_unk08;                    // +0x08, non-zero rows are never matched as quests
    int32_t m_unk0C;                    // +0x0C
};

int32_t QuestLogGetQuestID(int32_t index);

int32_t QuestLogEntryHasField0C(uint32_t index);

int32_t QuestLogGetIndexByID(int32_t questID);

#endif
