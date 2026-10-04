#include "object/client/CGBag_C.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "db/Db.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/DBCacheInstances.hpp"

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

// The slot after this one. The player's own bag (the one with bank slots) is walked in this order
// rather than by index: backpack, bags, keyring, bank, bank bags, currency, equipment, buyback.
// Each pair is the last slot of a run and the first of the next; the eighth ends the walk.
static const uint32_t s_bagSlotRuns[8][2] = {                   // ref: DAT_00a37b14
    { 0x26, 0x13 },
    { 0x16, 0x56 },
    { 0x75, 0x27 },
    { 0x42, 0x43 },
    { 0x49, 0x76 },
    { 0x95, 0x00 },
    { 0x12, 0x4A },
    { 0x55, 0x00 },
};

// Inline in each of the reference's bag walks.
static uint32_t BagNextSlot(const CGBag_C* bag, uint32_t slot) {
    auto count = bag->m_numSlots;

    if (slot >= count) {
        return count;
    }

    if (bag->m_hasBankSlots) {
        for (uint32_t i = 0; i < 8; i++) {
            if (s_bagSlotRuns[i][0] == slot) {
                return i < 7 ? s_bagSlotRuns[i][1] : count;
            }
        }
    }

    return slot + 1;
}

// ref: FUN_00754020
// The item is this entry.
static int32_t BagItemMatchesID(CGItem_C* item, void* param) {
    return item->GetEntryID() == *static_cast<int32_t*>(param);
}

// ref: FUN_00754560
// Adds the stack to the count when the item is this entry (or any, for -1). Never stops the walk.
static int32_t BagItemCountStack(CGItem_C* item, void* param) {
    auto count = static_cast<int32_t*>(param);

    if (item->GetEntryID() == count[0] || count[0] == -1) {
        count[1] += static_cast<int32_t>(item->Item()->stackCount);
    }

    return 0;
}

// ref: FUN_007546f0
// The first item the predicate accepts, looking inside each container too, with the bag and slot it
// was found at. The locations mask picks the inventory sections for the player's own bag (backpack,
// bags and equipment unless given); 0x80 admits broken items, 0x10 keeps out of containers, and
// 0x100 asks for the smallest stack of all that match instead of the first.
// PARTIAL: location 0x20 (only items with a use spell and charges left, FUN_00707c60 and
// FUN_00707dc0) is not ported; nothing that reaches here passes it.
CGItem_C* CGBag_C::FindItem(BAGITEMPREDICATE predicate, void* param, WOWGUID* bag, uint32_t* slot, uint32_t locations) {
    if (this->m_hasBankSlots && (locations & 0x247) == 0) {
        locations |= 0x247;
    }

    *bag = 0;
    *slot = 0;

    CGItem_C* smallest = nullptr;

    for (uint32_t index = this->m_hasBankSlots ? 0x17 : 0; index < this->m_numSlots; index = BagNextSlot(this, index)) {
        if (this->m_hasBankSlots && !InventorySlotInLocations(index, locations)) {
            continue;
        }

        WOWGUID guid = index < this->m_numSlots ? this->m_slots[index] : 0;
        auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, ".\\Bag_C.cpp", 0x187));

        if (!item || item->m_disabled) {
            continue;
        }

        if ((locations & 0x80) == 0) {
            auto data = item->Item();

            if ((!(data->flags & 0x8) && data->maxDurability != 0 && data->durability == 0) || (data->flags & 0x10)) {
                continue;
            }
        }

        if ((locations & 0x20) != 0) {
            auto inner = item->GetBag();

            if (!inner || (locations & 0x10) != 0) {
                continue;
            }
        }

        CGItem_C* found;

        if (predicate(item, param)) {
            *bag = this->m_owner;
            *slot = index;
            found = item;
        } else {
            auto inner = item->GetBag();

            if (!inner || (locations & 0x10) != 0) {
                continue;
            }

            found = inner->FindItem(predicate, param, bag, slot, locations);
        }

        if (found) {
            if ((locations & 0x100) == 0) {
                return found;
            }

            if (!smallest || found->Item()->stackCount < smallest->Item()->stackCount) {
                smallest = found;
            }
        }
    }

    return smallest;
}

