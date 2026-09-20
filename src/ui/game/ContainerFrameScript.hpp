#ifndef UI_GAME_CONTAINER_FRAME_SCRIPT_HPP
#define UI_GAME_CONTAINER_FRAME_SCRIPT_HPP

#include <cstdint>

class CGItem_C;
struct lua_State;

void ContainerFrameScriptRegisterFunctions();

// The item object in a bag slot, or null. The bag is 0-based (the backpack is 0) and the slot is
// 1-based, which is how FrameXML passes them.
CGItem_C* Script_GetContainerItem(lua_State* L, int32_t bagArg, int32_t slotArg);

#endif
