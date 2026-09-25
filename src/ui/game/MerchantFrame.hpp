#ifndef UI_GAME_MERCHANT_FRAME_HPP
#define UI_GAME_MERCHANT_FRAME_HPP

#include "util/GUID.hpp"
#include <cstdint>

// The reference's MerchantFrame.cpp: one 0x20-byte record per item the open vendor sells. Only the
// fields its ported functions read are named; offsets are the reference's.
struct MerchantItem {
    uint8_t m_unk00[0x4];
    int32_t m_unk04;                    // +0x04, zero for an empty slot
    uint8_t m_unk08[0x14];
    int32_t m_extendedCostID;           // +0x1c
};

int32_t MerchantCanRepair();

MerchantItem* MerchantGetItem(int32_t index);

int32_t MerchantIsBuybackItem(WOWGUID guid);

#endif
