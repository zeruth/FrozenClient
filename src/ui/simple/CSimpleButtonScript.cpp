#include <storm/String.hpp>
#include "ui/simple/CSimpleButtonScript.hpp"
#include "gx/Coordinate.hpp"
#include "ui/Util.hpp"
#include "ui/simple/CSimpleButton.hpp"
#include "ui/simple/CSimpleFont.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>

int32_t CSimpleButton_SetStateTexture(lua_State* L, CSimpleButtonState state, const char* method) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        auto texture = static_cast<CSimpleTexture*>(lua_touserdata(L, -1));
        lua_settop(L, -2);

        if (!texture) {
            luaL_error(L, "%s:%s(): Couldn't find 'this' in texture", button->GetDisplayName(), method);
        }

        if (!texture->IsA(CSimpleTexture::GetObjectType())) {
            luaL_error(L, "%s:%s(): Wrong object type, expected texture", button->GetDisplayName(), method);
        }

        button->SetStateTexture(state, texture);

    } else if (lua_isstring(L, 2)) {
        auto texFile = lua_tostring(L, 2);
        button->SetStateTexture(state, texFile);

    } else if (lua_type(L, 2) == LUA_TNIL) {
        CSimpleTexture* texture = nullptr;
        button->SetStateTexture(state, texture);

    } else {
        luaL_error(L, "Usage: %s:%s(texture or \"texture\" or nil)", button->GetDisplayName(), method);
    }

    return 0;
}

int32_t CSimpleButton_GetStateTexture(lua_State* L, CSimpleButtonState state) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    auto texture = button->m_textures[state];

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

int32_t CSimpleButton_Enable(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    if (button->ProtectedFunctionsAllowed()) {
        button->Enable(1);
    } else {
        // TODO
        // - disallowed logic
    }

    return 0;
}

int32_t CSimpleButton_Disable(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    if (button->ProtectedFunctionsAllowed()) {
        button->Enable(0);
    } else {
        // TODO
        // - disallowed logic
    }

    return 0;
}

int32_t CSimpleButton_IsEnabled(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, button->GetButtonState() != BUTTONSTATE_DISABLED);

    return 1;
}

int32_t CSimpleButton_GetButtonState(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    auto buttonState = button->GetButtonState();

    if (buttonState == BUTTONSTATE_DISABLED) {
        lua_pushstring(L, "DISABLED");
        return 1;
    } else if (buttonState == BUTTONSTATE_PUSHED) {
        lua_pushstring(L, "PUSHED");
        return 1;
    } else if (buttonState == BUTTONSTATE_NORMAL) {
        lua_pushstring(L, "NORMAL");
        return 1;
    }

    lua_pushstring(L, "UNKNOWN");
    return 1;
}

int32_t CSimpleButton_SetButtonState(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetButtonState(\"state\" [, lock])", button->GetDisplayName());
    }

    auto stateName = lua_tostring(L, 2);
    CSimpleButtonState state;

    if (!SStrCmpI(stateName, "NORMAL", STORM_MAX_STR)) {
        state = BUTTONSTATE_NORMAL;
    } else if (!SStrCmpI(stateName, "PUSHED", STORM_MAX_STR)) {
        state = BUTTONSTATE_PUSHED;
    } else if (!SStrCmpI(stateName, "DISABLED", STORM_MAX_STR)) {
        state = BUTTONSTATE_DISABLED;
    } else {
        return luaL_error(L, "%s:SetButtonState(): Unknown button state %s", button->GetDisplayName(), stateName);
    }

    int32_t lock = lua_isnumber(L, 3) ? lua_tonumber(L, 3) != 0.0 : lua_toboolean(L, 3);

    button->SetButtonState(state, lock);

    return 0;
}

int32_t CSimpleButton_SetNormalFontObject(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    CSimpleFont* font = nullptr;

    if (lua_type(L, 2) == LUA_TSTRING) {
        auto fontName = lua_tostring(L, 2);
        font = CSimpleFont::GetFont(fontName, 0);
    } else if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        font = static_cast<CSimpleFont*>(lua_touserdata(L, -1));
        lua_settop(L, -2);
    }

    if (!button || !font || !font->IsA(CSimpleFont::GetObjectType())) {
        return luaL_error(L, "Usage: %s:SetNormalFontObject(\"fontname\" or fontObject)", button->GetDisplayName());
    }

    button->m_normalFont = font;
    button->UpdateTextState(button->m_state);

    return 0;
}

