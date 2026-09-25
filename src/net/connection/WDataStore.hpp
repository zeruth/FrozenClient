#ifndef NET_CONNECTION_W_DATA_STORE_HPP
#define NET_CONNECTION_W_DATA_STORE_HPP

#include <common/datastore/CDataStore.hpp>
#include <cstdint>

// The network data store (vtable 0x009e2148). Its buffers come from one of three ObjectAlloc
// pools, picked by the allocation size, instead of from the Storm heap.
class WDataStore : public CDataStore {
    public:
        // Static variables
        // The pools, by block size: 0x300 (DAT_00b38cf0), 0x4000 (DAT_00b38cf4), 0x40040
        // (DAT_00b38cf8). Each points at its ObjectAlloc heap id.
        static uint32_t* s_heap300;
        static uint32_t* s_heap4000;
        static uint32_t* s_heap40040;

        // Virtual member functions
        virtual void InternalDestroy(uint8_t*& data, uint32_t& base, uint32_t& alloc);

        // Member variables
        // +0x18: the pooled block backing the buffer, or null when the buffer is a plain Storm
        // allocation. Its first dword is the block's ObjectAlloc handle.
        uint32_t* m_poolBlock = nullptr;
};

#endif
