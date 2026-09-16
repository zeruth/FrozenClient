#include "ui/simple/CSimpleColorSelectScript.hpp"

#include "ui/simple/CSimpleColorSelect.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "util/Lua.hpp"
#include <cstdint>

namespace {

CSimpleColorSelect* This(lua_State* L) {
    auto type = CSimpleColorSelect::GetObjectType();

    return static_cast<CSimpleColorSelect*>(FrameScript_GetObjectThis(L, type));
}

float Number(lua_State* L, int32_t index) {
    return lua_type(L, index) == LUA_TNUMBER ? static_cast<float>(lua_tonumber(L, index)) : 0.0f;
}

// The four textures are read and written by name from the interface; one pair of accessors each.
int32_t PushTexture(lua_State* L, CSimpleTexture* texture) {
    if (!texture) {
        lua_pushnil(L);
    } else {
        texture->RegisterScriptObject(0);
        lua_rawgeti(L, LUA_REGISTRYINDEX, texture->lua_objectRef);
    }

    return 1;
}

} // namespace

int32_t CSimpleColorSelect_SetColorRGB(lua_State* L) {
    This(L)->SetColorRGB(Number(L, 2), Number(L, 3), Number(L, 4));

    return 0;
}

int32_t CSimpleColorSelect_GetColorRGB(lua_State* L) {
    float r;
    float g;
    float b;
    This(L)->GetColorRGB(r, g, b);

    lua_pushnumber(L, r);
    lua_pushnumber(L, g);
    lua_pushnumber(L, b);

    return 3;
}

int32_t CSimpleColorSelect_SetColorHSV(lua_State* L) {
    This(L)->SetColorHSV(Number(L, 2), Number(L, 3), Number(L, 4));

    return 0;
}

int32_t CSimpleColorSelect_GetColorHSV(lua_State* L) {
    auto frame = This(L);

    lua_pushnumber(L, frame->m_hue);
    lua_pushnumber(L, frame->m_saturation);
    lua_pushnumber(L, frame->m_value);

    return 3;
}

int32_t CSimpleColorSelect_GetColorWheelTexture(lua_State* L) {
    return PushTexture(L, This(L)->m_wheelTexture);
}

int32_t CSimpleColorSelect_GetColorWheelThumbTexture(lua_State* L) {
    return PushTexture(L, This(L)->m_wheelThumbTexture);
}

int32_t CSimpleColorSelect_GetColorValueTexture(lua_State* L) {
    return PushTexture(L, This(L)->m_valueTexture);
}

int32_t CSimpleColorSelect_GetColorValueThumbTexture(lua_State* L) {
    return PushTexture(L, This(L)->m_valueThumbTexture);
}

int32_t CSimpleColorSelect_SetColorWheelTexture(lua_State* L) {
    auto frame = This(L);

    if (frame->m_wheelTexture && lua_isstring(L, 2)) {
        frame->m_wheelTexture->SetTexture(lua_tostring(L, 2), false, false, CSimpleTexture::s_textureFilterMode, ImageMode_UI);
    }

    return 0;
}

int32_t CSimpleColorSelect_SetColorValueTexture(lua_State* L) {
    auto frame = This(L);

    if (frame->m_valueTexture && lua_isstring(L, 2)) {
        frame->m_valueTexture->SetTexture(lua_tostring(L, 2), false, false, CSimpleTexture::s_textureFilterMode, ImageMode_UI);
    }

    return 0;
}

FrameScript_Method SimpleColorSelectMethods[NUM_SIMPLE_COLOR_SELECT_SCRIPT_METHODS] = {
    { "SetColorRGB",                 &CSimpleColorSelect_SetColorRGB },
    { "GetColorRGB",                 &CSimpleColorSelect_GetColorRGB },
    { "SetColorHSV",                 &CSimpleColorSelect_SetColorHSV },
    { "GetColorHSV",                 &CSimpleColorSelect_GetColorHSV },
    { "GetColorWheelTexture",        &CSimpleColorSelect_GetColorWheelTexture },
    { "GetColorWheelThumbTexture",   &CSimpleColorSelect_GetColorWheelThumbTexture },
    { "GetColorValueTexture",        &CSimpleColorSelect_GetColorValueTexture },
    { "GetColorValueThumbTexture",   &CSimpleColorSelect_GetColorValueThumbTexture },
    { "SetColorWheelTexture",        &CSimpleColorSelect_SetColorWheelTexture },
    { "SetColorValueTexture",        &CSimpleColorSelect_SetColorValueTexture },
};
