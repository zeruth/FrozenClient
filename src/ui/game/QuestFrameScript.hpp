#ifndef UI_GAME_QUEST_FRAME_SCRIPT_HPP
#define UI_GAME_QUEST_FRAME_SCRIPT_HPP

#include <cstdint>

// One item the quest frame lists. Only the item id has been identified; the other two dwords are
// carried until the code that fills them is ported.
struct QUEST_FRAME_ITEM {
    int32_t itemID = 0;
    uint32_t unk04 = 0;
    uint32_t unk08 = 0;
};

// The reference interleaves the three lists row by row, 0x24 bytes a row.
struct QUEST_FRAME_ITEM_ROW {
    QUEST_FRAME_ITEM reward;
    QUEST_FRAME_ITEM choice;
    QUEST_FRAME_ITEM required;
};

// ref: FUN_0058bb60
// The three per-faction values of one reward faction slot, in quest-record order. Slots past the
// fifth are ignored.
void QuestFrameSetRewardFaction(uint32_t index, int32_t factionID, int32_t valueID, int32_t valueOverride);

// ref: FUN_0058bb90
// The dword that follows the three faction arrays in the quest record (+0x28e0). Not identified.
void QuestFrameSetRewardFactionTail(int32_t value);

// ref: FUN_0058bba0
// How many choice items are listed: rows up to the first empty choice, at most six.
int32_t QuestFrameGetNumChoices();

// ref: FUN_0058bbc0
// The item id in one row of the "reward", "choice" or "required" list; 0 for any other type or a
// row past the seventh.
int32_t QuestFrameGetItemID(const char* type, uint32_t index);

void QuestFrameRegisterScriptFunctions();

#endif
