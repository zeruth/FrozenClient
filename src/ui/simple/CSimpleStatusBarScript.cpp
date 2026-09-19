#include "ui/simple/CSimpleStatusBarScript.hpp"
#include "ui/simple/CSimpleStatusBar.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"

namespace {

// ref: FUN_00971240
int32_t CSimpleStatusBar_GetOrientation(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    lua_pushstring(L, OrientationToString(statusBar->GetOrientation()));

    return 1;
}

int32_t CSimpleStatusBar_SetOrientation(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleStatusBar_GetMinMaxValues(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, statusBar->GetMinValue());
    lua_pushnumber(L, statusBar->GetMaxValue());

    return 2;
}

int32_t CSimpleStatusBar_SetMinMaxValues(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        luaL_error(L, "Usage: %s:SetMinMaxValues(min, max)", statusBar->GetDisplayName());
        return 0;
    }

    auto min = lua_tonumber(L, 2);
    auto max = lua_tonumber(L, 3);

    if (min < -1.0e12 || min > 1.0e12 || max < -1.0e12 || max > 1.0e12) {
        luaL_error(L, "Min or Max out of range");
        return 0;
    }

    if (max - min > 1.0e12) {
        luaL_error(L, "Min and Max too far apart");
        return 0;
    }

    statusBar->SetMinMaxValues(static_cast<float>(min), static_cast<float>(max));

    return 0;
}

int32_t CSimpleStatusBar_GetValue(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, statusBar->GetValue());

    return 1;
}

int32_t CSimpleStatusBar_SetValue(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetValue(value)", statusBar->GetDisplayName());
        return 0;
    }

    auto value = static_cast<float>(lua_tonumber(L, 2));

    statusBar->SetValue(value);

    return 0;
}

// ref: FUN_009715d0
// Hands back the texture's own Lua object, registering it on first use, which is the same shape
// GetScrollChild uses. A bar with no texture answers nil.
int32_t CSimpleStatusBar_GetStatusBarTexture(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    auto texture = statusBar->GetStatusBarTexture();

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

int32_t CSimpleStatusBar_SetStatusBarTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00971800
// The bar texture's colour. A bar with no texture answers opaque white rather than nil -- the
// reference substitutes 1,1,1,1 before reading, so the four return values are always there.
int32_t CSimpleStatusBar_GetStatusBarColor(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    auto texture = statusBar->GetStatusBarTexture();

    if (!texture) {
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 1.0);

        return 4;
    }

    CImVector color;
    texture->GetVertexColor(color);

    // Same 0.00392156f the texture's own getter uses; see the note there.
    lua_pushnumber(L, color.r * 0.00392156f);
    lua_pushnumber(L, color.g * 0.00392156f);
    lua_pushnumber(L, color.b * 0.00392156f);
    lua_pushnumber(L, color.a * 0.00392156f);

    return 4;
}

int32_t CSimpleStatusBar_SetStatusBarColor(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    CImVector color = {};
    FrameScript_GetColor(L, 2, color);

    statusBar->SetStatusBarColor(color);

    return 0;
}

// ref: FUN_00971970
// One bit of the reference's flag byte at +0x29c. Pushes 1 or nil, never false.
int32_t CSimpleStatusBar_GetRotatesTexture(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    if (statusBar->GetRotatesTexture()) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_009719d0
int32_t CSimpleStatusBar_SetRotatesTexture(lua_State* L) {
    auto type = CSimpleStatusBar::GetObjectType();
    auto statusBar = static_cast<CSimpleStatusBar*>(FrameScript_GetObjectThis(L, type));

    statusBar->SetRotatesTexture(StringToBOOL(L, 2, 0));

    return 0;
}

}

FrameScript_Method SimpleStatusBarMethods[] = {
    { "GetOrientation",         &CSimpleStatusBar_GetOrientation },
    { "SetOrientation",         &CSimpleStatusBar_SetOrientation },
    { "GetMinMaxValues",        &CSimpleStatusBar_GetMinMaxValues },
    { "SetMinMaxValues",        &CSimpleStatusBar_SetMinMaxValues },
    { "GetValue",               &CSimpleStatusBar_GetValue },
    { "SetValue",               &CSimpleStatusBar_SetValue },
    { "GetStatusBarTexture",    &CSimpleStatusBar_GetStatusBarTexture },
    { "SetStatusBarTexture",    &CSimpleStatusBar_SetStatusBarTexture },
    { "GetStatusBarColor",      &CSimpleStatusBar_GetStatusBarColor },
    { "SetStatusBarColor",      &CSimpleStatusBar_SetStatusBarColor },
    { "GetRotatesTexture",      &CSimpleStatusBar_GetRotatesTexture },
    { "SetRotatesTexture",      &CSimpleStatusBar_SetRotatesTexture },
};
