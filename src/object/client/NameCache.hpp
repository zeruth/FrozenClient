#ifndef OBJECT_CLIENT_NAME_CACHE_HPP
#define OBJECT_CLIENT_NAME_CACHE_HPP

#include "net/Types.hpp"
#include "util/GUID.hpp"
#include <cstdint>
#include <string>

class CDataStore;
class CGObject;

// Names for the objects in the world (the reference's DBCache name / creature caches). A lookup
// returns the cached name, or null while the query is in flight; players resolve through
// CMSG_NAME_QUERY by guid, creatures through CMSG_QUERY_CREATURE by entry.
const char* NameCacheGetName(CGObject* object);

// True once a name is known for the object (no query sent)
bool NameCacheHasName(CGObject* object);

// One creature template as the server sent it, cached by creature entry.
//
// The creature query reply has always carried this; frozen read the name out of it and dropped
// the rest on the floor, which is why UnitCreatureType, UnitCreatureFamily and UnitClassification
// were all stubbed at once. They are three reads of this record.
//
// The reference hangs the record off the unit (CGUnit_C +0x964) and reads fields straight out of
// it -- +0x08 icon name, +0x0c type flags, +0x10 type, +0x14 family, +0x18 classification. Frozen
// keeps it in the cache and looks it up by entry, so it has no reason to mirror those offsets.
//
// Field ORDER is the wire order, and it was verified rather than assumed: the reference client
// writes these records back out verbatim, and its own Cache/WDB/enUS/creaturecache.wdb decodes
// cleanly under exactly this layout. All 61 records there leave 77 bytes after the six strings,
// which is 12 dwords, then the single racialLeader byte, then 7 more dwords -- no slack, so the
// one unaligned byte is placed by the data and not by guesswork.
struct CreatureCacheRec {
    std::string name;
    std::string subName;
    // The verb the cursor uses over this creature -- "Speak", "Trainer", "Directions". The
    // reference builds "Unable<icon>" from it when interaction is refused (FUN_00715ea0).
    std::string iconName;
    uint32_t typeFlags = 0;
    int32_t type = 0;
    int32_t family = 0;
    int32_t classification = 0;
    int32_t killCredit[2] = { 0, 0 };
    int32_t displayID[4] = { 0, 0, 0, 0 };
    float healthMultiplier = 0.0f;
    float powerMultiplier = 0.0f;
    uint8_t racialLeader = 0;
    int32_t questItemID[6] = { 0, 0, 0, 0, 0, 0 };
    int32_t movementInfoID = 0;
};

// The cached template for a creature entry, or null when nothing has arrived for it yet. Never
// sends a query of its own -- NameCacheGetName is what puts an entry in flight.
const CreatureCacheRec* NameCacheGetCreatureInfo(int32_t entryID);

void NameCacheRegisterHandlers();
void NameCacheClear();

int32_t ReceiveNameQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ReceiveCreatureQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
