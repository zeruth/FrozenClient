#include "util/DynamicString.hpp"
#include <storm/Memory.hpp>

// ref: FUN_0095d690
DynamicString::~DynamicString() {
    if (this->m_data) {
        SMemFree(this->m_data, __FILE__, __LINE__, 0x0);
    }
}

// ref: FUN_0095d820
void DynamicString::Grow() {
    auto capacity = (this->m_length * 3 >> 1) + 16;
    this->m_capacity = capacity;

    if (!this->m_data) {
        auto data = static_cast<char*>(SMemAlloc(capacity, __FILE__, __LINE__, 0x0));
        this->m_data = data;
        *data = '\0';

        return;
    }

    this->m_data = static_cast<char*>(SMemReAlloc(this->m_data, capacity, __FILE__, __LINE__, 0x0));
}

// ref: FUN_0095d760
void DynamicString::Resize(int32_t size) {
    if (size < 1) {
        this->m_length = 0;
        this->m_capacity = 0;

        if (this->m_data) {
            SMemFree(this->m_data, __FILE__, __LINE__, 0x0);
        }

        this->m_data = nullptr;

        return;
    }

    if (this->m_data) {
        if (size <= this->m_length) {
            this->m_length = size - 1;
            this->m_data[size - 1] = '\0';

            auto data = static_cast<char*>(SMemReAlloc(this->m_data, size, __FILE__, __LINE__, 0x0));
            this->m_capacity = size;
            this->m_data = data;

            return;
        }

        auto data = static_cast<char*>(SMemReAlloc(this->m_data, size, __FILE__, __LINE__, 0x0));
        this->m_capacity = size;
        this->m_data = data;

        return;
    }

    auto data = static_cast<char*>(SMemAlloc(size, __FILE__, __LINE__, 0x0));
    this->m_capacity = size;
    this->m_data = data;
    this->m_length = 0;
    *data = '\0';
}
