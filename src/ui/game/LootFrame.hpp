#ifndef UI_GAME_LOOT_FRAME_HPP
#define UI_GAME_LOOT_FRAME_HPP

#include <cstdint>

// The reference's LootFrame.cpp: one 0x20-byte record per loot slot of the open loot window. Only
// the fields its ported functions read are named; offsets are the reference's.
struct LootSlot {
    int32_t m_unk00;                    // +0x00, zero for an empty slot
    int32_t m_itemID;                   // +0x04
    int32_t m_unk08;
    int32_t m_unk0C;                    // +0x0C
    int32_t m_unk10;                    // +0x10
    int32_t m_unk14;                    // +0x14
    int32_t m_unk18;
    int32_t m_unk1C;
};

int32_t LootGetSlotItemID(uint32_t slot);

int32_t LootGetSlotField0C(uint32_t slot);

int32_t LootGetSlotField10(uint32_t slot);

int32_t LootGetSlotField14(uint32_t slot);

int32_t LootGetType();

#endif
