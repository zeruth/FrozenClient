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
        char name[256] = { 0 };
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

    info.known = true;
    info.missing = false;

    return 1;
}
