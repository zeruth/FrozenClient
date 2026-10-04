#ifndef OBJECT_CLIENT_CG_BAG_C_HPP
#define OBJECT_CLIENT_CG_BAG_C_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

class CGItem_C;

// A bag walk's test of one item: non-zero to stop at it.
typedef int32_t (*BAGITEMPREDICATE)(CGItem_C* item, void* param);

// The reference's Bag_C.cpp: a view of a run of item guids, one a slot. Only the fields its ported
// functions read are laid out; offsets are the reference's.
class CGBag_C {
    public:
        // Member variables
        uint32_t m_numSlots;            // +0x00
        WOWGUID* m_slots;               // +0x04
        WOWGUID m_owner;                // +0x08, the object the slots belong to
        uint8_t m_hasBankSlots;         // +0x10, slots 39-66 and 67-73 are the bank's

        // Member functions
        int32_t FindSlot(CGItem_C* item);
        CGItem_C* GetItem(uint32_t slot);
        CGItem_C* GetItemAt(uint32_t slot);

        // ref: FUN_007546f0
        CGItem_C* FindItem(BAGITEMPREDICATE predicate, void* param, WOWGUID* bag, uint32_t* slot, uint32_t locations);

        // ref: FUN_007541f0
        bool FindTotemCategory(int32_t type, uint32_t* mask, int32_t recurse, uint32_t locations);

        // ref: FUN_00754470
        int32_t CountFreeSlots();

        // ref: FUN_00754a20
        CGItem_C* FindItemByID(int32_t itemID, uint32_t locations);

        // ref: FUN_00754d00
        int32_t CountItem(int32_t itemID, uint32_t locations);

        // ref: FUN_007548f0
        bool HasTotemCategory(int32_t categoryID, uint32_t locations);
};

// The banker the bank window is open at, zero when it is closed.
extern WOWGUID s_bankerGUID;

#endif
