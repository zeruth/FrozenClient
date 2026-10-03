// DBCache template bodies. Included at the foot of DBCache.hpp.
//
// The reference addresses are those of the CREATURE instance unless a line says otherwise; every
// other instance is the same code over a different record.

#include <common/Time.hpp>
#include <storm/String.hpp>
#include <cstring>

// ref: FUN_0067b6a0
template <class T, class K>
const T* DBCache<T, K>::GetRecord(const K& id, const WOWGUID* guid, DBCACHECALLBACKFN callback,
                                  void* param, bool dedupe) {
    if (id == K()) {
        return nullptr;
    }

    auto entry = this->Find(id);

    if (!entry) {
        // Only a lookup that wants to hear back creates the entry and asks the server. One that
        // passes no callback is a peek.
        if (callback) {
            entry = this->NewEntry(id);

            auto cb = STORM_NEW(DBCACHECALLBACK)();
            cb->m_callback = callback;
            cb->m_guid = guid ? *guid : 0;
            cb->m_param = param;
            entry->m_callbacks.LinkToTail(cb);

            if (this->AtQueryLimit()) {
                this->m_pending.LinkToTail(entry);

                return nullptr;
            }

            this->SendQuery(entry);
        }

        return nullptr;
    }

    if (entry->m_loaded) {
        return &entry->m_record;
    }

    if (callback) {
        // `dedupe` stops the same callback and argument being registered twice on one entry,
        // which is what a lookup made every frame would otherwise do.
        if (dedupe) {
            for (auto cb = entry->m_callbacks.Head(); cb; cb = entry->m_callbacks.Next(cb)) {
                if (cb->m_callback == callback && cb->m_param == param) {
                    return nullptr;
                }
            }
        }

        auto cb = STORM_NEW(DBCACHECALLBACK)();
        cb->m_callback = callback;
        cb->m_guid = guid ? *guid : 0;
        cb->m_param = param;
        entry->m_callbacks.LinkToTail(cb);
    }

    return nullptr;
}

// ref: FUN_00679e10
template <class T, class K>
void DBCache<T, K>::CancelCallback(const K& id, DBCACHECALLBACKFN callback, void* param) {
    auto entry = this->Find(id);

    if (!entry) {
        return;
    }

    if (entry->m_dispatching) {
        // "DBCache::CancelCallback ignored for id %016I64X." in the reference.
        return;
    }

    for (auto cb = entry->m_callbacks.Head(); cb; ) {
        auto next = entry->m_callbacks.Next(cb);

        if (cb->m_callback == callback && cb->m_param == param) {
            entry->m_callbacks.DeleteNode(cb);
        }

        cb = next;
    }
}

// ref: FUN_0067b840
template <class T, class K>
void DBCache<T, K>::Response(CDataStore* msg, bool single) {
    uint8_t count = 1;

    if (!single) {
        msg->Get(count);

        if (count == 0) {
            return;
        }
    }

    do {
        uint32_t raw = 0;
        msg->Get(raw);

        bool found = (raw & 0x80000000) == 0;
        uint32_t idValue = raw & 0x7FFFFFFF;
        K id(idValue);

        auto entry = this->Find(id);

        if (found) {
            if (!entry) {
                entry = this->m_hash.New(id.Hash(), id, 0, 0);
            }

            entry->m_dispatching = true;
            entry->m_record.Read(msg);
            entry->m_loaded = true;
            entry->m_id = id;

            for (auto cb = entry->m_callbacks.Head(); cb; ) {
                auto next = entry->m_callbacks.Next(cb);
                cb->m_callback(idValue, &cb->m_guid, cb->m_param, true);
                cb = next;
            }

            // FUN_00670070: the callbacks have been answered, so they go.
            entry->m_callbacks.Clear();

            entry->m_dispatching = false;

            if (entry->m_deletePending) {
                auto again = this->Find(id);

                if (again) {
                    if (!again->m_loaded) {
                        this->NotFound(id);
                    } else if (!again->m_dispatching) {
                        this->DeleteEntry(again);
                    } else {
                        again->m_deletePending = true;
                    }
                }
            }
        } else if (entry) {
            entry->m_dispatching = true;
            this->m_hash.Unlink(entry);

            for (auto cb = entry->m_callbacks.Head(); cb; ) {
                auto next = entry->m_callbacks.Next(cb);
                cb->m_callback(idValue, &cb->m_guid, cb->m_param, false);
                cb = next;
            }

            entry->m_dispatching = false;
            this->DeleteEntry(entry);
        }

        count--;
    } while (count != 0);
}

