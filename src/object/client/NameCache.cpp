#include "object/client/NameCache.hpp"
#include "ui/game/Types.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "object/client/ItemCache.hpp"
#include "object/client/CGObject.hpp"
#include "object/Types.hpp"
#include "client/ClientServices.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"

#include <common/DataStore.hpp>
#include <storm/String.hpp>
#include <map>
#include <string>

namespace {

struct CachedName {
    std::string name;
    bool pending = false;
};

std::map<WOWGUID, CachedName> s_playerNames;    // by guid (CMSG_NAME_QUERY)
std::map<int32_t, CachedName> s_creatureNames;  // by creature entry (CMSG_QUERY_CREATURE)
std::map<int32_t, CreatureCacheRec> s_creatureInfo;  // the rest of that same reply, by entry

// Server->client packed guid: a mask byte, then one byte per set bit (low to high)
uint64_t GetPackedGuid(CDataStore* msg) {
    uint8_t mask = 0;
    msg->Get(mask);
    uint64_t guid = 0;

    for (int32_t i = 0; i < 8; i++) {
        if (mask & (1 << i)) {
            uint8_t b = 0;
            msg->Get(b);
            guid |= static_cast<uint64_t>(b) << (i * 8);
        }
    }

    return guid;
}

void QueryPlayerName(WOWGUID guid) {
    CachedName& entry = s_playerNames[guid];

    if (entry.pending) {
        return;
    }

    entry.pending = true;

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_NAME_QUERY));
    msg.Put(static_cast<uint64_t>(guid));
    msg.Finalize();
    ClientServices::Send(&msg);
}

void QueryCreatureName(int32_t entryID, WOWGUID guid) {
    CachedName& entry = s_creatureNames[entryID];

    if (entry.pending) {
        return;
    }

    entry.pending = true;

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_QUERY_CREATURE));
    msg.Put(static_cast<uint32_t>(entryID));
    msg.Put(static_cast<uint64_t>(guid));
    msg.Finalize();
    ClientServices::Send(&msg);
}

CachedName* Find(CGObject* object, bool query) {
    if (!object) {
        return nullptr;
    }

    if (object->IsA(TYPE_PLAYER)) {
        WOWGUID guid = object->GetGUID();
        auto it = s_playerNames.find(guid);

        if (it == s_playerNames.end() || (it->second.name.empty() && !it->second.pending)) {
            if (query) {
                QueryPlayerName(guid);
            }

            it = s_playerNames.find(guid);
        }

        return it == s_playerNames.end() ? nullptr : &it->second;
    }

    if (object->IsA(TYPE_UNIT)) {
        int32_t entryID = object->GetEntryID();
        auto it = s_creatureNames.find(entryID);

        if (it == s_creatureNames.end() || (it->second.name.empty() && !it->second.pending)) {
            if (query) {
                QueryCreatureName(entryID, object->GetGUID());
            }

            it = s_creatureNames.find(entryID);
        }

        return it == s_creatureNames.end() ? nullptr : &it->second;
    }

    return nullptr;
}

} // namespace

const char* NameCacheGetName(CGObject* object) {
    // Items are not in this cache and never were: Find handles players by guid and units by entry
    // and returns null for everything else, so an item object in the world had no name at all.
    // Their names live in the item cache, keyed the same way creatures are -- by entry -- and the
    // lookup there sends its own query on the first miss.
    if (object && object->IsA(TYPE_ITEM)) {
        auto info = ItemCacheGet(object->GetEntryID());

        return (info && !info->name.empty()) ? info->name.c_str() : nullptr;
    }

    CachedName* entry = Find(object, true);
    return (entry && !entry->name.empty()) ? entry->name.c_str() : nullptr;
}

const CreatureCacheRec* NameCacheGetCreatureInfo(int32_t entryID) {
    auto it = s_creatureInfo.find(entryID);

    return it == s_creatureInfo.end() ? nullptr : &it->second;
}

bool NameCacheHasName(CGObject* object) {
    CachedName* entry = Find(object, false);
    return entry && !entry->name.empty();
}

void NameCacheClear() {
    s_playerNames.clear();
    s_creatureNames.clear();
    s_creatureInfo.clear();
}

void NameCacheRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_QUERY_PLAYER_NAME_RESPONSE, &ReceiveNameQueryResponse, nullptr);
    ClientServices::SetMessageHandler(SMSG_QUERY_CREATURE_RESPONSE, &ReceiveCreatureQueryResponse, nullptr);
}