// ref: FUN_007541f0
// Whether the items here (and, with recurse, in the containers here) cover every bit of *mask for
// a totem category type, clearing the bits each matching item provides. Buyback is not searched.
bool CGBag_C::FindTotemCategory(int32_t type, uint32_t* mask, int32_t recurse, uint32_t locations) {
    if ((locations & 7) == 0) {
        locations |= 7;
    }

    for (uint32_t index = this->m_hasBankSlots ? 0x17 : 0; index < this->m_numSlots; index = BagNextSlot(this, index)) {
        if (this->m_hasBankSlots && !InventorySlotInLocations(index, locations)) {
            continue;
        }

        WOWGUID guid = index < this->m_numSlots ? this->m_slots[index] : 0;
        auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, ".\\Bag_C.cpp", 0x1e6));

        if (!item || (this->m_hasBankSlots && index >= 0x4A && index <= 0x55)) {
            continue;
        }

        WOWGUID requester = 0;
        auto stats = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(item->GetEntryID())), &requester, nullptr, nullptr, false);

        if (stats && stats->totemCategory) {
            auto category = g_totemCategoryDB.GetRecord(stats->totemCategory);

            if (category && category->m_totemCategoryType == type) {
                *mask &= ~category->m_totemCategoryMask;

                if (*mask == 0) {
                    return true;
                }
            }
        }

        if (recurse && item->IsA(TYPE_CONTAINER)) {
            auto inner = item->GetBag();

            if (inner && inner->FindTotemCategory(type, mask, 0, locations)) {
                return true;
            }
        }
    }

    return false;
}

// ref: FUN_00754470
// Empty slots: every one of a plain bag's, and for the player the backpack's and those of each
// equipped bag.
int32_t CGBag_C::CountFreeSlots() {
    int32_t count = 0;

    for (uint32_t index = 0; index < this->m_numSlots; index++) {
        if (this->m_hasBankSlots) {
            if (index <= 0x12
                || (index >= 0x27 && index <= 0x42)
                || (index >= 0x43 && index <= 0x49)
                || (index >= 0x56 && index <= 0x75)
                || (index >= 0x4A && index <= 0x55)
                || index - 0x76 <= 0x1F) {
                continue;
            }

            if (index - 0x13 <= 3) {
                WOWGUID guid = index < this->m_numSlots ? this->m_slots[index] : 0;
                auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, ".\\Bag_C.cpp", 0x2e9));

                if (item && item->GetClassID() == 1) {
                    count += item->GetBag()->CountFreeSlots();
                }

                continue;
            }
        }

        if (index >= this->m_numSlots || this->m_slots[index] == 0) {
            count++;
        }
    }

    return count;
}

// ref: FUN_00754a20
CGItem_C* CGBag_C::FindItemByID(int32_t itemID, uint32_t locations) {
    WOWGUID bag;
    uint32_t slot;

    return this->FindItem(&BagItemMatchesID, &itemID, &bag, &slot, locations);
}

// ref: FUN_00754d00
// How many of an item the bags hold; -2 counts the free slots instead.
int32_t CGBag_C::CountItem(int32_t itemID, uint32_t locations) {
    if (itemID == -2) {
        return this->CountFreeSlots();
    }

    int32_t count[2] = { itemID, 0 };
    WOWGUID bag;
    uint32_t slot;

    this->FindItem(&BagItemCountStack, count, &bag, &slot, locations);

    return count[1];
}

// ref: FUN_007548f0
// Whether the bags hold the tools a totem category asks for.
bool CGBag_C::HasTotemCategory(int32_t categoryID, uint32_t locations) {
    auto category = g_totemCategoryDB.GetRecord(categoryID);

    if (!category) {
        return false;
    }

    uint32_t mask = category->m_totemCategoryMask;

    return this->FindTotemCategory(category->m_totemCategoryType, &mask, 1, locations);
}
