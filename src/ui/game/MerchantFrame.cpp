#include "ui/game/MerchantFrame.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"

// The open vendor and what it sells. Filled only by the vendor packet handlers, which are not
// ported, so no vendor is ever open.
static WOWGUID s_merchantGuid;                  // ref: DAT_00bfa3e8
static int32_t s_merchantItemCount;             // ref: DAT_00bfa3f0
static MerchantItem s_merchantItems[150];       // ref: DAT_00bf9128

// ref: FUN_005845b0
int32_t MerchantCanRepair() {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(s_merchantGuid, TYPE_UNIT, __FILE__, __LINE__));

    if (unit && (unit->Unit()->npcFlags >> 12 & 1)) {
        return 1;
    }

    return 0;
}

// ref: FUN_00584080
MerchantItem* MerchantGetItem(int32_t index) {
    if (index >= 0 && index < s_merchantItemCount && s_merchantGuid != 0) {
        return &s_merchantItems[index];
    }

    return nullptr;
}

// ref: FUN_00584d90
// The reference walks the player's slot table from the first buyback slot (74) to the last (85),
// each index checked against the table's size; frozen's descriptor holds all twelve.
int32_t MerchantIsBuybackItem(WOWGUID guid) {
    auto player = CGPlayer_C::GetActivePtr();

    if (!player) {
        return 0;
    }

    for (uint32_t i = 0; i < 12; i++) {
        if (player->Player()->vendorBuybackSlots[i] == guid) {
            return 1;
        }
    }

    return 0;
}
