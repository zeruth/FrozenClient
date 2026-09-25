#include "ui/game/GuildBankFrame.hpp"
#include <storm/Array.hpp>

// The slots of each tab. Filled only by the guild bank packet handlers, which are not ported, so
// every tab reads as empty.
static TSGrowableArray<GuildBankSlot> s_guildBankTabs[6];   // ref: DAT_00c1dc38

// ref: FUN_005a4c10
GuildBankSlot* GuildBankGetSlot(uint32_t tab, uint32_t slot) {
    if (tab < 6 && slot < s_guildBankTabs[tab].Count()) {
        GuildBankSlot* entry = &s_guildBankTabs[tab][slot];

        if (entry->m_unk00 != 0) {
            return entry;
        }
    }

    return nullptr;
}

// ref: FUN_005a4ce0
// packedSlot is tab * 98 + slot.
void GuildBankClearSlotFlag(int32_t id, uint32_t packedSlot) {
    uint32_t tab = packedSlot / 98;

    if (tab < 6 && packedSlot % 98 < s_guildBankTabs[tab].Count()) {
        GuildBankSlot* entry = &s_guildBankTabs[tab][packedSlot % 98];

        if (id == entry->m_unk00) {
            entry->m_unk24 = 0;
        }
    }
}
