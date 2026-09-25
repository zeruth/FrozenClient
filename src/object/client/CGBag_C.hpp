#ifndef OBJECT_CLIENT_CG_BAG_C_HPP
#define OBJECT_CLIENT_CG_BAG_C_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

class CGItem_C;

// The reference's Bag_C.cpp: a view of a run of item guids, one a slot. Only the fields its ported
// functions read are laid out; offsets are the reference's.
class CGBag_C {
    public:
        // Member variables
        uint32_t m_numSlots;            // +0x00
        WOWGUID* m_slots;               // +0x04
        uint32_t m_unk08;
        uint32_t m_unk0C;
        uint8_t m_hasBankSlots;         // +0x10, slots 39-66 and 67-73 are the bank's

        // Member functions
        int32_t FindSlot(CGItem_C* item);
        CGItem_C* GetItem(uint32_t slot);
        CGItem_C* GetItemAt(uint32_t slot);
};

// The banker the bank window is open at, zero when it is closed.
extern WOWGUID s_bankerGUID;

#endif
