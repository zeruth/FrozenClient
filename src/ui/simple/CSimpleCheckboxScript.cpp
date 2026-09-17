#include "ui/simple/CSimpleCheckboxScript.hpp"
#include "ui/Util.hpp"
#include "ui/simple/CSimpleCheckbox.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>

int32_t CSimpleCheckbox_SetChecked(lua_State* L) {
    auto type = CSimpleCheckbox::GetObjectType();
    auto checkbox = static_cast<CSimpleCheckbox*>(FrameScript_GetObjectThis(L, type));

    auto checked = StringToBOOL(L, 2, 1);
    checkbox->SetChecked(checked, 0);

    return 0;
}

int32_t CSimpleCheckbox_GetChecked(lua_State* L) {
    auto type = CSimpleCheckbox::GetObjectType();
    auto checkbox = static_cast<CSimpleCheckbox*>(FrameScript_GetObjectThis(L, type));

    if (checkbox->GetChecked()) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// Hand a checkbox's texture region back to Lua. The object has to be registered with the script
// system before it has a reference to push -- the same dance CSimpleButton_GetStateTexture does.
int32_t CSimpleCheckbox_PushTexture(lua_State* L, CSimpleTexture* texture) {
    if (!texture) {
        lua_pushnil(L);

        return 1;
    }

    if (!texture->lua_registered) {
        texture->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, texture->lua_objectRef);

    return 1;
}

int32_t CSimpleCheckbox_GetCheckedTexture(lua_State* L) {
    auto type = CSimpleCheckbox::GetObjectType();
    auto checkbox = static_cast<CSimpleCheckbox*>(FrameScript_GetObjectThis(L, type));

    return CSimpleCheckbox_PushTexture(L, checkbox->m_checkedTexture);
}

int32_t CSimpleCheckbox_SetCheckedTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleCheckbox_GetDisabledCheckedTexture(lua_State* L) {
    auto type = CSimpleCheckbox::GetObjectType();
    auto checkbox = static_cast<CSimpleCheckbox*>(FrameScript_GetObjectThis(L, type));

    return CSimpleCheckbox_PushTexture(L, checkbox->m_disabledTexture);
}

int32_t CSimpleCheckbox_SetDisabledCheckedTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

FrameScript_Method SimpleCheckboxMethods[NUM_SIMPLE_CHECKBOX_SCRIPT_METHODS] = {
    { "SetChecked",                  &CSimpleCheckbox_SetChecked },
    { "GetChecked",                  &CSimpleCheckbox_GetChecked },
    { "GetCheckedTexture",           &CSimpleCheckbox_GetCheckedTexture },
    { "SetCheckedTexture",           &CSimpleCheckbox_SetCheckedTexture },
    { "GetDisabledCheckedTexture",   &CSimpleCheckbox_GetDisabledCheckedTexture },
    { "SetDisabledCheckedTexture",   &CSimpleCheckbox_SetDisabledCheckedTexture }
};
