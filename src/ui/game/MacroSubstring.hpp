#ifndef UI_GAME_MACRO_SUBSTRING_HPP
#define UI_GAME_MACRO_SUBSTRING_HPP

#include <cstdint>

// A [begin, end) view into a macro's text, the unit the macro option parser works in. The name is
// the reference's own, from the type name its array allocations carry.
struct MacroSubstring {
    const char* m_begin = nullptr;
    const char* m_end = nullptr;

    // Trim leading spaces from begin; end is either `end` or, when `terminator` is nonzero, the
    // first `terminator` before it (reported through terminatorPos). Trailing spaces are trimmed.
    void Set(const char* begin, const char* end, char terminator, const char** terminatorPos);

    // Copy into dest, truncated to fit destSize with its terminator.
    void Copy(char* dest, int32_t destSize) const;

    // The leading decimal digits as a number.
    int32_t ToInt() const;
};

#endif
