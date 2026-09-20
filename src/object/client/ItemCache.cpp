#include "object/client/ItemCache.hpp"
#include "client/ClientServices.hpp"

#include <common/DataStore.hpp>
#include <map>

namespace {

std::map<int32_t, ItemInfo> s_items;

// CDataStore has no signed overload, so read the dword and narrow at the call site. Every field
// below is a dword on the wire regardless of how the record stores it.
int32_t GetI32(CDataStore* msg) {
    uint32_t value = 0;
    msg->Get(value);

    return static_cast<int32_t>(value);
}

void QueryItem(int32_t entryID) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_ITEM_QUERY_SINGLE));
    msg.Put(static_cast<uint32_t>(entryID));
    // The reference pairs the entry with the guid of the item that prompted the lookup. A query
    // made from an entry alone -- which is every call that reaches here -- has no such item, and
    // the server answers on the entry regardless.
    msg.Put(static_cast<uint64_t>(0));
    msg.Finalize();
    ClientServices::Send(&msg);
}

} // namespace

const ItemInfo* ItemCacheGet(int32_t entryID) {
    if (entryID <= 0) {
        return nullptr;
    }

    auto it = s_items.find(entryID);

    if (it != s_items.end()) {
        return it->second.known ? &it->second : nullptr;
    }

    // Insert first, so the in-flight entry suppresses the duplicate queries a tooltip would
    // otherwise send on every frame it is up.
    s_items[entryID];
    QueryItem(entryID);

    return nullptr;
}

void ItemCacheClear() {
    s_items.clear();
}

void ItemCacheRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_ITEM_QUERY_SINGLE_RESPONSE, &ReceiveItemQueryResponse, nullptr);
}

