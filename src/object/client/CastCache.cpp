#include "object/client/CastCache.hpp"
#include "client/ClientServices.hpp"
#include <common/DataStore.hpp>
#include <common/Time.hpp>
#include <map>

namespace {

std::map<WOWGUID, ClientCast> s_casts;

// Server->client packed guid: a mask byte, then one byte per set bit, low to high.
WOWGUID GetPackedGuid(CDataStore* msg) {
    uint8_t mask = 0;
    msg->Get(mask);

    WOWGUID guid = 0;

    for (int32_t i = 0; i < 8; i++) {
        if (mask & (1 << i)) {
            uint8_t b = 0;
            msg->Get(b);
            guid |= static_cast<WOWGUID>(b) << (i * 8);
        }
    }

    return guid;
}

bool Remaining(CDataStore* msg, uint32_t bytes) {
    return msg && msg->Tell() + bytes <= msg->Size();
}

} // namespace

const ClientCast* CastCacheGet(WOWGUID guid, bool channeled) {
    auto it = s_casts.find(guid);

    if (it == s_casts.end() || it->second.channeled != channeled) {
        return nullptr;
    }

    // A cast whose end has passed is finished, whether or not the server has told us yet: the
    // completion packet can be lost or late, and a bar that never empties is worse than one that
    // clears a frame early.
    if (it->second.endMs && OsGetAsyncTimeMs() >= it->second.endMs) {
        s_casts.erase(it);

        return nullptr;
    }

    return &it->second;
}

void CastCacheClear() {
    s_casts.clear();
}

// SMSG_SPELL_START:
//   packed castItemGuid (the caster when no item is involved)
//   packed casterGuid
//   uint8  castCount
//   uint32 spellID
//   uint32 castFlags
//   int32  timer                 milliseconds of cast left
//   ... spell targets, then optional blocks
//
// Everything this client needs sits before the variable-length target block, so the prefix is read
// and the rest ignored rather than porting SpellCastTargets to reach nothing beyond it.
int32_t ReceiveSpellStart(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!Remaining(msg, 2)) {
        return 1;
    }

    GetPackedGuid(msg); // cast item, unused
    WOWGUID caster = GetPackedGuid(msg);

    if (!Remaining(msg, 13)) {
        return 1;
    }

    uint8_t castCount = 0;
    uint32_t spellID = 0;
    uint32_t castFlags = 0;
    int32_t timer = 0;

    msg->Get(castCount);
    msg->Get(spellID);
    msg->Get(castFlags);
    msg->Get(*reinterpret_cast<uint32_t*>(&timer));

    if (!spellID || timer <= 0) {
        // An instant cast has no bar to show.
        s_casts.erase(caster);

        return 1;
    }

    ClientCast cast;
    cast.spellID = static_cast<int32_t>(spellID);
    cast.startMs = static_cast<uint32_t>(OsGetAsyncTimeMs());
    cast.endMs = cast.startMs + static_cast<uint32_t>(timer);
    cast.channeled = false;

    s_casts[caster] = cast;

    return 1;
}

// SMSG_SPELL_GO marks the cast as actually going off, which ends the bar.
int32_t ReceiveSpellGo(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!Remaining(msg, 2)) {
        return 1;
    }

    GetPackedGuid(msg); // cast item
    WOWGUID caster = GetPackedGuid(msg);

    auto it = s_casts.find(caster);

    // Only a cast ends here. A channel's SPELL_GO arrives at the START of the channel, so treating
    // it as an end would clear the bar the instant it appeared.
    if (it != s_casts.end() && !it->second.channeled) {
        s_casts.erase(it);
    }

    return 1;
}

// SMSG_SPELL_FAILURE / SMSG_SPELL_FAILED_OTHER: an interrupted or refused cast stops the bar.
int32_t ReceiveSpellFailure(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!Remaining(msg, 1)) {
        return 1;
    }

    s_casts.erase(GetPackedGuid(msg));

    return 1;
}

// MSG_CHANNEL_START: packed casterGuid, uint32 spellID, uint32 duration.
int32_t ReceiveChannelStart(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!Remaining(msg, 1)) {
        return 1;
    }

    WOWGUID caster = GetPackedGuid(msg);

    if (!Remaining(msg, 8)) {
        return 1;
    }

    uint32_t spellID = 0;
    uint32_t duration = 0;

    msg->Get(spellID);
    msg->Get(duration);

    if (!spellID) {
        s_casts.erase(caster);

        return 1;
    }

    ClientCast cast;
    cast.spellID = static_cast<int32_t>(spellID);
    cast.startMs = static_cast<uint32_t>(OsGetAsyncTimeMs());
    cast.endMs = cast.startMs + duration;
    cast.channeled = true;

    s_casts[caster] = cast;

    return 1;
}

// MSG_CHANNEL_UPDATE: packed casterGuid, uint32 remaining. Zero means the channel has ended.
int32_t ReceiveChannelUpdate(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!Remaining(msg, 1)) {
        return 1;
    }

    WOWGUID caster = GetPackedGuid(msg);

    if (!Remaining(msg, 4)) {
        return 1;
    }

    uint32_t remaining = 0;
    msg->Get(remaining);

    auto it = s_casts.find(caster);

    if (it == s_casts.end() || !it->second.channeled) {
        return 1;
    }

    if (!remaining) {
        s_casts.erase(it);

        return 1;
    }

    // The server is authoritative on how much is left, so the end moves rather than the start: a
    // channel shortened by haste or extended by a tick should not rewrite when it began.
    it->second.endMs = static_cast<uint32_t>(OsGetAsyncTimeMs()) + remaining;

    return 1;
}

void CastCacheRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_SPELL_START, &ReceiveSpellStart, nullptr);
    ClientServices::SetMessageHandler(SMSG_SPELL_GO, &ReceiveSpellGo, nullptr);
    ClientServices::SetMessageHandler(SMSG_SPELL_FAILURE, &ReceiveSpellFailure, nullptr);
    ClientServices::SetMessageHandler(SMSG_SPELL_FAILED_OTHER, &ReceiveSpellFailure, nullptr);
    ClientServices::SetMessageHandler(MSG_CHANNEL_START, &ReceiveChannelStart, nullptr);
    ClientServices::SetMessageHandler(MSG_CHANNEL_UPDATE, &ReceiveChannelUpdate, nullptr);
}
