#ifndef UI_GAME_SCRIPT_UTIL_HPP
#define UI_GAME_SCRIPT_UTIL_HPP

#include "util/GUID.hpp"

class CGUnit_C;

CGUnit_C* Script_GetUnitFromName(const char* name);

bool Script_GetGUIDFromString(const char*& token, WOWGUID& guid);

bool Script_GetGUIDFromToken(const char* token, WOWGUID& guid, bool defaultToTarget);

// The inverse: the token FrameXML would know this unit by, or null. Walks the candidates and asks
// the function above, so the two cannot disagree about what a token means.
const char* Script_GetTokenFromGUID(WOWGUID guid);

#endif
