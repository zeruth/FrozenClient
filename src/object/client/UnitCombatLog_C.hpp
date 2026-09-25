#ifndef OBJECT_CLIENT_UNIT_COMBAT_LOG_C_HPP
#define OBJECT_CLIENT_UNIT_COMBAT_LOG_C_HPP

#include <cstdint>

// Writers for a combat log entry's argument text. Each advances `cursor` and takes what it writes
// off `remaining`, puts a comma first while more than one byte is left, and never terminates the
// text.

// Lower-case hex with a 0x prefix, written only while more than nine bytes are left.
void CombatLogAppendHex(char*& cursor, uint32_t& remaining, uint32_t value);

void CombatLogAppendString(char*& cursor, uint32_t& remaining, const char* str);

// The string in double quotes, with quote and backslash escaped by a backslash.
void CombatLogAppendQuotedString(char*& cursor, uint32_t& remaining, const char* str);

// Whether the object flags carry any of the special bits in `mask` (0xffff0000), or at least one
// bit of each of its affiliation (0xf), reaction (0xf0), control (0x300) and type (0xfc00) groups.
bool CombatLogObjectFlagsMatch(uint32_t flags, uint32_t mask);

#endif
