#ifndef UTIL_GUID_GUID_STRING_HPP
#define UTIL_GUID_GUID_STRING_HPP

#include "util/guid/Types.hpp"

// "0x" and sixteen upper-case hex digits, terminated: `buffer` takes 19 bytes.
void GUIDToHexString(WOWGUID guid, char* buffer);

#endif
