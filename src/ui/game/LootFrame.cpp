#include "ui/game/LootFrame.hpp"
#include "util/guid/Types.hpp"

// The open loot window. Written by the loot response handler, which is not ported, so no loot
// window is ever open. While s_lootMoney is non-zero the money takes the first slot and every item
// slot is one further along.
static LootSlot s_lootSlots[18];                // ref: DAT_00bfa690
static int32_t s_lootType;                      // ref: DAT_00bfa68c
static int32_t s_lootMoney;                     // ref: DAT_00bfa8d0
static WOWGUID s_lootGuid;                      // ref: DAT_00bfa8d8

// ref: FUN_00588210
int32_t LootGetSlotItemID(uint32_t slot) {
    if (s_lootGuid) {
        if (s_lootMoney) {
            if (slot == 0) {
                return 0;
            }

            slot--;
        }

        if (slot < 18) {
            return s_lootSlots[slot].m_itemID;
        }
    }

    return 0;
}

// ref: FUN_00588250
int32_t LootGetSlotField0C(uint32_t slot) {
    if (s_lootGuid) {
        if (s_lootMoney) {
            if (slot == 0) {
                return 0;
            }

            slot--;
        }

        if (slot < 18) {
            return s_lootSlots[slot].m_unk0C;
        }
    }

    return 0;
}

// ref: FUN_00588290
int32_t LootGetSlotField10(uint32_t slot) {
    if (s_lootGuid) {
        if (s_lootMoney) {
            if (slot == 0) {
                return 0;
            }

            slot--;
        }

        if (slot < 18) {
            return s_lootSlots[slot].m_unk10;
        }
    }

    return 0;
}

// ref: FUN_005882d0
int32_t LootGetSlotField14(uint32_t slot) {
    if (s_lootGuid) {
        if (s_lootMoney) {
            if (slot == 0) {
                return 0;
            }

            slot--;
        }

        if (slot < 18) {
            return s_lootSlots[slot].m_unk14;
        }
    }

    return 0;
}

// ref: FUN_00588530
int32_t LootGetType() {
    return s_lootType;
}
