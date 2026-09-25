#include "ui/game/QuestLog.hpp"

// The quest log as the interface lists it. Written by the quest log rebuild, which is not ported,
// so the log is always empty.
static QuestLogEntry s_questLogEntries[50];     // ref: DAT_00c237b0
static int32_t s_numQuestLogEntries;            // ref: DAT_00c23ad0

// ref: FUN_005dec40
int32_t QuestLogGetQuestID(int32_t index) {
    if (index >= 0 && index < s_numQuestLogEntries && !s_questLogEntries[index].m_unk08) {
        return s_questLogEntries[index].m_questID;
    }

    return 0;
}

// ref: FUN_005dee30
int32_t QuestLogEntryHasField0C(uint32_t index) {
    if (index < static_cast<uint32_t>(s_numQuestLogEntries)
        && !s_questLogEntries[index].m_unk08
        && s_questLogEntries[index].m_unk0C
    ) {
        return 1;
    }

    return 0;
}

// Returns the 1-based line of the quest, or 0. Walks all 50 lines, not only the used ones.
// ref: FUN_005deeb0
int32_t QuestLogGetIndexByID(int32_t questID) {
    for (uint32_t i = 0; i < 50; i++) {
        if (!s_questLogEntries[i].m_unk08 && s_questLogEntries[i].m_questID == questID) {
            return i + 1;
        }
    }

    return 0;
}
