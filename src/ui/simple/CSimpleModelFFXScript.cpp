#include "ui/simple/CSimpleModelFFXScript.hpp"
#include "ui/simple/CSimpleModelFFX.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>

// ref: FUN_004e6be0
int32_t CSimpleModelFFX_ResetLights(lua_State* L) {
    auto type = CSimpleModelFFX::GetObjectType();
    auto model = static_cast<CSimpleModelFFX*>(FrameScript_GetObjectThis(L, type));

    // Every one of the six light sets loses its count and its flag; the light slots themselves
    // are left untouched, exactly as the reference does.
    for (int32_t i = 0; i < 2; i++) {
        model->m_lights[i].m_count = 0;
        model->m_lights[i].m_dirty = false;

        model->m_characterLights[i].m_count = 0;
        model->m_characterLights[i].m_dirty = false;

        model->m_petLights[i].m_count = 0;
        model->m_petLights[i].m_dirty = false;
    }

    return 0;
}

// ---------------------------------------------------------------------------------------------
// The three Add* bindings below are identified but not ported. All three share one body: pick
// the light set from the index argument (set 1 when lua_isnumber(L, 2) and lua_tointeger(L, 2)
// is non-zero, set 0 otherwise), build a light from the arguments at index 3 onwards with the
// shared parser FUN_00960a10, and -- when the chosen set still holds fewer than four lights --
// append it and raise the set's flag. A parse that fails falls through to the usage error.
//
// FUN_00960a10 is the blocker. It is 777 bytes, it is shared with CSimpleModel:SetLight (which
// is stubbed for the same reason), and it has not been decompiled; neither has the 108-byte
// light struct it fills, which is built by FUN_00834a40 and torn down by FUN_00834ab0. The
// argument grammar in the usage strings is not enough to recover it -- the optional groups, the
// defaults and the conditions under which the parse reports failure are all unknown -- and a
// reconstructed parser would either raise a Lua error on every call from the character and pet
// portrait frames or append a wrongly filled light. Both are worse than the stub, so the stub
// stands until FUN_00960a10 is decompiled.
// ---------------------------------------------------------------------------------------------

// ref: FUN_004e6c60
int32_t CSimpleModelFFX_AddLight(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_004e6d60
int32_t CSimpleModelFFX_AddCharacterLight(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_004e6e60
int32_t CSimpleModelFFX_AddPetLight(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

FrameScript_Method SimpleModelFFXMethods[NUM_SIMPLE_MODEL_FFX_SCRIPT_METHODS] = {
    { "ResetLights",        &CSimpleModelFFX_ResetLights },
    { "AddLight",           &CSimpleModelFFX_AddLight },
    { "AddCharacterLight",  &CSimpleModelFFX_AddCharacterLight },
    { "AddPetLight",        &CSimpleModelFFX_AddPetLight }
};
