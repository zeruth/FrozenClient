#ifndef UI_GAME_CHARACTER_INFO_SCRIPT_HPP
#define UI_GAME_CHARACTER_INFO_SCRIPT_HPP

#include <cstdint>

class CGObject_C;

void CharacterInfoRegisterScriptFunctions();

int32_t IsActivePlayerOrInspectTarget(CGObject_C* object);

#endif
