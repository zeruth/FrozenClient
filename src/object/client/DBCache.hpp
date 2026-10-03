#ifndef OBJECT_CLIENT_DB_CACHE_HPP
#define OBJECT_CLIENT_DB_CACHE_HPP

#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "util/GUID.hpp"
#include <common/DataStore.hpp>
#include <storm/Hash.hpp>
#include <storm/List.hpp>
#include <storm/Memory.hpp>
#include <cstdint>

// The reference's DBCache (DBCache.cpp, DBCacheInstances.cpp): the client's asynchronous record
// caches -- creature, game object, item, name and the rest. Each is one instantiation of the same
// template, which is why the reference carries fifteen near-identical copies of every function
// (GetRecord at 0x0067b6a0 for creatures, 0x0067ca30 for items, 0x0067d770 for names, ...).
// Frozen writes the template once.
//
// How a lookup works, as the reference does it:
//
//   GetRecord(id, guid, callback, param, dedupe) answers the record at once when it has arrived.
//   When it has not, it registers `callback` on the entry and returns null -- and only when a
//   callback is supplied does a miss create the entry and SEND THE QUERY. A lookup without a
//   callback never asks the server; it only reads what is already there.
//
//   The server's answer runs the response (Response below), which reads the record, marks it
//   loaded and calls every callback registered on it with found = true. A "not found" answer
//   (bit 31 of the id) calls them with found = false and drops the entry.
//
//   Queries are rate limited when the instance was built with a rate: past the limit a new entry
//   joins the pending list instead of sending, and Update drains that list each period.
//
// Records persist between sessions in Cache\WDB\<locale>\<name>.wdb when the instance asks for it.
// The file format is the reference's own (the header is checked field for field on load), so the
// files frozen writes are the files the reference reads and the other way round.

// A key for a cache whose records are named by a 32-bit id: creature and item entries, quest ids.
// The hash value IS the id, as in the reference's lookup (FUN_006f6020 compares the node's first
// dword, its hash, against the id).
class DBCACHEKEY32 {
    public:
        DBCACHEKEY32() : m_id(0) {}
        DBCACHEKEY32(uint32_t id) : m_id(id) {}
        bool operator==(const DBCACHEKEY32& key) const { return this->m_id == key.m_id; }
        uint32_t Hash() const { return this->m_id; }
        uint32_t m_id;
};

// A key for a cache named by a 64-bit guid: names, pet names, guilds. The hash is the low dword
// and the comparison is the whole guid (FUN_006792e0).
class DBCACHEKEY64 {
    public:
        DBCACHEKEY64() : m_id(0) {}
        DBCACHEKEY64(WOWGUID id) : m_id(id) {}
        bool operator==(const DBCACHEKEY64& key) const { return this->m_id == key.m_id; }
        uint32_t Hash() const { return static_cast<uint32_t>(this->m_id); }
        WOWGUID m_id;
};

// The callback a lookup registers. The reference hands it the id's low dword (every instance,
// the guid-keyed ones too), the guid the lookup was made for, its own argument, and whether the
// record arrived.
typedef void (*DBCACHECALLBACKFN)(uint32_t id, const WOWGUID* guid, void* param, bool found);

// One registered callback (RTTI DBCACHECALLBACK, 0x20 bytes in the reference).
struct DBCACHECALLBACK {
    TSLink<DBCACHECALLBACK> m_link;     // +0x00
    DBCACHECALLBACKFN m_callback;       // +0x08
    WOWGUID m_guid;                     // +0x10
    void* m_param;                      // +0x18
};

// One cached record and its bookkeeping. The reference lays it out as the hash node (0x18 bytes),
// the record, the id again, the loaded flag, the callback list, three flags and the pending link;
// the offsets differ per instance only because the record does.
template <class T, class K>
class DBCACHEHASH : public TSHashObject<DBCACHEHASH<T, K>, K> {
    public:
        T m_record;
        K m_id;
        bool m_loaded = false;
        STORM_EXPLICIT_LIST(DBCACHECALLBACK, m_link) m_callbacks;
        // Set on a record the server marked as not to be kept (the name cache's FUN_0067a4c0);
        // Save skips it.
        bool m_noSave = false;
        // True while the callbacks are being called, so a callback that asks for the entry to go
        // only marks it (m_deletePending) rather than freeing what the loop is walking.
        bool m_dispatching = false;
        bool m_deletePending = false;
        TSLink<DBCACHEHASH<T, K>> m_pendingLink;

        ~DBCACHEHASH() {
            this->m_callbacks.Clear();
        }
};

template <class T, class K>
class DBCache {
    public:
        typedef DBCACHEHASH<T, K> Entry;

