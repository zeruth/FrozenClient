#include "util/BigEndian.hpp"

// Readers for the big-endian structures behind the Battle.net code (reference 0x888a00 onward);
// each consumes four bytes and advances the cursor past them.

// ref: FUN_00889e50
uint32_t BigEndianReadU32(const uint8_t** cursor) {
    uint32_t value = 0;

    for (int32_t i = 4; i != 0; i--) {
        value = (value << 8) | **cursor;
        (*cursor)++;
    }

    return value;
}

// ref: FUN_0088a200
void BigEndianReadU32(const uint8_t** cursor, uint32_t* value) {
    *value = 0;

    for (int32_t i = 4; i != 0; i--) {
        *value = *value << 8;
        *value = **cursor | *value;
        (*cursor)++;
    }
}
