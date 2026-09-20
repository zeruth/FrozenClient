#ifndef OBJECT_CLIENT_ITEM_LINK_HPP
#define OBJECT_CLIENT_ITEM_LINK_HPP

#include <cstdint>

class CGItem_C;

// The three gem sockets a link carries. A fourth field is written to the link and is always zero.
struct ITEM_LINK_GEMS {
    int32_t gem[3] = { 0, 0, 0 };
};

// ref: FUN_00706d70
// An item's display name. suffix selects a random-property or random-suffix decoration; 0 is a
// plain item and is the only case frozen answers fully -- see the definition.
const char* ItemNameFromEntry(int32_t entryID, int32_t suffix);

// ref: FUN_0061e290
// The full hyperlink, colour included. The returned pointer is to a shared buffer, as it is in the
// reference: the caller is expected to push it to Lua immediately.
const char* ItemLinkBuild(int32_t entryID, int32_t quality, int32_t enchant,
                          const ITEM_LINK_GEMS& gems, int32_t suffix, int32_t seed);

// ref: FUN_0061e3a0
// The link for an item that exists as an object, which is where the enchant, gems and random
// property come from. Null for a null item.
const char* ItemLinkFromObject(CGItem_C* item);

#endif
