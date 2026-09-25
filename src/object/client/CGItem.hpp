#ifndef OBJECT_CLIENT_CG_ITEM_HPP
#define OBJECT_CLIENT_CG_ITEM_HPP

#include "util/GUID.hpp"
#include <cstdint>

struct ItemEnchantment {
    int32_t id;
    int32_t expiration;
    int32_t chargesRemaining;
};

struct CGItemData {
    WOWGUID owner;
    WOWGUID containedIn;
    WOWGUID creator;
    WOWGUID giftCreator;
    uint32_t stackCount;
    int32_t expiration;
    int32_t spellCharges[5];
    uint32_t flags;
    ItemEnchantment enchantments[12];
    int32_t propertySeed;
    int32_t randomPropertiesID;
    int32_t durability;
    int32_t maxDurability;
    int32_t createPlayedTime;
    int32_t pad;
};

class CGItem {
    public:
        // Public to match CGUnit::Unit() and CGPlayer::Player(), the sibling accessors for the
        // other data blocks. The script bindings read these the way the reference does.
        CGItemData* Item() const;

        // ref: FUN_00584ae0
        // 0 for an item flagged indestructible (ITEM_FIELD_FLAGS bit 3).
        int32_t GetMaxDurability() const;

        // Public static functions
        static uint32_t GetBaseOffset();
        static uint32_t GetBaseOffsetSaved();
        static uint32_t GetDataSize();
        static uint32_t GetDataSizeSaved();
        static uint32_t TotalFields();
        static uint32_t TotalFieldsSaved();

    protected:
        // Protected member variables
        CGItemData* m_item;
        uint32_t* m_itemSaved;

        // Protected member functions
};

#endif
