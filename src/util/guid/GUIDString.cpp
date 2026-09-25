#include "util/guid/GUIDString.hpp"
#include <cstdint>

// ref: FUN_0074d0d0
void GUIDToHexString(WOWGUID guid, char* buffer) {
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[18] = '\0';

    char* out = buffer + 17;

    for (int32_t i = 16; i != 0; i--) {
        uint8_t nibble = guid & 0xF;
        *out = nibble < 10 ? nibble + '0' : nibble + ('A' - 10);
        guid >>= 4;
        out--;
    }
}