        // Constructor (FUN_00675c80 and its fourteen siblings). `ratePerMinute` is the query
        // budget: the reference keeps (rate * 30000) / 60000 per thirty-second period, and zero
        // means no limit.
        DBCache(uint32_t signature, const char* fileName, NETMESSAGE queryOpcode, uint32_t arg5,
                bool sendGuid, bool persist, uint32_t ratePerMinute)
            : m_signature(signature)
            , m_fileName(fileName)
            , m_session(0)
            , m_queryOpcode(queryOpcode)
            , m_arg40(arg5)
            , m_sendGuid(sendGuid)
            , m_persist(persist)
            , m_initialized(false)
            , m_maxQueries((ratePerMinute * 30000) / 60000)
            , m_sentQueries(0)
            , m_periodEnd(0) {
        }

        // The record for `id`, or null when it has not arrived. See the note at the top of the
        // file for what `callback` changes.
        const T* GetRecord(const K& id, const WOWGUID* guid, DBCACHECALLBACKFN callback, void* param,
                           bool dedupe);

        // The record when it has arrived, never querying. What a lookup without a callback is.
        const T* Peek(const K& id) {
            return this->GetRecord(id, nullptr, nullptr, nullptr, false);
        }

        // Take a callback back off an entry (FUN_00679e10 for creatures). An entry that is being
        // dispatched keeps its callbacks: the reference refuses and logs instead.
        void CancelCallback(const K& id, DBCACHECALLBACKFN callback, void* param);

        // The server's answer: `count` records (one when `single`, else a count byte first),
        // each its id followed by the record (FUN_0067b840 for creatures, GetI32/0x0067cbd0 for
        // items).
        void Response(CDataStore* msg, bool single);

        // Insert or replace a record that arrived some other way than Response -- the name cache's
        // own handler reads its record itself (FUN_00680c60). Calls the callbacks like Response.
        Entry* Store(const K& id, const T& record);

        // The server does not know `id`: call the callbacks with found = false and drop the entry
        // (FUN_00679d90 / FUN_0067a2d0).
        void NotFound(const K& id);

        // Drop a loaded record so the next lookup asks again (FUN_0067a940).
        void Invalidate(const K& id);

        // Put an entry back on the pending list, to be asked for again when the next period opens
        // (FUN_0067a360, the name cache's "try later" answer).
        void Requeue(const K& id) {
            if (auto entry = this->Find(id)) {
                this->m_pending.LinkToTail(entry);
            }
        }

        // The entry for `id`, loaded or not, or null.
        Entry* Find(const K& id) {
            return this->m_hash.Ptr(id.Hash(), id);
        }

        // Send the queries that were held back for the rate limit (FUN_00669350).
        void Update();

        // Drop every entry still waiting for its record (FUN_0066a010).
        void ClearUnloaded();

        // The session value SMSG_CLIENTCACHE_VERSION carries. A persistent cache whose saved
        // session differs throws everything away (FUN_0066f940).
        void SetSession(uint32_t session);

        // Read the saved records (FUN_0067ba40) and write them back (FUN_0066a090).
        void Load();
        void Save();

        // Save, then empty (FUN_0066f910).
        void Shutdown();

    private:
        // Make the entry a lookup needs and either send its query or hold it for the limit.
        Entry* NewEntry(const K& id);
        void SendQuery(Entry* entry);
        bool AtQueryLimit() const {
            return this->m_maxQueries != 0 && this->m_maxQueries <= this->m_sentQueries;
        }
        void DispatchCallbacks(Entry* entry, uint32_t id, bool found);
        void DeleteEntry(Entry* entry);

        TSHashTable<Entry, K> m_hash;           // +0x08
        uint32_t m_signature;                   // +0x30
        const char* m_fileName;                 // +0x34
        uint32_t m_session;                     // +0x38
        NETMESSAGE m_queryOpcode;               // +0x3c
        uint32_t m_arg40;                       // +0x40, the constructor's fifth argument
        bool m_sendGuid;                        // +0x44
        bool m_persist;                         // +0x45
        bool m_initialized;                     // +0x46
        uint32_t m_maxQueries;                  // +0x48
        uint32_t m_sentQueries;                 // +0x4c
        uint32_t m_periodEnd;                   // +0x50
        STORM_EXPLICIT_LIST(Entry, m_pendingLink) m_pending;    // +0x54
};

// The WDB directory, Cache\WDB\<locale> (FUN_00667ef0), created when missing.
void DBCacheGetDirectory(char* buffer, size_t size);

// The locale as the WDB header stores it: the four characters, first one highest ("enUS" is
// 0x656e5553). The reference keeps it at 0x00b38cd0.
uint32_t DBCacheGetLocaleTag();

// One WDB file, read and written whole.
bool DBCacheReadFile(const char* path, uint8_t** data, uint32_t* size);
bool DBCacheWriteFile(const char* path, const void* data, uint32_t size);

#include "object/client/DBCache.inl"

#endif
