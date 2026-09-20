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

    // The most of this item a character may own at once. NOT what GetItemInfo reports as the
    // stack size -- that is the next field, which the reference reads at record offset 0x5c.
    int32_t maxCount = 0;

    // The stack size GetItemInfo reports.
    int32_t stackable = 0;
    int32_t containerSlots = 0;

    // The stat block. The reference keeps ten slots and fills the unused TYPES with -1 while
    // leaving their values at zero, so a reader walks until it sees -1 rather than counting.
    static const int32_t MAX_STATS = 10;
    int32_t statsCount = 0;
    int32_t statType[MAX_STATS] = {};
    int32_t statValue[MAX_STATS] = {};

    int32_t scalingStatDistribution = 0;
    int32_t scalingStatValue = 0;

    // Two damage bands, each a float pair and a school.
    static const int32_t MAX_DAMAGES = 2;
    float damageMin[MAX_DAMAGES] = {};
    float damageMax[MAX_DAMAGES] = {};
    int32_t damageType[MAX_DAMAGES] = {};

    // Armor first, then the six schools in the order the wire sends them: holy, fire, nature,
    // frost, shadow, arcane.
    int32_t armor = 0;
    int32_t resistance[6] = {};

    int32_t delay = 0;
    int32_t ammoType = 0;
    float rangedModRange = 0.0f;

    // Five "on use"/"on equip" spell slots, six parallel arrays -- which is how the wire carries
    // them, not a convenience: the reference reads one index across all six before moving on.
    static const int32_t MAX_SPELLS = 5;
    int32_t spellID[MAX_SPELLS] = {};
    int32_t spellTrigger[MAX_SPELLS] = {};
    int32_t spellCharges[MAX_SPELLS] = {};
    int32_t spellCooldown[MAX_SPELLS] = {};
    int32_t spellCategory[MAX_SPELLS] = {};
    int32_t spellCategoryCooldown[MAX_SPELLS] = {};

    int32_t bonding = 0;
    std::string description;

    int32_t pageText = 0;
    int32_t languageID = 0;
    int32_t pageMaterial = 0;
    int32_t startQuest = 0;
    int32_t lockID = 0;
    int32_t material = 0;
    int32_t sheath = 0;
    int32_t randomProperty = 0;
    int32_t randomSuffix = 0;
    int32_t block = 0;
    int32_t itemSet = 0;
    int32_t maxDurability = 0;
    int32_t area = 0;
    int32_t map = 0;
    int32_t bagFamily = 0;
    int32_t totemCategory = 0;

    static const int32_t MAX_SOCKETS = 3;
    int32_t socketColor[MAX_SOCKETS] = {};
    int32_t socketContent[MAX_SOCKETS] = {};

    int32_t socketBonus = 0;
    int32_t gemProperties = 0;
    int32_t requiredDisenchantSkill = 0;
    float armorDamageModifier = 0.0f;
    int32_t duration = 0;
    int32_t itemLimitCategory = 0;
    int32_t holidayID = 0;
};

// Returns the record when it is known, or null while the query is in flight or the entry is
// missing. Sends CMSG_ITEM_QUERY_SINGLE on the first miss and not again.
const ItemInfo* ItemCacheGet(int32_t entryID);

void ItemCacheRegisterHandlers();
void ItemCacheClear();

int32_t ReceiveItemQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
