#include "ui/game/ClassTrainerFrame.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"

// Nothing fills these yet: the trainer list handler that does is not ported.
// ref: DAT_00c0dc90
static char s_trainerGreetingText[0x800];
// ref: DAT_00c0e49c
static int32_t s_trainerSelectedService;
// ref: DAT_00c0e4a0
static uint32_t s_numTrainerServices;
// ref: DAT_00c0e4bc
static TrainerService** s_trainerServices;

// ref: FUN_00594170
// The list position of the selected service, or 0xFFFFFFFF when nothing is selected or the
// selection is not in the list.
uint32_t TrainerGetSelectionIndex() {
    if (s_trainerSelectedService != 0) {
        uint32_t i = 0;

        while (i < s_numTrainerServices) {
            if (s_trainerServices[i]->id == s_trainerSelectedService) {
                break;
            }

            i++;
        }

        if (i != s_numTrainerServices) {
            return i;
        }
    }

    return 0xFFFFFFFF;
}

// ref: FUN_00594240
TrainerService* TrainerGetService(uint32_t index) {
    if (index >= s_numTrainerServices) {
        return nullptr;
    }

    return s_trainerServices[index];
}

namespace {

// ref: FUN_00593d90
int32_t Script_GetTrainerGreetingText(lua_State* L) {
    lua_pushstring(L, s_trainerGreetingText);

    return 1;
}

// ref: FUN_00594480
int32_t Script_GetTrainerServiceCost(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetTrainerServiceCost(index)");

        return 0;
    }

    auto index = static_cast<int32_t>(lua_tonumber(L, 1));

    int32_t moneyCost = 0;
    int32_t talentCost;
    int32_t professionCost;

    TrainerService* service;

    if (static_cast<uint32_t>(index - 1) < s_numTrainerServices && (service = s_trainerServices[index - 1])) {
        talentCost = service->talentCost;
        moneyCost = service->moneyCost;
        professionCost = service->professionCost;
    } else {
        talentCost = 0;
        professionCost = 0;
    }

    lua_pushnumber(L, moneyCost);
    lua_pushnumber(L, talentCost);
    lua_pushnumber(L, professionCost);

    return 3;
}

// ref: FUN_00594530
int32_t Script_GetTrainerServiceLevelReq(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetTrainerServiceLevelReq(index)");

        return 0;
    }

    auto index = static_cast<int32_t>(lua_tonumber(L, 1));

    uint8_t levelReq = 0;

    TrainerService* service;

    if (static_cast<uint32_t>(index - 1) < s_numTrainerServices && (service = s_trainerServices[index - 1])) {
        levelReq = service->levelReq;
    }

    lua_pushnumber(L, levelReq);

    return 1;
}

// ref: FUN_005945b0
// How many of the three ability requirements are set. The usage text names the binding
// GetTrainerServiceAbilityReq, as the reference's does.
int32_t Script_GetTrainerServiceNumAbilityReq(lua_State* L) {
    if (lua_isnumber(L, 1)) {
        auto index = static_cast<int32_t>(lua_tonumber(L, 1));

        TrainerService* service;

        if (static_cast<uint32_t>(index - 1) < s_numTrainerServices) {
            service = s_trainerServices[index - 1];
        } else {
            service = nullptr;
        }

        uint8_t count = 0;

        if (service) {
            count = 0 < service->abilityReq[0];

            if (0 < service->abilityReq[1]) {
                count++;
            }

            if (0 < service->abilityReq[2]) {
                count++;
            }
        }

        lua_pushnumber(L, count);

        return 1;
    }

    luaL_error(L, "Usage: GetTrainerServiceAbilityReq(index)");

    return 0;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GetTrainerGreetingText",         &Script_GetTrainerGreetingText },
    { "GetTrainerServiceCost",          &Script_GetTrainerServiceCost },
    { "GetTrainerServiceLevelReq",      &Script_GetTrainerServiceLevelReq },
    { "GetTrainerServiceNumAbilityReq", &Script_GetTrainerServiceNumAbilityReq },
};

void ClassTrainerFrameRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
