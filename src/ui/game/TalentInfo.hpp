#ifndef UI_GAME_TALENT_INFO_HPP
#define UI_GAME_TALENT_INFO_HPP

#include <cstdint>

struct lua_State;

// The reference's TalentInfo.cpp.

// The talent group a call means when it names none: the player's active group, or 0 for the pet.
// ref: FUN_005c57a0
uint32_t TalentInfoDefaultGroup(int32_t pet);

// A 1-based group index argument, as the 0-based group; the active group when the argument is
// not a number.
// ref: FUN_005c57d0
uint32_t TalentInfoGroupArg(lua_State* L, int32_t index);

#endif