// ref: FUN_004a38f0
// Pushes a button's font object, registering it on first use. The reference shares one helper
// across the three getters below, which is why they are one-line forwards here too.
//
// A button with no font for that state answers with NO values, not nil -- the reference returns
// zero from the binding rather than pushing anything, and FrameXML's button templates rely on the
// difference when they fall back to the parent's font.
static int32_t ButtonFontObject(CSimpleFont* font, lua_State* L) {
    if (!font) {
        return 0;
    }

    if (!font->lua_registered) {
        font->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, font->lua_objectRef);

    return 1;
}

// ref: FUN_00977450
int32_t CSimpleButton_GetNormalFontObject(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    return ButtonFontObject(button->m_normalFont, L);
}

int32_t CSimpleButton_SetDisabledFontObject(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    CSimpleFont* font = nullptr;

    if (lua_type(L, 2) == LUA_TSTRING) {
        auto fontName = lua_tostring(L, 2);
        font = CSimpleFont::GetFont(fontName, 0);
    } else if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        font = static_cast<CSimpleFont*>(lua_touserdata(L, -1));
        lua_settop(L, -2);
    }

    if (!button || !font || !font->IsA(CSimpleFont::GetObjectType())) {
        return luaL_error(L, "Usage: %s:SetDisabledFontObject(\"fontname\")", button->GetDisplayName());
    }

    button->m_disabledFont = font;
    button->UpdateTextState(button->m_state);

    return 0;
}

// ref: FUN_009775d0
int32_t CSimpleButton_GetDisabledFontObject(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    return ButtonFontObject(button->m_disabledFont, L);
}

int32_t CSimpleButton_SetHighlightFontObject(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    CSimpleFont* font = nullptr;

    if (lua_type(L, 2) == LUA_TSTRING) {
        auto fontName = lua_tostring(L, 2);
        font = CSimpleFont::GetFont(fontName, 0);
    } else if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        font = static_cast<CSimpleFont*>(lua_touserdata(L, -1));
        lua_settop(L, -2);
    }

    if (!button || !font || !font->IsA(CSimpleFont::GetObjectType())) {
        return luaL_error(L, "Usage: %s:SetHighlightFontObject(\"fontname\")", button->GetDisplayName());
    }

    button->m_highlightFont = font;
    button->UpdateTextState(button->m_state);

    return 0;
}

// ref: FUN_00977750
int32_t CSimpleButton_GetHighlightFontObject(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    return ButtonFontObject(button->m_highlightFont, L);
}

int32_t CSimpleButton_SetFontString(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleButton_GetFontString(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));
    auto text = button->m_text;

    if (!text) {
        lua_pushnil(L);
        return 1;
    }

    if (!text->lua_registered) {
        text->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, text->lua_objectRef);

    return 1;
}

int32_t CSimpleButton_SetText(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    const char* text = lua_tostring(L, 2);
    button->SetText(text);

    return 0;
}

// ref: FUN_009779b0
// Formats the arguments the way string.format would and sets the label. Note what it does NOT do:
// the font string's SetFormattedText refuses with "Font not set" first, and the button's has no
// such check in the reference, so a button without a font takes the text and shows nothing rather
// than raising.
int32_t CSimpleButton_SetFormattedText(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    char buffer[4096];

    button->SetText(FrameScript_Sprintf(L, 2, buffer, sizeof(buffer)));

    return 0;
}

int32_t CSimpleButton_GetText(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    const char* text = nullptr;

    if (button->m_text && button->m_text->GetText() && *button->m_text->GetText()) {
        text = button->m_text->GetText();
    }

    lua_pushstring(L, text);

    return 1;
}

int32_t CSimpleButton_SetNormalTexture(lua_State* L) {
    return CSimpleButton_SetStateTexture(L, BUTTONSTATE_NORMAL, "SetNormalTexture");
}

int32_t CSimpleButton_GetNormalTexture(lua_State* L) {
    return CSimpleButton_GetStateTexture(L, BUTTONSTATE_NORMAL);
}

int32_t CSimpleButton_SetPushedTexture(lua_State* L) {
    return CSimpleButton_SetStateTexture(L, BUTTONSTATE_PUSHED, "SetPushedTexture");
}

