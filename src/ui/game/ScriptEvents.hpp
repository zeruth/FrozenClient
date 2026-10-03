#ifndef UI_GAME_SCRIPT_EVENTS_HPP
#define UI_GAME_SCRIPT_EVENTS_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

extern const char* g_scriptEvents[];

uint32_t RuneGetCooldownStart(int32_t rune);

void ScriptEventsInitialize();

void ScriptEventsRegisterEvents();

void ScriptEventsRegisterFunctions();

// ref: FUN_0060bb70
// Every unit token that names `guid` right now, in the reference's order: player, vehicle, pet,
// the first party slot that matches (or its pet), the first raid slot, the first arena opponent,
// the first boss frame, then target, focus and mouseover. The array is static and is overwritten
// by the next call.
const char* const* ScriptEventsGetUnitTokens(WOWGUID guid, int32_t* count);

// ref: FUN_0060bf10
// Signal `event` once for every token that names `guid`, with the token as its argument. This is
// how UNIT_NAME_UPDATE and its siblings reach every frame showing the unit.
void ScriptEventsSignalUnitEvent(WOWGUID guid, int32_t event);

#endif
