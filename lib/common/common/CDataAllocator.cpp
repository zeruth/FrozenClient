#include "common/CDataAllocator.hpp"
#include <storm/Memory.hpp>
#include <cstring>

// ref: FUN_0095d110
void* CDataAllocator::GetData(int32_t zero, const char* fileName, int32_t lineNumber) {
    if (this->m_dataPerBlock == 1) {
        fileName = nullptr;
        lineNumber = 0;
    }

    if (!this->m_freeList) {
        if (!fileName) {
            lineNumber = 155;
            fileName = ".\\CDataAllocator.cpp";
        }

        auto block = static_cast<void**>(SMemAlloc(this->m_bytesPerData * this->m_dataPerBlock + sizeof(void*), fileName, lineNumber, 0x0));
        auto data = reinterpret_cast<uint8_t*>(block + 1);

        this->m_freeList = data;

        if (this->m_dataPerBlock != 1) {
            for (uint32_t i = 0; i < this->m_dataPerBlock - 1; i++) {
                auto next = data + this->m_bytesPerData;
                *reinterpret_cast<void**>(data) = next;
                data = next;
            }
        }

        *reinterpret_cast<void**>(data) = nullptr;

        *block = this->m_blockList;
        this->m_blockList = block;
    }

    auto data = this->m_freeList;
    this->m_freeList = *static_cast<void**>(data);

    if (zero) {
        memset(data, 0, this->m_bytesPerData);
    }

    this->m_dataCount++;

    return data;
}

// ref: FUN_0095d1b0
void CDataAllocator::PutData(void* data) {
    *static_cast<void**>(data) = this->m_freeList;
    this->m_dataCount--;
    this->m_freeList = data;
}
