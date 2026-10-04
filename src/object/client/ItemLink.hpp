#ifndef OBJECT_CLIENT_ITEM_LINK_HPP
#define OBJECT_CLIENT_ITEM_LINK_HPP

#include "util/GUID.hpp"
#include <cstdint>

class CGItem_C;

// The three gem sockets a link carries. A fourth field is written to the link and is always zero.
struct ITEM_LINK_GEMS {
    int32_t gem[3] = { 0, 0, 0 };
};

// What an item tooltip shows for an item that is not an object the client holds: the fields of an
// "item:" hyperlink, and whatever a setter fills in from its own source (a mail attachment, a trade
// slot, an inspected player's gear). 0xd8 bytes in the reference; the tooltip carries one at +0x3e8
// and reads it instead of an item object when its "use link info" flag is set.
struct ITEMLINKINFO {
    int32_t itemID;                 // +0x00
    // Per enchantment slot, as the item descriptor numbers them: 0 permanent, 1 temporary, 2..4
    // the sockets, 5 the socket bonus, 6 the prismatic socket, 7..11 the random property.
    int32_t enchant[12];            // +0x04
    int32_t enchantDuration[12];    // +0x34
    int32_t enchantCharges[12];     // +0x64
    int32_t cooldown;               // +0x94 milliseconds left
    int32_t proposedEnchant;        // +0x98 a spell shown as "will be enchanted with"
    int32_t unk9c;                  // +0x9c not reset
    WOWGUID creator;                // +0xa0
    WOWGUID giftCreator;            // +0xa8
    int32_t suffixFactor;           // +0xb0 the link's "seed"
    int32_t randomPropertyID;       // +0xb4 negative for an ItemRandomSuffix row
    int32_t charges;                // +0xb8 -1 when not known
    int32_t locked;                 // +0xbc
    int32_t lockID;                 // +0xc0
    int32_t maxDurability;          // +0xc4 -1 means "take the item's own"
    int32_t durability;             // +0xc8
    int32_t level;                  // +0xcc the linking player's level
    int32_t stackCount;             // +0xd0
    int32_t maxStackCount;          // +0xd4 -1 when there is no second count

    void Reset();
    int32_t Parse(const char* link);
};

// An item's display name, "<name> of the <suffix>" when it has a random property (positive
// suffix, ItemRandomProperties) or a random suffix (negative, ItemRandomSuffix). Null, with an
// empty buffer, while the item's cache record has not arrived; this never asks for it.
char* ItemNameFromEntry(char* dest, uint32_t destSize, int32_t entryID, int32_t suffix);

// The full hyperlink, colour included. The returned pointer is to a shared buffer, as it is in the
// reference: the caller is expected to push it to Lua immediately.
const char* ItemLinkBuild(int32_t entryID, int32_t quality, int32_t enchant,
                          const ITEM_LINK_GEMS& gems, int32_t suffix, int32_t seed);

// The same link for an item without gems.
const char* ItemLinkBuild(int32_t entryID, int32_t quality, int32_t enchant, int32_t suffix, int32_t seed);

// The link for an item that exists as an object, which is where the enchant, gems and random
// property come from. Null for a null item.
const char* ItemLinkFromObject(CGItem_C* item);

#endif
