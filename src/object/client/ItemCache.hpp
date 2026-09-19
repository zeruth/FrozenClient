#ifndef OBJECT_CLIENT_ITEM_CACHE_HPP
#define OBJECT_CLIENT_ITEM_CACHE_HPP

#include "net/Types.hpp"
#include "util/GUID.hpp"
#include <cstdint>
#include <string>

class CDataStore;

// Static item data by entry id -- name, quality, icon and the fields the tooltip and the item
// bindings read. This stands in for the reference's CDBCache item cache the way NameCache stands in
// for its name and creature caches: same asynchronous shape, much smaller. See
// docs/ref/parity-itemcache.md for the record's wire format and where it was recovered from.
struct ItemInfo {
    // Populated only once the response lands. Until then a lookup returns null and a query is in
    // flight; an entry the server does not know is marked missing and never asked for again.
    bool known = false;
    bool missing = false;

    // Null in the reference when the server sends an empty string, so this is empty rather than a
    // placeholder and callers decide what to show.
    const char* Name() const { return name.c_str(); }

    std::string name;

    int32_t itemClass = 0;
    int32_t subClass = 0;
    int32_t soundOverrideSubclass = 0;

    int32_t displayInfoID = 0;
    int32_t quality = 0;
    uint32_t flags = 0;
    uint32_t flags2 = 0;

    int32_t buyPrice = 0;
    int32_t sellPrice = 0;
    int32_t inventoryType = 0;
    int32_t allowableClass = 0;
    int32_t allowableRace = 0;
    int32_t itemLevel = 0;
    int32_t requiredLevel = 0;
    int32_t requiredSkill = 0;
    int32_t requiredSkillRank = 0;
    int32_t requiredSpell = 0;
    int32_t requiredHonorRank = 0;
    int32_t requiredCityRank = 0;
    int32_t requiredReputationFaction = 0;
    int32_t requiredReputationRank = 0;

    // What GetItemInfo reports as the stack size, and what the bag code needs.
    int32_t maxCount = 0;
    int32_t stackable = 0;
    int32_t containerSlots = 0;
};

// Returns the record when it is known, or null while the query is in flight or the entry is
// missing. Sends CMSG_ITEM_QUERY_SINGLE on the first miss and not again.
const ItemInfo* ItemCacheGet(int32_t entryID);

void ItemCacheRegisterHandlers();
void ItemCacheClear();

int32_t ReceiveItemQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
