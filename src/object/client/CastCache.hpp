#ifndef OBJECT_CLIENT_CAST_CACHE_HPP
#define OBJECT_CLIENT_CAST_CACHE_HPP

#include "net/Types.hpp"
#include "util/guid/Types.hpp"
#include <cstdint>

class CDataStore;

// What each visible unit is currently casting or channelling.
//
// Like the auras, this is state the server publishes on its own opcodes rather than in the object
// update blocks, so without handlers UnitCastingInfo and UnitChannelInfo have nothing to answer with
// and the casting bar never appears.
struct ClientCast {
    int32_t spellID = 0;
    uint32_t startMs = 0;   // OsGetAsyncTimeMs() when the cast was seen
    uint32_t endMs = 0;     // startMs + the duration the server sent
    bool channeled = false;
};

// Null when the unit is not casting, or when what it is doing is the other kind: a caller asking for
// a cast should not be handed a channel, since FrameXML calls both and distinguishes by which one
// answers.
const ClientCast* CastCacheGet(WOWGUID guid, bool channeled);

void CastCacheClear();

void CastCacheRegisterHandlers();

int32_t ReceiveSpellStart(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

int32_t ReceiveSpellGo(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

int32_t ReceiveSpellFailure(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

int32_t ReceiveChannelStart(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

int32_t ReceiveChannelUpdate(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
