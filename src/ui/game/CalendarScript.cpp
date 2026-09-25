#include "ui/game/CalendarScript.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"

namespace {

// Set while a calendar request is waiting on the server. Nothing sets it yet: the calendar
// requests that do are not ported.
// ref: DAT_00c207f3
uint8_t s_calendarActionPending;

// ref: FUN_005b8c10
int32_t Script_CalendarIsActionPending(lua_State* L) {
    lua_pushboolean(L, s_calendarActionPending);

    return 1;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "CalendarIsActionPending", &Script_CalendarIsActionPending },
};

void CalendarRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
