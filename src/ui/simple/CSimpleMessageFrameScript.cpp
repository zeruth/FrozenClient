#include "ui/simple/CSimpleMessageFrameScript.hpp"

#include "ui/simple/CSimpleFontedFrameFont.hpp"
#include "ui/simple/CSimpleMessageFrame.hpp"
#include "ui/FrameScript.hpp"
#include "ui/Util.hpp"
#include "ui/simple/CSimpleFontStringAttributes.hpp"
#include "ui/simple/CSimpleFont.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>
#include <storm/String.hpp>

namespace {

CSimpleMessageFrame* This(lua_State* L) {
    auto type = CSimpleMessageFrame::GetObjectType();

    return static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, type));
}

// The colour arguments are optional; the interface calls AddMessage with as few as one argument.
float OptionalColor(lua_State* L, int32_t index) {
    return lua_type(L, index) == LUA_TNUMBER ? static_cast<float>(lua_tonumber(L, index)) : 1.0f;
}

} // namespace

int32_t CSimpleMessageFrame_AddMessage(lua_State* L) {
    auto frame = This(L);

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:AddMessage(\"text\" [, r, g, b [, id]])", frame->GetDisplayName());
    }

    int32_t id = lua_type(L, 6) == LUA_TNUMBER ? static_cast<int32_t>(lua_tonumber(L, 6)) : 0;

    frame->AddMessage(
        lua_tostring(L, 2),
        OptionalColor(L, 3),
        OptionalColor(L, 4),
        OptionalColor(L, 5),
        id
    );

    return 0;
}

int32_t CSimpleMessageFrame_Clear(lua_State* L) {
    This(L)->Clear();

    return 0;
}

int32_t CSimpleMessageFrame_SetFading(lua_State* L) {
    This(L)->m_fade = lua_toboolean(L, 2) != 0;

    return 0;
}

int32_t CSimpleMessageFrame_GetFading(lua_State* L) {
    lua_pushboolean(L, This(L)->m_fade);

    return 1;
}

int32_t CSimpleMessageFrame_SetFadeDuration(lua_State* L) {
    if (lua_type(L, 2) == LUA_TNUMBER) {
        This(L)->m_fadeDuration = static_cast<float>(lua_tonumber(L, 2));
    }

    return 0;
}

int32_t CSimpleMessageFrame_GetFadeDuration(lua_State* L) {
    lua_pushnumber(L, This(L)->m_fadeDuration);

    return 1;
}

int32_t CSimpleMessageFrame_SetTimeVisible(lua_State* L) {
    if (lua_type(L, 2) == LUA_TNUMBER) {
        This(L)->m_displayDuration = static_cast<float>(lua_tonumber(L, 2));
    }

    return 0;
}

int32_t CSimpleMessageFrame_GetTimeVisible(lua_State* L) {
    lua_pushnumber(L, This(L)->m_displayDuration);

    return 1;
}

int32_t CSimpleMessageFrame_SetInsertMode(lua_State* L) {
    const char* mode = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;

    if (mode) {
        This(L)->m_insertMode = !SStrCmpI(mode, "TOP", STORM_MAX_STR)
            ? INSERT_MODE_TOP
            : INSERT_MODE_BOTTOM;
    }

    return 0;
}

int32_t CSimpleMessageFrame_GetInsertMode(lua_State* L) {
    lua_pushstring(L, This(L)->m_insertMode == INSERT_MODE_TOP ? "TOP" : "BOTTOM");

    return 1;
}

// The frame's font, or null. The reference reaches a CSimpleFont at the frame's +0x2dc and hands it
// to the same helpers the font string bindings use; frozen's copies of those take CSimpleFontString*
// and cannot, so the three bindings below work the font's attributes directly the way
// CSimpleFont_SetFont already does. Divergence in sharing, not in behaviour: one reference helper
// against two frozen implementations of it. The note above the helper declarations in
// CSimpleFontStringScript.hpp records what unifying them would need.
static CSimpleFont* MessageFrameFont(CSimpleMessageFrame* frame) {
    return frame->m_font;
}

// ref: FUN_009729e0
int32_t CSimpleMessageFrame_SetTextColor(lua_State* L) {
    auto type = CSimpleMessageFrame::GetObjectType();
    auto frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, type));
    auto font = MessageFrameFont(frame);

    if (!font) {
        return 0;
    }

    CImVector color = { 0x00, 0x00, 0x00, 0x00 };
    FrameScript_GetColor(L, 2, color);

    font->m_attributes.SetColor(color);
    font->UpdateObjects();

    return 0;
}

// ref: FUN_00972920
int32_t CSimpleMessageFrame_SetFont(lua_State* L) {
    auto type = CSimpleMessageFrame::GetObjectType();
    auto frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, type));
    auto font = MessageFrameFont(frame);

    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetFont(\"font\", fontHeight [, flags])", frame->GetDisplayName());
    }

    if (!font) {
        lua_pushnil(L);

        return 1;
    }

    uint32_t fontFlags = 0x0;

    if (lua_isstring(L, 4)) {
        fontFlags = StringToFontFlags(lua_tostring(L, 4));
    }

    // Height stays in the units the caller passed, as everywhere else in frozen; see
    // FontString_SetFont for why that diverges from the reference deliberately.
    if (font->m_attributes.SetFont(lua_tostring(L, 2), static_cast<float>(lua_tonumber(L, 3)), fontFlags)) {
        font->UpdateObjects();

        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_00972980
int32_t CSimpleMessageFrame_GetFont(lua_State* L) {
    auto type = CSimpleMessageFrame::GetObjectType();
    auto frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, type));
    auto font = MessageFrameFont(frame);

    if (!font || !font->m_attributes.m_font.GetString()) {
        lua_pushnil(L);

        return 1;
    }

    lua_pushstring(L, font->m_attributes.m_font.GetString());
    lua_pushnumber(L, font->m_attributes.m_fontHeight);
    lua_pushstring(L, FontFlagsToString(font->m_attributes.m_fontFlags));

    return 3;
}

FrameScript_Method SimpleMessageFrameMethods[NUM_SIMPLE_MESSAGE_FRAME_SCRIPT_METHODS] = {
    { "AddMessage",                 &CSimpleMessageFrame_AddMessage },
    { "Clear",                      &CSimpleMessageFrame_Clear },
    { "SetFading",                  &CSimpleMessageFrame_SetFading },
    { "GetFading",                  &CSimpleMessageFrame_GetFading },
    { "SetFadeDuration",            &CSimpleMessageFrame_SetFadeDuration },
    { "GetFadeDuration",            &CSimpleMessageFrame_GetFadeDuration },
    { "SetTimeVisible",             &CSimpleMessageFrame_SetTimeVisible },
    { "GetTimeVisible",             &CSimpleMessageFrame_GetTimeVisible },
    { "SetInsertMode",              &CSimpleMessageFrame_SetInsertMode },
    { "GetInsertMode",              &CSimpleMessageFrame_GetInsertMode },
    { "SetTextColor",               &CSimpleMessageFrame_SetTextColor },
    { "SetFont",                    &CSimpleMessageFrame_SetFont },
    { "GetFont",                    &CSimpleMessageFrame_GetFont },
};
