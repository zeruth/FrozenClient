#ifndef OBJECT_CLIENT_NAME_CACHE_HPP
#define OBJECT_CLIENT_NAME_CACHE_HPP

#include "net/Types.hpp"
#include "util/GUID.hpp"
#include <cstdint>

class CDataStore;
class CGObject;

// Names for the objects in the world (the reference's DBCache name / creature caches). A lookup
// returns the cached name, or null while the query is in flight; players resolve through
// CMSG_NAME_QUERY by guid, creatures through CMSG_QUERY_CREATURE by entry.
const char* NameCacheGetName(CGObject* object);

// True once a name is known for the object (no query sent)
bool NameCacheHasName(CGObject* object);

void NameCacheRegisterHandlers();
void NameCacheClear();

int32_t ReceiveNameQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ReceiveCreatureQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
