#include "object/client/UnitCombatLog_C.hpp"

// ref: FUN_0074d280
void CombatLogAppendHex(char*& cursor, uint32_t& remaining, uint32_t value) {
    if (remaining > 1) {
        *cursor++ = ',';
        remaining--;
    }

    if (remaining <= 9) {
        return;
    }

    *cursor++ = '0';
    *cursor++ = 'x';
    remaining -= 2;

    if (value == 0) {
        *cursor++ = '0';
        remaining--;

        return;
    }

    uint32_t digitMask = 0xF0000000;
    int32_t digits = 8;

    if ((value & 0xF0000000) == 0) {
        do {
            digitMask >>= 4;
            digits--;
        } while ((value & digitMask) == 0);
    }

    cursor += digits;

    do {
        cursor--;
        uint8_t nibble = value & 0xF;
        char c = nibble < 10 ? nibble + '0' : nibble + ('a' - 10);
        value >>= 4;
        *cursor = c;
    } while (value != 0);

    cursor += digits;
    remaining -= digits;
}

// ref: FUN_0074d310
void CombatLogAppendString(char*& cursor, uint32_t& remaining, const char* str) {
    if (remaining > 1) {
        *cursor++ = ',';
        remaining--;
    }

    char c = *str;

    while (c != '\0' && remaining != 0) {
        *cursor++ = c;
        remaining--;
        c = str[1];
        str++;
    }
}

// ref: FUN_0074d350
void CombatLogAppendQuotedString(char*& cursor, uint32_t& remaining, const char* str) {
    if (remaining > 1) {
        *cursor++ = ',';
        remaining--;

        if (remaining > 1) {
            *cursor++ = '"';
            remaining--;
        }
    }

    char c = *str;

    while (c != '\0' && remaining != 0) {
        if ((c == '"' || c == '\\') && remaining > 1) {
            *cursor++ = '\\';
            remaining--;
        }

        *cursor++ = *str;
        remaining--;
        c = str[1];
        str++;
    }

    if (remaining > 1) {
        *cursor++ = '"';
        remaining--;
    }
}

// ref: FUN_0074d1a0
bool CombatLogObjectFlagsMatch(uint32_t flags, uint32_t mask) {
    uint32_t bits = mask & flags;

    if (bits & 0xFFFF0000) {
        return true;
    }

    if ((bits & 0xF) && (bits & 0xF0) && (bits & 0x300)) {
        return (bits & 0xFC00) != 0;
    }

    return false;
}
