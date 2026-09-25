#include "net/connection/WDataStore.hpp"
#include <common/ObjectAlloc.hpp>
#include <storm/Memory.hpp>

// ref: DAT_00b38cf0
uint32_t* WDataStore::s_heap300;
// ref: DAT_00b38cf4
uint32_t* WDataStore::s_heap4000;
// ref: DAT_00b38cf8
uint32_t* WDataStore::s_heap40040;

// ref: FUN_00466190
void WDataStore::InternalDestroy(uint8_t*& data, uint32_t& base, uint32_t& alloc) {
    if (!this->m_poolBlock) {
        if (data) {
            SMemFree(data, __FILE__, __LINE__, 0);
        }
    } else {
        uint32_t* heap;

        if (alloc == 0x300) {
            heap = WDataStore::s_heap300;
        } else if (alloc == 0x4000) {
            heap = WDataStore::s_heap4000;
        } else if (alloc == 0x40040) {
            heap = WDataStore::s_heap40040;
        } else {
            goto done;
        }

        ObjectFree(*heap, *this->m_poolBlock);
        this->m_poolBlock = nullptr;
    }

done:
    data = nullptr;
    base = 0;
    alloc = 0;
}
