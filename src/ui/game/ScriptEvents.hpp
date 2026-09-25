#ifndef UI_GAME_SCRIPT_EVENTS_HPP
#define UI_GAME_SCRIPT_EVENTS_HPP

#include <cstdint>

extern const char* g_scriptEvents[];

uint32_t RuneGetCooldownStart(int32_t rune);

void ScriptEventsInitialize();

void ScriptEventsRegisterEvents();

void ScriptEventsRegisterFunctions();

#endif
