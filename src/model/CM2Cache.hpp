#ifndef MODEL_C_M2_CACHE_HPP
#define MODEL_C_M2_CACHE_HPP

#include "model/CM2Shared.hpp"
#include <cstdint>
#include <storm/List.hpp>

class CM2Shared;

class CM2Cache {
    public:
        // Static variables
        static CM2Cache s_cache;

        // Member variables
        uint32_t m_initialized = 0;
        uint32_t m_flags = 0;
        // +0x8 / +0xc: shared models nobody references any more, oldest first, each held for
        // 9999ms in case it is asked for again (CM2Shared::Release queues, AddRef revives,
        // GarbageCollect destroys).
        CM2Shared* m_freeListHead = nullptr;
        CM2Shared** m_freeListTail = &this->m_freeListHead;
        // +0x10: 1021 hash buckets of shared models, each chain ordered by hash then name.
        CM2Shared* m_buckets[0x3fd] = {};
        // +0x109c / +0x10a0: the time of the last UpdateShared, and the shared models whose
        // geometry buffers were used, least recently first; a model idle for 10 seconds gives
        // its buffers back.
        uint32_t m_geometryTime = 0;
        STORM_EXPLICIT_LIST(CM2Shared, m_geometryLink) m_geometryList;

        // Member functions
        void BeginThread(void (*callback)(void*), void* arg);
        CM2Shared* CreateShared(const char*, uint32_t);
        void GarbageCollect(int32_t all);
        void TouchGeometry(CM2Shared* shared);
        int32_t Initialize(uint32_t flags);
        void UpdateShared();
        void WaitThread();
};

#endif
