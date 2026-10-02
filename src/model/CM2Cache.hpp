#ifndef MODEL_C_M2_CACHE_HPP
#define MODEL_C_M2_CACHE_HPP

#include "model/CM2Shared.hpp"
#include <cstdint>
#include <common/Prop.hpp>
#include <storm/List.hpp>
#include <storm/thread/SEvent.hpp>
#include <storm/thread/SThread.hpp>

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
        // +0x1004..+0x1018: the animate thread. It waits on m_threadStart, runs m_threadCallback
        // with m_threadArg, and signals m_threadDone; flag 0x400 says it is running and 0x800
        // tells it to leave. It runs in the property context of the thread that started it.
        SThread m_thread;
        SEvent m_threadStart = SEvent(0, 0);
        SEvent m_threadDone = SEvent(0, 0);
        HPROPCONTEXT m_threadPropContext = nullptr;
        void (*m_threadCallback)(void*) = nullptr;
        void* m_threadArg = nullptr;
        // +0x109c / +0x10a0: the time of the last UpdateShared, and the shared models whose
        // geometry buffers were used, least recently first; a model idle for 10 seconds gives
        // its buffers back.
        uint32_t m_geometryTime = 0;
        STORM_EXPLICIT_LIST(CM2Shared, m_geometryLink) m_geometryList;

        // Static functions
        static uint32_t ThreadProc(void* arg);

        // Member functions
        void BeginThread(void (*callback)(void*), void* arg);
        void ClearBuckets();
        void Destroy();
        CM2Shared* CreateShared(const char*, uint32_t);
        void GarbageCollect(int32_t all);
        void TouchGeometry(CM2Shared* shared);
        int32_t Initialize(uint32_t flags);
        void UpdateShared();
        void WaitThread();
};

#endif
