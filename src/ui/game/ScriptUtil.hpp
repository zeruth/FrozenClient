#ifndef UI_GAME_SCRIPT_UTIL_HPP
#define UI_GAME_SCRIPT_UTIL_HPP

#include "util/GUID.hpp"
#include <cstdint>

class CGItem_C;
class CGUnit_C;
struct lua_State;

CGUnit_C* Script_GetUnitFromName(const char* name);

// The item object in one of a unit's inventory slots, or null. The slot arrives 1-based from Lua
// -- GetInventorySlotInfo hands FrameXML the number this expects -- and indexes invSlots, which
// carries the equipped pieces followed by the bag slots.
//
// Only the player's own inventory resolves: another unit's slots are never sent.
CGItem_C* Script_GetInventoryItem(lua_State* L, int32_t unitArg, int32_t slotArg);

// Equip locations as FrameXML names them, indexed by an item record's inventory type, and the
// count so callers can range-check. Read out of the reference's PTR_DAT_00ac7fd8; index 0 is
// deliberately empty, for an item that equips nowhere. Defined in GameScript.cpp.
extern const char* s_equipLocations[];
extern const int32_t EQUIP_LOCATION_COUNT;

bool Script_GetGUIDFromString(const char*& token, WOWGUID& guid);

bool Script_GetGUIDFromToken(const char* token, WOWGUID& guid, bool defaultToTarget);

// The inverse: the token FrameXML would know this unit by, or null. Walks the candidates and asks
// the function above, so the two cannot disagree about what a token means.
const char* Script_GetTokenFromGUID(WOWGUID guid);

#endif
