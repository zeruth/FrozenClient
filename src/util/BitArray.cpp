#include "util/BitArray.hpp"
#include <storm/Memory.hpp>

// ref: FUN_0095da10
BitArray::BitArray() {
    this->m_bits = nullptr;
    this->m_count = 0;
    this->m_owned = false;
}

// ref: FUN_0095da80
BitArray::~BitArray() {
    if (this->m_bits && this->m_owned) {
        delete[] this->m_bits;
    }
}

// ref: FUN_0095daa0
void BitArray::Assign(uint8_t* bits, uint32_t count, bool owned) {
    if (this->m_bits && this->m_owned) {
        delete[] this->m_bits;
    }

    this->m_bits = bits;
    this->m_count = count;
    this->m_owned = owned;
}

// ref: FUN_0095da20
bool BitArray::IsSet(uint32_t index) const {
    return ((1 << (index & 7)) & this->m_bits[index >> 3]) != 0;
}

// ref: FUN_0095da50
void BitArray::Set(uint32_t index, bool value) {
    auto byte = &this->m_bits[index >> 3];
    uint8_t mask = 1 << (index & 7);

    if (value) {
        *byte |= mask;

        return;
    }

    *byte &= ~mask;
}