// ref: FUN_00680c60 (the name cache's store, the one instance that reads its own records)
template <class T, class K>
typename DBCache<T, K>::Entry* DBCache<T, K>::Store(const K& id, const T& record) {
    auto entry = this->Find(id);

    if (!entry) {
        entry = this->m_hash.New(id.Hash(), id, 0, 0);
    }

    entry->m_dispatching = true;
    entry->m_record = record;
    entry->m_id = id;
    entry->m_loaded = true;

    uint32_t idValue = id.Hash();

    for (auto cb = entry->m_callbacks.Head(); cb; ) {
        auto next = entry->m_callbacks.Next(cb);
        cb->m_callback(idValue, &cb->m_guid, cb->m_param, true);
        cb = next;
    }

    entry->m_callbacks.Clear();

    entry->m_dispatching = false;

    if (entry->m_deletePending) {
        this->Invalidate(id);

        return nullptr;
    }

    return entry;
}

// ref: FUN_00679d90
template <class T, class K>
void DBCache<T, K>::NotFound(const K& id) {
    auto entry = this->Find(id);

    if (!entry) {
        return;
    }

    entry->m_dispatching = true;

    for (auto cb = entry->m_callbacks.Head(); cb; ) {
        auto next = entry->m_callbacks.Next(cb);
        cb->m_callback(id.Hash(), &cb->m_guid, cb->m_param, false);
        cb = next;
    }

    entry->m_dispatching = false;

    this->m_hash.Unlink(entry);
    this->DeleteEntry(entry);
}

// ref: FUN_0067a940 (the pet name instance)
template <class T, class K>
void DBCache<T, K>::Invalidate(const K& id) {
    auto entry = this->Find(id);

    if (!entry) {
        return;
    }

    if (!entry->m_loaded) {
        this->NotFound(id);

        return;
    }

    if (entry->m_dispatching) {
        entry->m_deletePending = true;

        return;
    }

    this->m_hash.Unlink(entry);
    this->DeleteEntry(entry);
}

// ref: FUN_00669350
template <class T, class K>
void DBCache<T, K>::Update() {
    if (this->m_maxQueries == 0 || !this->m_initialized) {
        return;
    }

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    if (this->m_periodEnd != 0 && static_cast<int32_t>(now - this->m_periodEnd) < 0) {
        return;
    }

    this->m_sentQueries = 0;
    this->m_periodEnd = static_cast<uint32_t>(OsGetAsyncTimeMs()) + 30000;

    for (auto entry = this->m_pending.Head(); entry; ) {
        auto next = this->m_pending.Next(entry);

        entry->m_pendingLink.Unlink();
        this->SendQuery(entry);

        if (this->AtQueryLimit()) {
            break;
        }

        entry = next;
    }
}

// ref: FUN_0066a010
template <class T, class K>
void DBCache<T, K>::ClearUnloaded() {
    for (auto entry = this->m_hash.Head(); entry; ) {
        auto next = this->m_hash.Next(entry);

        if (!entry->m_loaded) {
            this->m_hash.Unlink(entry);
            this->DeleteEntry(entry);
        }

        entry = next;
    }
}

// ref: FUN_0066f940
template <class T, class K>
void DBCache<T, K>::SetSession(uint32_t session) {
    if (!this->m_persist || this->m_session == session) {
        return;
    }

    this->ClearUnloaded();
    this->m_hash.Clear();

    this->m_session = session;
}

// ref: FUN_0066f910
template <class T, class K>
void DBCache<T, K>::Shutdown() {
    this->Save();
    this->ClearUnloaded();
    this->m_hash.Clear();

    this->m_initialized = false;
}

