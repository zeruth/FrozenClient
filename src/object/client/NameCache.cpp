#include "object/client/NameCache.hpp"
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
    CachedName* entry = Find(object, true);
    return (entry && !entry->name.empty()) ? entry->name.c_str() : nullptr;
}

bool NameCacheHasName(CGObject* object) {
    CachedName* entry = Find(object, false);
    return entry && !entry->name.empty();
}

void NameCacheClear() {
    s_playerNames.clear();
    s_creatureNames.clear();
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
    // The event carries a unit token, and the cache is keyed by guid, so only tokens that can be
    // resolved without a reverse lookup are signalled. "player" is the one that matters here.
    if (guid == ClntObjMgrGetActivePlayer()) {
        // 144 is UNIT_NAME_UPDATE in g_scriptEvents; there is no constant for it.
        FrameScript_SignalEvent(144, "%s", "player");
    }

    return 1;
}

// SMSG_CREATURE_QUERY_RESPONSE (0x61): u32 entry (high bit set when unknown), name x4, subname,
// icon name, then the creature template fields the world text does not need
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

    CachedName& entry = s_creatureNames[static_cast<int32_t>(entryID)];
    entry.pending = false;
    entry.name = name[0] ? name : "Unknown";

    return 1;
}