int32_t CSimpleButton_GetPushedTexture(lua_State* L) {
    return CSimpleButton_GetStateTexture(L, BUTTONSTATE_PUSHED);
}

int32_t CSimpleButton_SetDisabledTexture(lua_State* L) {
    return CSimpleButton_SetStateTexture(L, BUTTONSTATE_DISABLED, "SetDisabledTexture");
}

int32_t CSimpleButton_GetDisabledTexture(lua_State* L) {
    return CSimpleButton_GetStateTexture(L, BUTTONSTATE_DISABLED);
}

int32_t CSimpleButton_SetHighlightTexture(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    EGxBlend blendMode = GxBlend_Add;
    if (lua_isstring(L, 3)) {
        auto blendString = lua_tostring(L, 3);
        StringToBlendMode(blendString, blendMode);
    }

    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        auto texture = static_cast<CSimpleTexture*>(lua_touserdata(L, -1));
        lua_settop(L, -2);

        if (!texture) {
            luaL_error(L, "%s:SetHighlightTexture(): Couldn't find 'this' in texture", button->GetDisplayName());
        }

        if (!texture->IsA(CSimpleTexture::GetObjectType())) {
            luaL_error(L, "%s:SetHighlightTexture(): Wrong object type, expected texture", button->GetDisplayName());
        }

        button->SetHighlight(texture, blendMode);

    } else if (lua_isstring(L, 2)) {
        auto texFile = lua_tostring(L, 2);
        button->SetHighlight(texFile, blendMode);

    } else if (lua_type(L, 2) == LUA_TNIL) {
        CSimpleTexture* texture = nullptr;
        button->SetHighlight(texture, GxBlend_Add);

    } else {
        luaL_error(L, "Usage: %s:SetHighlightTexture(texture or \"texture\" or nil [, \"blendmode\")", button->GetDisplayName());
    }

    return 0;
}

int32_t CSimpleButton_GetHighlightTexture(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    auto texture = button->m_highlightTexture;

    if (texture) {
        if (!texture->lua_registered) {
            texture->RegisterScriptObject(nullptr);
        }

        lua_rawgeti(L, LUA_REGISTRYINDEX, texture->lua_objectRef);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_00977f60
// Where the label moves while the button is held down.
//
// Stored in the reference's units, not the caller's: it divides by the aspect compensation times
// 1024 and runs the result through NDCToDDCWidth, and GetPushedTextOffset undoes exactly that. The
// pair is self-consistent either way, so this follows the reference rather than frozen's font-height
// divergence -- a font height is a size the caller chose, while this is a position that has to land
// in the same space as the rest of the layout.
int32_t CSimpleButton_SetPushedTextOffset(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetPushedTextOffset(x, y)", button->GetDisplayName());
    }

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    button->m_pressedOffset.x = NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 2)) / scale);
    button->m_pressedOffset.y = NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 3)) / scale);

    return 0;
}

// ref: FUN_00978040
// Built as the exact inverse of the setter. The reference fetches the aspect compensation and calls
// DDCToNDCWidth, but the decompiler lost the arguments, so whether it groups them this way or as
// DDCToNDCWidth(offset * scale) is not recoverable from the disassembly -- only that the pair round
// trips, which this does.
int32_t CSimpleButton_GetPushedTextOffset(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(button->m_pressedOffset.x) * scale);
    lua_pushnumber(L, DDCToNDCWidth(button->m_pressedOffset.y) * scale);

    return 2;
}

int32_t CSimpleButton_GetTextWidth(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    auto text = button->m_text;

    float width = text ? text->GetWidth() : 0.0f;
    float ddcWidth = CoordinateGetAspectCompensation() * 1024.0f * width;
    float ndcWidth = DDCToNDCWidth(ddcWidth);

    lua_pushnumber(L, ndcWidth);

    return 1;
}

int32_t CSimpleButton_GetTextHeight(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    auto text = button->m_text;

    float height = text ? text->GetHeight() : 0.0f;
    float ddcHeight = CoordinateGetAspectCompensation() * 1024.0f * height;
    float ndcHeight = DDCToNDCWidth(ddcHeight);

    lua_pushnumber(L, ndcHeight);

    return 1;
}

