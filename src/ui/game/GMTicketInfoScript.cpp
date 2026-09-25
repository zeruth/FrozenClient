#include "ui/game/GMTicketInfoScript.hpp"
#include <common/DataStore.hpp>
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cmath>
#include <storm/Memory.hpp>
#include <storm/String.hpp>

namespace {

// The survey the player is filling in. Written here and read when the survey is submitted.
uint8_t s_surveyAnswerRanks[10];     // ref: DAT_00c1e8c4
char* s_surveyAnswerComments[10];    // ref: DAT_00c1e8d8
char* s_surveyComment;               // ref: DAT_00c1e900

// ref: FUN_005ac260
void SetSurveyAnswer(uint32_t question, uint8_t rank, const char* comment) {
    if (static_cast<int32_t>(question) < 0 || question >= 10) {
        return;
    }

    s_surveyAnswerRanks[question] = rank;

    if (s_surveyAnswerComments[question]) {
        SMemFree(s_surveyAnswerComments[question], __FILE__, __LINE__, 0);
    }

    s_surveyAnswerComments[question] = nullptr;

    if (comment && *comment) {
        s_surveyAnswerComments[question] = SStrDupA(comment, __FILE__, __LINE__);
    }
}

// ref: FUN_005ac2d0
void SetSurveyComment(const char* comment) {
    if (s_surveyComment) {
        SMemFree(s_surveyComment, __FILE__, __LINE__, 0);
    }

    s_surveyComment = nullptr;

    if (comment && *comment) {
        s_surveyComment = SStrDupA(comment, __FILE__, __LINE__);
    }
}

int32_t Script_GetGMTicket(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_NewGMTicket(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UpdateGMTicket(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_DeleteGMTicket(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GMResponseNeedMoreHelp(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GMResponseResolve(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_005ad1c0
// Asks the server; the answer arrives later as an event, so this pushes nothing.
int32_t Script_GetGMStatus(lua_State* L) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_GM_TICKET_GET_SYSTEM_STATUS));
    msg.Finalize();
    ClientServices::Send(&msg);

    return 0;
}

int32_t Script_GMSurveyQuestion(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GMSurveyNumAnswers(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GMSurveyAnswer(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_005ac390
int32_t Script_GMSurveyAnswerSubmit(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isstring(L, 3)) {
        luaL_error(L, "Usage: GMSurveyAnswerSubmit(question, rank, comment)");

        return 0;
    }

    auto question = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1)))) - 1;
    auto rank = lua_tonumber(L, 2);
    auto comment = lua_tostring(L, 3);

    if (question < 10) {
        SetSurveyAnswer(question, static_cast<uint8_t>(llrint(rank)), comment);

        return 0;
    }

    luaL_error(L, "GMSurveyAnswerSubmit: Questions limited from %d to %d", 1, 10);

    return 0;
}

// ref: FUN_005ac480
int32_t Script_GMSurveyCommentSubmit(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: GMSurveyCommentSubmit(comment)");

        return 0;
    }

    SetSurveyComment(lua_tostring(L, 1));

    return 0;
}

int32_t Script_GMSurveySubmit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GMReportLag(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RegisterStaticConstants(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GetGMTicket",                &Script_GetGMTicket },
    { "NewGMTicket",                &Script_NewGMTicket },
    { "UpdateGMTicket",             &Script_UpdateGMTicket },
    { "DeleteGMTicket",             &Script_DeleteGMTicket },
    { "GMResponseNeedMoreHelp",     &Script_GMResponseNeedMoreHelp },
    { "GMResponseResolve",          &Script_GMResponseResolve },
    { "GetGMStatus",                &Script_GetGMStatus },
    { "GMSurveyQuestion",           &Script_GMSurveyQuestion },
    { "GMSurveyNumAnswers",         &Script_GMSurveyNumAnswers },
    { "GMSurveyAnswer",             &Script_GMSurveyAnswer },
    { "GMSurveyAnswerSubmit",       &Script_GMSurveyAnswerSubmit },
    { "GMSurveyCommentSubmit",      &Script_GMSurveyCommentSubmit },
    { "GMSurveySubmit",             &Script_GMSurveySubmit },
    { "GMReportLag",                &Script_GMReportLag },
    { "RegisterStaticConstants",    &Script_RegisterStaticConstants },
};

void GMTicketInfoRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
