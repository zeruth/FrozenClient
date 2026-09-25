#ifndef UTIL_BIG_ENDIAN_HPP
#define UTIL_BIG_ENDIAN_HPP

#include <cstdint>

uint32_t BigEndianReadU32(const uint8_t** cursor);

void BigEndianReadU32(const uint8_t** cursor, uint32_t* value);

#endif