int32_t CSimpleButton_RegisterForClicks(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    uint64_t action = 0;

    for (int32_t i = 2; lua_isstring(L, i); i++) {
        auto actionStr = lua_tostring(L, i);
        action |= StringToClickAction(actionStr);
    }

    button->SetClickAction(action);

    return 0;
}

int32_t CSimpleButton_Click(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    auto inputButton = "LeftButton";
    if (lua_isstring(L, 2)) {
        inputButton = lua_tostring(L, 2);
    }

    auto v6 = StringToBOOL(L, 3, 0);

    button->OnClick(inputButton, v6);

    return 0;
}

int32_t CSimpleButton_LockHighlight(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    button->LockHighlight(1);

    return 0;
}

int32_t CSimpleButton_UnlockHighlight(lua_State* L) {
    auto type = CSimpleButton::GetObjectType();
    auto button = static_cast<CSimpleButton*>(FrameScript_GetObjectThis(L, type));

    button->LockHighlight(0);

    return 0;
}

// TODO FUN_00978360 and FUN_009783b0 reach the flag through virtuals at +0xf8 and +0xf4,
// so it lives on the button rather than in a script-side member. CSimpleButton has the
// call sites for it commented out (CSimpleButton.cpp, around the disabled-state handling)
// but no member; that is what these two wait on, not on themselves.
int32_t CSimpleButton_GetMotionScriptsWhileDisabled(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleButton_SetMotionScriptsWhileDisabled(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

FrameScript_Method SimpleButtonMethods[NUM_SIMPLE_BUTTON_SCRIPT_METHODS] = {
    { "Enable",                     &CSimpleButton_Enable },
    { "Disable",                    &CSimpleButton_Disable },
    { "IsEnabled",                  &CSimpleButton_IsEnabled },
    { "GetButtonState",             &CSimpleButton_GetButtonState },
    { "SetButtonState",             &CSimpleButton_SetButtonState },
    { "SetNormalFontObject",        &CSimpleButton_SetNormalFontObject },
    { "GetNormalFontObject",        &CSimpleButton_GetNormalFontObject },
    { "SetDisabledFontObject",      &CSimpleButton_SetDisabledFontObject },
    { "GetDisabledFontObject",      &CSimpleButton_GetDisabledFontObject },
    { "SetHighlightFontObject",     &CSimpleButton_SetHighlightFontObject },
    { "GetHighlightFontObject",     &CSimpleButton_GetHighlightFontObject },
    { "SetFontString",              &CSimpleButton_SetFontString },
    { "GetFontString",              &CSimpleButton_GetFontString },
    { "SetText",                    &CSimpleButton_SetText },
    { "SetFormattedText",           &CSimpleButton_SetFormattedText },
    { "GetText",                    &CSimpleButton_GetText },
    { "SetNormalTexture",           &CSimpleButton_SetNormalTexture },
    { "GetNormalTexture",           &CSimpleButton_GetNormalTexture },
    { "SetPushedTexture",           &CSimpleButton_SetPushedTexture },
    { "GetPushedTexture",           &CSimpleButton_GetPushedTexture },
    { "SetDisabledTexture",         &CSimpleButton_SetDisabledTexture },
    { "GetDisabledTexture",         &CSimpleButton_GetDisabledTexture },
    { "SetHighlightTexture",        &CSimpleButton_SetHighlightTexture },
    { "GetHighlightTexture",        &CSimpleButton_GetHighlightTexture },
    { "SetPushedTextOffset",        &CSimpleButton_SetPushedTextOffset },
    { "GetPushedTextOffset",        &CSimpleButton_GetPushedTextOffset },
    { "GetTextWidth",               &CSimpleButton_GetTextWidth },
    { "GetTextHeight",              &CSimpleButton_GetTextHeight },
    { "RegisterForClicks",          &CSimpleButton_RegisterForClicks },
    { "Click",                      &CSimpleButton_Click },
    { "LockHighlight",              &CSimpleButton_LockHighlight },
    { "UnlockHighlight",            &CSimpleButton_UnlockHighlight },
    { "GetMotionScriptsWhileDisabled", &CSimpleButton_GetMotionScriptsWhileDisabled },
    { "SetMotionScriptsWhileDisabled", &CSimpleButton_SetMotionScriptsWhileDisabled }
};