// SMSG_ITEM_QUERY_SINGLE_RESPONSE (0x58). Field order read out of the reference's record reader at
// 0098d910 and written up in docs/ref/parity-itemcache.md; the high bit of the entry marks an item
// the server does not have, the same convention the creature response uses.
//
// Only the leading fields are read. Everything past requiredLevel -- the skill and reputation
// requirements, the stat block, damage, spells and the description -- is left in the buffer, which
// is safe because each response is its own message and nothing reads on from here. Extend
// downwards in the documented order when a binding needs more.
int32_t ReceiveItemQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t entryID = 0;
    msg->Get(entryID);

    if (entryID & 0x80000000) {
        auto& missing = s_items[static_cast<int32_t>(entryID & 0x7FFFFFFF)];
        missing.known = true;
        missing.missing = true;

        return 1;
    }

    auto& info = s_items[static_cast<int32_t>(entryID)];

    info.itemClass = GetI32(msg);
    info.subClass = GetI32(msg);
    info.soundOverrideSubclass = GetI32(msg);

    // Four name fields. Only the first is the item's name; the reference keeps all four and stores
    // a null for any that arrive empty.
    for (int32_t i = 0; i < 4; i++) {
        char name[400] = { 0 };
        msg->GetString(name, sizeof(name));

        if (i == 0) {
            info.name = name;
        }
    }

    info.displayInfoID = GetI32(msg);
    info.quality = GetI32(msg);
    msg->Get(info.flags);
    msg->Get(info.flags2);
    info.buyPrice = GetI32(msg);
    info.sellPrice = GetI32(msg);
    info.inventoryType = GetI32(msg);
    info.allowableClass = GetI32(msg);
    info.allowableRace = GetI32(msg);
    info.itemLevel = GetI32(msg);
    info.requiredLevel = GetI32(msg);
    info.requiredSkill = GetI32(msg);
    info.requiredSkillRank = GetI32(msg);
    info.requiredSpell = GetI32(msg);
    info.requiredHonorRank = GetI32(msg);
    info.requiredCityRank = GetI32(msg);
    info.requiredReputationFaction = GetI32(msg);
    info.requiredReputationRank = GetI32(msg);
    info.maxCount = GetI32(msg);
    info.stackable = GetI32(msg);
    info.containerSlots = GetI32(msg);

    // The stat block.
    //
    // DIVERGENCE, deliberately: the reference reads statsCount pairs with no bound at all, into a
    // ten-slot pair of arrays. A server sending eleven would walk it off the end of the record and
    // into the fields below. That is a bug, not behaviour worth reproducing, so the read stays in
    // step with the stream -- every pair is consumed -- while only the first ten are stored.
    info.statsCount = GetI32(msg);

    for (int32_t i = 0; i < info.statsCount; i++) {
        int32_t type = GetI32(msg);
        int32_t value = GetI32(msg);

        if (i < ItemInfo::MAX_STATS) {
            info.statType[i] = type;
            info.statValue[i] = value;
        }
    }

    // The reference marks the unused slots by TYPE only, leaving their values at zero, so a reader
    // that walks the array stops on -1 rather than trusting the count.
    for (int32_t i = info.statsCount; i < ItemInfo::MAX_STATS; i++) {
        info.statType[i] = -1;
    }

    info.scalingStatDistribution = GetI32(msg);
    info.scalingStatValue = GetI32(msg);

    // Two damage bands. Note the interleave: min, max and school are read together per band, not
    // as three runs, even though the record stores them as three parallel arrays.
    for (int32_t i = 0; i < ItemInfo::MAX_DAMAGES; i++) {
        msg->Get(info.damageMin[i]);
        msg->Get(info.damageMax[i]);
        info.damageType[i] = GetI32(msg);
    }

    info.armor = GetI32(msg);

    for (int32_t i = 0; i < 6; i++) {
        info.resistance[i] = GetI32(msg);
    }

    info.delay = GetI32(msg);
    info.ammoType = GetI32(msg);
    msg->Get(info.rangedModRange);

    // Six parallel arrays, read one index across all six before advancing -- the wire order, and
    // the reason this is a loop over slots rather than six separate runs.
    for (int32_t i = 0; i < ItemInfo::MAX_SPELLS; i++) {
        info.spellID[i] = GetI32(msg);
        info.spellTrigger[i] = GetI32(msg);
        info.spellCharges[i] = GetI32(msg);
        info.spellCooldown[i] = GetI32(msg);
        info.spellCategory[i] = GetI32(msg);
        info.spellCategoryCooldown[i] = GetI32(msg);
    }

    info.bonding = GetI32(msg);

    {
        // The reference reads the description into a 1024 byte buffer and the names above into 400
        // byte ones. Matching those sizes matters: GetString truncates to the buffer, so a smaller
        // one here would silently clip a long description.
        char description[1024] = { 0 };
        msg->GetString(description, sizeof(description));
        info.description = description;
    }

    info.pageText = GetI32(msg);
    info.languageID = GetI32(msg);
    info.pageMaterial = GetI32(msg);
    info.startQuest = GetI32(msg);
    info.lockID = GetI32(msg);
    info.material = GetI32(msg);
    info.sheath = GetI32(msg);
    info.randomProperty = GetI32(msg);
    info.randomSuffix = GetI32(msg);
    info.block = GetI32(msg);
    info.itemSet = GetI32(msg);
    info.maxDurability = GetI32(msg);
    info.area = GetI32(msg);
    info.map = GetI32(msg);
    info.bagFamily = GetI32(msg);
    info.totemCategory = GetI32(msg);

    for (int32_t i = 0; i < ItemInfo::MAX_SOCKETS; i++) {
        info.socketColor[i] = GetI32(msg);
        info.socketContent[i] = GetI32(msg);
    }

    info.socketBonus = GetI32(msg);
    info.gemProperties = GetI32(msg);
    info.requiredDisenchantSkill = GetI32(msg);
    msg->Get(info.armorDamageModifier);
    info.duration = GetI32(msg);
    info.itemLimitCategory = GetI32(msg);
    info.holidayID = GetI32(msg);

    // That is the whole record. holidayID is the last field the reference reads.

    info.known = true;
    info.missing = false;

    return 1;
}