// ref: FUN_0067ba40
// The file is a 0x18-byte header -- signature, build 12340 (0x3034), locale, record size, record
// version, session -- then (id, size, record bytes) triples, ending at a zero id. Any header field
// that does not match leaves the cache empty, which is how a client of another build or locale
// ignores the file rather than misreading it.
template <class T, class K>
void DBCache<T, K>::Load() {
    this->m_initialized = true;

    if (!this->m_persist) {
        return;
    }

    char dir[STORM_MAX_PATH];
    DBCacheGetDirectory(dir, sizeof(dir));

    char path[STORM_MAX_PATH];
    SStrPrintf(path, sizeof(path), "%s/%s", dir, this->m_fileName);

    uint8_t* data = nullptr;
    uint32_t size = 0;

    if (!DBCacheReadFile(path, &data, &size)) {
        return;
    }

    if (size < 0x18) {
        SMemFree(data, __FILE__, __LINE__, 0);

        return;
    }

    CDataStore header(data, 0x18);
    header.Finalize();

    uint32_t signature = 0, build = 0, locale = 0, recordSize = 0, version = 0, session = 0;
    header.Get(signature);
    header.Get(build);
    header.Get(locale);
    header.Get(recordSize);
    header.Get(version);

    if (signature != this->m_signature || build != 12340 || locale != DBCacheGetLocaleTag()
        || recordSize != T::RECORD_SIZE || version != T::RECORD_VERSION) {
        SMemFree(data, __FILE__, __LINE__, 0);

        return;
    }

    header.Get(session);
    this->m_session = session;

    bool corrupt = false;
    uint32_t pos = 0x18;

    while (pos + 8 <= size) {
        uint32_t id = 0;
        uint32_t length = 0;
        memcpy(&id, data + pos, 4);
        memcpy(&length, data + pos + 4, 4);
        pos += 8;

        if (id == 0) {
            break;
        }

        if (pos + length > size) {
            corrupt = true;
            break;
        }

        CDataStore record(data + pos, length);
        record.Finalize();
        pos += length;

        K key(id);
        auto entry = this->Find(key);

        if (!entry) {
            entry = this->m_hash.New(key.Hash(), key, 0, 0);
        }

        entry->m_record.Read(&record);
        entry->m_loaded = true;
        entry->m_id = key;
    }

    SMemFree(data, __FILE__, __LINE__, 0);

    if (corrupt) {
        // FUN_0066cc90: a truncated file is not trusted at all.
        this->m_hash.Clear();
    }
}

// ref: FUN_0066a090
template <class T, class K>
void DBCache<T, K>::Save() {
    if (!this->m_persist || !this->m_initialized) {
        return;
    }

    char dir[STORM_MAX_PATH];
    DBCacheGetDirectory(dir, sizeof(dir));

    char path[STORM_MAX_PATH];
    SStrPrintf(path, sizeof(path), "%s/%s", dir, this->m_fileName);

    CDataStore out;
    out.Put(this->m_signature);
    out.Put(static_cast<uint32_t>(12340));
    out.Put(DBCacheGetLocaleTag());
    out.Put(static_cast<uint32_t>(T::RECORD_SIZE));
    out.Put(static_cast<uint32_t>(T::RECORD_VERSION));
    out.Put(this->m_session);

    for (auto entry = this->m_hash.Head(); entry; entry = this->m_hash.Next(entry)) {
        if (!entry->m_loaded || entry->m_noSave) {
            continue;
        }

        out.Put(static_cast<uint32_t>(entry->m_id.Hash()));

        // The size is written as a placeholder and patched once the record has been written,
        // as the reference does (FUN_0047af90 on the position eight bytes back).
        uint32_t sizePos = out.Size();
        out.Put(static_cast<uint32_t>(0));

        uint32_t start = out.Size();
        entry->m_record.Write(&out);
        uint32_t length = out.Size() - start;

        out.Seek(sizePos);
        out.Put(length);
        out.Seek(out.Size());
    }

    // The terminating pair: a zero id and a zero size.
    out.Put(static_cast<uint32_t>(0));
    out.Put(static_cast<uint32_t>(0));

    const void* buffer = nullptr;
    uint32_t length = 0;
    uint32_t alloc = 0;
    out.GetBufferParams(&buffer, &length, &alloc);

    DBCacheWriteFile(path, buffer, out.Size());
}

// ref: FUN_0067af50
template <class T, class K>
typename DBCache<T, K>::Entry* DBCache<T, K>::NewEntry(const K& id) {
    auto entry = this->m_hash.New(id.Hash(), id, 0, 0);
    entry->m_id = id;

    return entry;
}

// ref: FUN_00668970
// The opcode, the id, and -- for the instances that want it -- the guid of the object the first
// lookup was made for, which the server uses to answer for that object.
template <class T, class K>
void DBCache<T, K>::SendQuery(Entry* entry) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(this->m_queryOpcode));
    T::PutQueryId(&msg, entry->m_id);

    if (this->m_sendGuid) {
        auto cb = entry->m_callbacks.Head();
        msg.Put(static_cast<uint64_t>(cb ? cb->m_guid : 0));
    }

    msg.Finalize();
    ClientServices::Send(&msg);

    this->m_sentQueries++;
}

template <class T, class K>
void DBCache<T, K>::DeleteEntry(Entry* entry) {
    this->m_hash.Unlink(entry);
    entry->m_pendingLink.Unlink();
    entry->~Entry();
    SMemFree(entry, __FILE__, __LINE__, 0);
}
