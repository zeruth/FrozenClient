#include "object/client/CGBag_C.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/ObjMgr.hpp"

// Nothing sets it yet: the bank window code is not ported, so the bank slots stay hidden.
WOWGUID s_bankerGUID;                   // ref: DAT_00beb9a0

// ref: FUN_00754040
int32_t CGBag_C::FindSlot(CGItem_C* item) {
    if (item == nullptr) {
        return -1;
    }

    for (uint32_t slot = 0; slot < this->m_numSlots; slot++) {
        if (ClntObjMgrObjectPtr(this->m_slots[slot], TYPE_ITEM, __FILE__, __LINE__) == item) {
            return static_cast<int32_t>(slot);
        }
    }

    return -1;
}

// ref: FUN_00754390
CGItem_C* CGBag_C::GetItem(uint32_t slot) {
    if ((!this->m_hasBankSlots
            || ((static_cast<int32_t>(slot) < 39 || 66 < static_cast<int32_t>(slot)) && 6 < slot - 67)
            || s_bankerGUID != 0)
        && static_cast<int32_t>(slot) < static_cast<int32_t>(this->m_numSlots)
    ) {
        WOWGUID guid = slot < this->m_numSlots ? this->m_slots[slot] : 0;

        return static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, __FILE__, __LINE__));
    }

    return nullptr;
}

// ref: FUN_00754400
// The same test and lookup as GetItem; the reference keeps both.
CGItem_C* CGBag_C::GetItemAt(uint32_t slot) {
    if ((!this->m_hasBankSlots
            || ((static_cast<int32_t>(slot) < 39 || 66 < static_cast<int32_t>(slot)) && 6 < slot - 67)
            || s_bankerGUID != 0)
        && static_cast<int32_t>(slot) < static_cast<int32_t>(this->m_numSlots)
    ) {
        WOWGUID guid = slot < this->m_numSlots ? this->m_slots[slot] : 0;

        return static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, __FILE__, __LINE__));
    }

    return nullptr;
}
