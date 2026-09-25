#include "ui/game/MacroSubstring.hpp"
#include <cstring>

// ref: FUN_005eea40
void MacroSubstring::Set(const char* begin, const char* end, char terminator, const char** terminatorPos) {
    this->m_begin = begin;

    while (begin < end && *this->m_begin == ' ') {
        begin = this->m_begin + 1;
        this->m_begin = begin;
    }

    if (terminator == '\0') {
        this->m_end = end;
    } else {
        this->m_end = this->m_begin;

        while (this->m_end < end && *this->m_end != terminator) {
            this->m_end++;
        }

        if (terminatorPos) {
            *terminatorPos = this->m_end;
        }
    }

    while (this->m_begin < this->m_end && this->m_end[-1] == ' ') {
        this->m_end--;
    }
}

// ref: FUN_005eeac0
void MacroSubstring::Copy(char* dest, int32_t destSize) const {
    uint32_t length = static_cast<uint32_t>(this->m_end - this->m_begin);
    uint32_t size = static_cast<uint32_t>(destSize - 1);

    if (length <= size) {
        size = length;
    }

    memcpy(dest, this->m_begin, size);
    dest[size] = '\0';
}

// ref: FUN_005eeb00
int32_t MacroSubstring::ToInt() const {
    int32_t value = 0;

    for (const char* c = this->m_begin; c < this->m_end && *c > '/' && *c < ':'; c++) {
        value = *c - '0' + value * 10;
    }

    return value;
}
