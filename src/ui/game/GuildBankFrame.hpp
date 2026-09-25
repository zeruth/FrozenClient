#ifndef UI_GAME_GUILD_BANK_FRAME_HPP
#define UI_GAME_GUILD_BANK_FRAME_HPP

#include <cstdint>

// The reference's GuildBankFrame.cpp: six tabs of up to 98 slots, one 0x2c-byte record a slot.
// Only the fields its ported functions read are named; offsets are the reference's.
struct GuildBankSlot {
    int32_t m_unk00;                    // +0x00, zero for an empty slot
    uint8_t m_unk04[0x20];
    int32_t m_unk24;                    // +0x24, cleared by GuildBankClearSlotFlag
    uint8_t m_unk28[0x4];
};

GuildBankSlot* GuildBankGetSlot(uint32_t tab, uint32_t slot);
void GuildBankClearSlotFlag(int32_t id, uint32_t packedSlot);

#endif
