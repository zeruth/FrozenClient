#include "ui/game/VoiceScript.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"

namespace {

// Set when the client has turned voice chat off. Nothing sets it yet: the voice code that does is
// not ported.
// ref: DAT_00d37d18
int32_t s_voiceDisabledByClient;

// ref: FUN_007dc910
int32_t Script_VoiceIsDisabledByClient(lua_State* L) {
    if (s_voiceDisabledByClient) {
        lua_pushnumber(L, 1.0);

        return 1;
    }

    lua_pushnil(L);

    return 1;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "VoiceIsDisabledByClient", &Script_VoiceIsDisabledByClient },
};

void VoiceRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
