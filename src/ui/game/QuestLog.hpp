#ifndef UI_GAME_QUEST_LOG_HPP
#define UI_GAME_QUEST_LOG_HPP

#include "object/client/DBCache.hpp"
#include <cstdint>

// The reference's QuestLog.cpp: one 0x10-byte record per quest log line. Only the fields its
// ported functions read are named; offsets are the reference's.
struct QuestLogEntry {
    int32_t m_questID;                  // +0x00
    int32_t m_slot;                     // +0x04, the quest's slot in the player's quest log fields
    int32_t m_unk08;                    // +0x08, non-zero rows are never matched as quests
    int32_t m_unk0C;                    // +0x0C
};

int32_t QuestLogGetQuestID(int32_t index);

int32_t QuestLogEntryHasField0C(uint32_t index);

int32_t QuestLogGetIndexByID(int32_t questID);

// The lines in the log, headers included (DAT_00c23ad0).
uint32_t QuestLogGetNumEntries();

// ref: FUN_005e0ea0
// Whether the quest on line `index` has everything it asks for: reputation, money, the creatures
// and objects counted, the items carried and the players slain. With `needObjectives`, a quest with
// nothing to collect counts as not complete.
bool QuestLogIsComplete(uint32_t index, bool needObjectives);

// One objective's text, as the tooltips list them under the quest's title. The callbacks are the
// ones each cache lookup is made with; FUN_005dfd70 fills in the quest log's own.
struct QUESTOBJECTIVEINFO {
    int32_t m_unk00 = -1;                                   // +0x00
    int32_t m_unk04 = 0;                                    // +0x04
    int32_t m_unk08 = 0;                                    // +0x08
    int32_t m_unk0C = 1;                                    // +0x0c
    DBCACHECALLBACKFN m_questCallback;                      // +0x10
    DBCACHECALLBACKFN m_gameObjectCallback;                 // +0x14
    DBCACHECALLBACKFN m_creatureCallback;                   // +0x18
    DBCACHECALLBACKFN m_itemCallback;                       // +0x1c
    void* m_param = nullptr;                                // +0x20
    char m_text[0x1000] = {};                               // +0x24 "Kobold Vermin slain: 3/8"
    char m_type[0x40] = {};                                 // +0x1024 "monster", "object" or "item"
    int32_t m_complete = 0;                                 // +0x1064

    // ref: FUN_005dfd70
    QUESTOBJECTIVEINFO();
};

// ref: FUN_005e2370
// The objective of quest `questID` that counts the creature (or, with the top bit set, the game
// object) `id`, written into `info`. False when the quest has no such objective or is not in the
// log.
bool QuestLogGetCreatureObjective(int32_t questID, int32_t id, QUESTOBJECTIVEINFO* info);

// ref: FUN_005e2630
// As QuestLogGetCreatureObjective, for an item the quest needs. An item that is also the quest's
// own source item only counts when more than one is needed.
bool QuestLogGetItemObjective(int32_t questID, int32_t itemID, QUESTOBJECTIVEINFO* info);

#endif
