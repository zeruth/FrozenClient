#include "ui/game/CalendarScript.hpp"
#include "ui/game/Calendar.hpp"
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

// ref: FUN_005b84d0
int32_t Script_CalendarEventGetNumInvites(lua_State* L) {
    uint32_t count;

    if (s_calendarEvent == nullptr) {
        count = 0;
    } else {
        count = s_calendarEvent->m_numInvites;
    }

    lua_pushnumber(L, static_cast<double>(count));

    return 1;
}

// ref: FUN_005b8690
int32_t Script_CalendarEventGetInviteSortCriterion(lua_State* L) {
    CalendarInviteSortCriterion* sort;

    if (s_calendarEvent == nullptr) {
        sort = nullptr;
    } else {
        sort = &s_calendarEvent->m_sortCriteria[s_calendarEvent->m_sortCriterion];
    }

    uint8_t reverse = 0;
    const char* criterion = "";

    if (sort) {
        switch (sort->m_criterion) {
            case 0:
                criterion = "name";
                break;

            case 1:
                criterion = "level";
                break;

            case 2:
                criterion = "class";
                break;

            case 3:
                criterion = "status";
                break;

            case 4:
                criterion = "party";
                break;

            case 5:
                criterion = "notes";
                break;
        }

        reverse = sort->m_reverse;
    }

    lua_pushstring(L, criterion);
    lua_pushboolean(L, reverse);

    return 2;
}

// ref: FUN_005b8a10
int32_t Script_CalendarEventHaveSettingsChanged(lua_State* L) {
    if (s_calendarEvent) {
        lua_pushboolean(L, s_calendarEvent->m_settingsChanged);

        return 1;
    }

    lua_pushboolean(L, 0);

    return 1;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "CalendarIsActionPending",                &Script_CalendarIsActionPending },
    { "CalendarEventGetNumInvites",             &Script_CalendarEventGetNumInvites },
    { "CalendarEventGetInviteSortCriterion",    &Script_CalendarEventGetInviteSortCriterion },
    { "CalendarEventHaveSettingsChanged",       &Script_CalendarEventHaveSettingsChanged },
};

void CalendarRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
