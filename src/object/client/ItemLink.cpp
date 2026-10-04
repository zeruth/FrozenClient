#include "object/client/ItemLink.hpp"

#include "db/Db.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "ui/FrameScript.hpp"

#include <storm/String.hpp>
#include <cstring>

namespace {

// ref: PTR_s__cff9d9d9d_00ad2a84
// Indexed by quality. Artifact and heirloom share one string in the reference -- the same pointer
// appears twice -- so they are not distinct colours despite being distinct qualities.
const char* s_qualityColours[8] = {
    "|cff9d9d9d", // poor
    "|cffffffff", // common
    "|cff1eff00", // uncommon
    "|cff0070dd", // rare
    "|cffa335ee", // epic
    "|cffff8000", // legendary
    "|cffe6cc80", // artifact
    "|cffe6cc80", // heirloom
};

// The reference formats into one shared 1024-byte buffer and returns a pointer to it, so every
// caller has to consume the link before building another. Kept, rather than returned by value,
// because callers push it straight to Lua and the lifetime question never arises.
char s_link[1024];                                  // ref: DAT_00c5cf50

// Step past the next ':' in a link and read the number after it. False, with the cursor left at
// the end, when there is no further field.
bool ItemLinkNextField(const char*& cursor, int32_t& value) {
    while (*cursor) {
        if (*cursor == ':') {
            cursor++;
            value = SStrToInt(cursor);

            return true;
        }

        cursor++;
    }

    return false;
}

} // namespace

// ref: FUN_0050f590
void ITEMLINKINFO::Reset() {
    this->itemID = 0;
    memset(this->enchant, 0, sizeof(this->enchant));
    memset(this->enchantDuration, 0, sizeof(this->enchantDuration));
    memset(this->enchantCharges, 0, sizeof(this->enchantCharges));
    this->cooldown = 0;
    this->proposedEnchant = 0;
    this->creator = 0;
    this->giftCreator = 0;
    this->suffixFactor = 0;
    this->randomPropertyID = 0;
    this->locked = 0;
    this->lockID = 0;
    this->maxDurability = 0;
    this->durability = 0;
    this->level = 0;
    this->charges = -1;
    this->stackCount = 1;
    this->maxStackCount = -1;
}

// ref: FUN_0050f630
// "item:id:enchant:gem1:gem2:gem3:gem4:suffix:seed:level", read as far as it goes. The fourth gem
// lands in enchantment slot 5, the one after the three sockets. Returns the item id, or 0 when the
// text holds no item link or its id is not positive.
int32_t ITEMLINKINFO::Parse(const char* link) {
    this->Reset();

    auto start = SStrStr(link, "item:");

    if (!start) {
        return 0;
    }

    const char* cursor = start + 5;
    this->itemID = SStrToInt(cursor);

    if (this->itemID <= 0) {
        return 0;
    }

    ItemLinkNextField(cursor, this->enchant[0]);

    for (int32_t i = 2; i < 5; i++) {
        ItemLinkNextField(cursor, this->enchant[i]);
    }

    if (ItemLinkNextField(cursor, this->enchant[5])
        && ItemLinkNextField(cursor, this->randomPropertyID)
        && ItemLinkNextField(cursor, this->suffixFactor)) {
        ItemLinkNextField(cursor, this->level);
    }

    return this->itemID;
}

// ref: FUN_00706d70
// A positive suffix names an ItemRandomProperties row and decorates only when that row has a name;
// a negative one names an ItemRandomSuffix row and decorates even with an empty name.
char* ItemNameFromEntry(char* dest, uint32_t destSize, int32_t entryID, int32_t suffix) {
    *dest = '\0';

    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(entryID)));

    if (!info || info->name.empty()) {
        return nullptr;
    }

    auto name = info->name.c_str();
    const char* suffixName = nullptr;

    if (suffix >= 1) {
        auto property = g_itemRandomPropertiesDB.GetRecord(suffix);

        if (property && property->m_name && property->m_name[0]) {
            suffixName = property->m_name;
        }
    }

    if (!suffixName) {
        if (suffix >= 0) {
            SStrCopy(dest, name, destSize);

            return dest;
        }

        auto randomSuffix = g_itemRandomSuffixDB.GetRecord(-suffix);
        suffixName = randomSuffix ? randomSuffix->m_name : "";
    }

    auto format = FrameScript_GetText("ITEM_SUFFIX_TEMPLATE", -1, GENDER_NOT_APPLICABLE);
    SStrPrintf(dest, destSize, format, name, suffixName);

    return dest;
}

// ref: FUN_0061e290
const char* ItemLinkBuild(int32_t entryID, int32_t quality, int32_t enchant,
                          const ITEM_LINK_GEMS& gems, int32_t suffix, int32_t seed) {
    // The player's level is baked into every link. It is what lets a recipient's client decide
    // whether to show the item as usable, and it is the LINKING player's level, not the reader's.
    auto player = CGPlayer_C::GetActivePtr();
    auto level = player ? player->Unit()->level : 0;

    // Out of range (an unsigned compare, so negative too) is common white -- not poor grey.
    auto colour = s_qualityColours[static_cast<uint32_t>(quality) > 7 ? 1 : quality];

    // The trailing reset is dropped when the colour is empty, so a link never carries a stray |r.
    auto reset = colour[0] ? "|r" : "";

    char name[256];
    ItemNameFromEntry(name, sizeof(name), entryID, suffix);

    // Nine numeric fields. The seventh is always zero: a fourth gem socket that nothing fills.
    SStrPrintf(
        s_link, sizeof(s_link),
        "%s|Hitem:%d:%d:%d:%d:%d:%d:%d:%d:%d|h[%s]|h%s",
        colour, entryID, enchant, gems.gem[0], gems.gem[1], gems.gem[2], 0, suffix, seed, level,
        name, reset
    );

    return s_link;
}

// ref: FUN_0061e360
const char* ItemLinkBuild(int32_t entryID, int32_t quality, int32_t enchant, int32_t suffix, int32_t seed) {
    ITEM_LINK_GEMS gems;

    return ItemLinkBuild(entryID, quality, enchant, gems, suffix, seed);
}

// ref: FUN_0061e3a0
// A gift-wrapped item (descriptor flag 0x8) links as its bare entry: no enchant, gems or random
// property. A charter's enchantment fields hold its petition, so they are not read as enchants.
const char* ItemLinkFromObject(CGItem_C* item) {
    if (!item) {
        return nullptr;
    }

    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(item->GetEntryID())));
    auto data = item->Item();
    bool wrapped = (data->flags >> 3) & 1;

    ITEM_LINK_GEMS gems;

    for (int32_t i = 0; i < 3; i++) {
        gems.gem[i] = !wrapped && !item->IsCharter() ? data->enchantments[2 + i].id : 0;
    }

    int32_t enchant = 0;
    int32_t suffix = 0;
    int32_t seed = 0;

    if (!wrapped) {
        seed = data->propertySeed;
        suffix = data->randomPropertiesID;
        enchant = item->IsCharter() ? 0 : data->enchantments[0].id;
    }

    // An item whose cache record has not arrived still links, as common quality. The reference
    // uses 1 rather than waiting or failing.
    auto quality = info ? info->quality : 1;

    return ItemLinkBuild(item->GetEntryID(), quality, enchant, gems, suffix, seed);
}