// SMSG_NAME_QUERY_RESPONSE (0x51): packed guid, u8 not-found, name, realm, u8 race, u8 gender,
// u8 class, u8 declined-names flag (+ 5 declined names when set)
int32_t ReceiveNameQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint64_t guid = GetPackedGuid(msg);
    uint8_t notFound = 0;
    msg->Get(notFound);

    CachedName& entry = s_playerNames[guid];
    entry.pending = false;

    if (notFound) {
        entry.name = "Unknown";
        return 1;
    }

    char name[64] = { 0 };
    msg->GetString(name, sizeof(name));
    entry.name = name[0] ? name : "Unknown";

    // Tell the interface the name arrived.
    //
    // The reply is asynchronous, so by the time it lands every frame that asked has already drawn
    // whatever UnitName answered at the time -- "Unknown" -- and nothing would ever ask again. That
    // is why the player frame kept showing "Unknown" even after UnitName itself was fixed: the
    // binding was right and the label was simply never refreshed.
    //
    // The event carries a unit token and the cache is keyed by guid, so this used to signal only
    // for the player -- the one token that could be recognised without a reverse lookup. There is
    // one now: Script_GetTokenFromGUID, beside the resolver it inverts. So the target's name, a
    // pet's and a party member's all refresh too, which is what "Unknown" on a target frame was
    // waiting for.
    auto token = Script_GetTokenFromGUID(guid);

    if (token) {
        FrameScript_SignalEvent(SCRIPT_UNIT_NAME_UPDATE, "%s", token);
    }

    return 1;
}

// SMSG_CREATURE_QUERY_RESPONSE (0x61): u32 entry (high bit set when unknown), then the whole
// creature template -- four names, subname, icon name, and the numeric fields.
//
// The tail used to be ignored. It is where a creature's type, family and classification live, so
// three Lua bindings sat stubbed over a packet that was already arriving with the answers in it.
int32_t ReceiveCreatureQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t entryID = 0;
    msg->Get(entryID);

    if (entryID & 0x80000000) {
        CachedName& entry = s_creatureNames[static_cast<int32_t>(entryID & 0x7FFFFFFF)];
        entry.pending = false;
        entry.name = "Unknown";
        return 1;
    }

    char name[128] = { 0 };
    msg->GetString(name, sizeof(name));

    // Names 1..3 are the other-gender and other-form spellings. They have to be read to stay in
    // step with the stream, but the reference keeps only the first: its cached record holds three
    // string pointers, not six.
    char alternate[128];
    for (int32_t i = 0; i < 3; i++) {
        msg->GetString(alternate, sizeof(alternate));
    }

    char subName[128] = { 0 };
    msg->GetString(subName, sizeof(subName));

    char iconName[128] = { 0 };
    msg->GetString(iconName, sizeof(iconName));

    CreatureCacheRec& rec = s_creatureInfo[static_cast<int32_t>(entryID)];
    rec.name = name;
    rec.subName = subName;
    rec.iconName = iconName;

    uint32_t value = 0;
    msg->Get(rec.typeFlags);
    msg->Get(value);
    rec.type = static_cast<int32_t>(value);
    msg->Get(value);
    rec.family = static_cast<int32_t>(value);
    msg->Get(value);
    rec.classification = static_cast<int32_t>(value);

    for (int32_t i = 0; i < 2; i++) {
        msg->Get(value);
        rec.killCredit[i] = static_cast<int32_t>(value);
    }

    for (int32_t i = 0; i < 4; i++) {
        msg->Get(value);
        rec.displayID[i] = static_cast<int32_t>(value);
    }

    msg->Get(rec.healthMultiplier);
    msg->Get(rec.powerMultiplier);

    // One byte, not a dword, and everything after it is unaligned. The record sizes in the
    // reference's own cache file are what settles that: 77 bytes of fields, which only works as
    // 12 dwords + 1 byte + 7 dwords.
    msg->Get(rec.racialLeader);

    for (int32_t i = 0; i < 6; i++) {
        msg->Get(value);
        rec.questItemID[i] = static_cast<int32_t>(value);
    }

    msg->Get(value);
    rec.movementInfoID = static_cast<int32_t>(value);

    CachedName& entry = s_creatureNames[static_cast<int32_t>(entryID)];
    entry.pending = false;
    entry.name = name[0] ? name : "Unknown";

    return 1;
}
