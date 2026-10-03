#ifndef UI_GAME_CHARACTER_INFO_SCRIPT_HPP
#define UI_GAME_CHARACTER_INFO_SCRIPT_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

class CGObject_C;

void CharacterInfoRegisterScriptFunctions();

// The player the inspect frame shows (DAT_00c24220).
extern WOWGUID s_inspectGUID;

int32_t IsActivePlayerOrInspectTarget(CGObject_C* object);

#endif
