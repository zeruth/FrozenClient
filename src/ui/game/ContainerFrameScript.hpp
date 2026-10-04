#ifndef UI_GAME_CONTAINER_FRAME_SCRIPT_HPP
#define UI_GAME_CONTAINER_FRAME_SCRIPT_HPP

#include "util/GUID.hpp"
#include <cstdint>

class CGItem_C;
struct lua_State;

void ContainerFrameScriptRegisterFunctions();

// The item object in a bag slot, or null. The bag is 0-based (the backpack is 0) and the slot is
// 1-based, which is how FrameXML passes them.
CGItem_C* Script_GetContainerItem(lua_State* L, int32_t bagArg, int32_t slotArg);

// ref: FUN_005d6f10
void ContainerSignalBagUpdateCooldown();

// ref: FUN_005d6f20
// A bag's guid by 0-based index: 0-3 the worn bags, 4-10 the bank bags. 0 out of range.
WOWGUID ContainerGetBagGuid(uint32_t index);

class CGItem_C;

// An item's inventory icon name, without directory or extension. ref: FUN_0070aa00
const char* ItemDisplayIcon(CGItem_C* item);

// The inventory icon name of an ItemDisplayInfo row, remembered once found;
// "INV_Misc_QuestionMark" when it has none.
const char* ItemDisplayGetIcon(int32_t displayID);

#endif
