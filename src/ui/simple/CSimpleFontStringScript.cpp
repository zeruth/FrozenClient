#include "ui/CScriptObject.hpp"
#include "ui/simple/CSimpleFontStringScript.hpp"
#include "ui/Util.hpp"
#include "ui/FrameScript.hpp"
#include "gx/Coordinate.hpp"
#include "ui/simple/CSimpleFont.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "ui/simple/CSimpleFontable.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>

// ---------------------------------------------------------------------------------------------
// Shared font-string script methods. CSimpleEditBox forwards its own bindings to these, applying
// them to the font string it draws its text with, exactly as the reference does.
// ---------------------------------------------------------------------------------------------

// ref: FUN_004a3790
int32_t FontString_SetFontObject(const char* displayName, CSimpleFontString* string, lua_State* L) {
    CSimpleFont* font = nullptr;

    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        font = static_cast<CSimpleFont*>(lua_touserdata(L, -1));
        lua_settop(L, -2);

        if (!font) {
            return luaL_error(L, "%s:SetFontObject(): Couldn't find 'this' in font object", displayName);
        }

        if (!font->IsA(CSimpleFont::GetObjectType())) {
            return luaL_error(L, "%s:SetFontObject(): Wrong object type, expected font", displayName);
        }
    } else if (lua_type(L, 2) == LUA_TSTRING) {
        auto fontName = lua_tostring(L, 2);
        font = CSimpleFont::GetFont(fontName, 0);

        if (!font) {
            return luaL_error(L, "%s:SetFontObject(): Couldn't find font named %s", displayName, fontName);
        }
    } else if (lua_type(L, 2) != LUA_TNIL) {
        return luaL_error(L, "Usage: %s:SetFontObject(font or \"font\" or nil)", displayName);
    }

    // A font object inherits from another font object, so adopting one that already inherits from
    // this string would make the chain cyclic and hang every later walk of it.
    for (CSimpleFontable* fontable = font; fontable; fontable = fontable->m_fontObject) {
        if (fontable == static_cast<CSimpleFontable*>(string)) {
            return luaL_error(L, "%s:SetFontObject(): Can't create a font object loop", displayName);
        }
    }

    string->SetFontObject(font);

    return 0;
}

// ref: FUN_004a38f0
int32_t FontString_GetFontObject(const char* displayName, CSimpleFontString* string, lua_State* L) {
    auto font = string->GetFontObject();

    if (!font) {
        lua_pushnil(L);

        return 1;
    }

    if (!font->lua_registered) {
        font->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, font->lua_objectRef);

    return 1;
}

// ref: FUN_004a3a50
int32_t FontString_SetFont(const char* displayName, CSimpleFontString* string, lua_State* L) {
    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetFont(\"font\", fontHeight [, flags])", displayName);
    }

    uint32_t fontFlags = 0x0;

    if (lua_isstring(L, 4)) {
        fontFlags = StringToFontFlags(lua_tostring(L, 4));
    }

    if (string && string->SetFont(lua_tostring(L, 2), static_cast<float>(lua_tonumber(L, 3)), fontFlags, false)) {
        string->m_fontableFlags &= ~FLAG_FONT_UPDATE;

        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_004a3b60
int32_t FontString_GetFont(const char* displayName, CSimpleFontString* string, lua_State* L) {
    auto fontName = string->GetFontName();

    if (!fontName) {
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushnil(L);

        return 3;
    }

    lua_pushstring(L, fontName);
    lua_pushnumber(L, string->GetFontHeight(false));
    lua_pushstring(L, FontFlagsToString(string->GetFontFlags()));

    return 3;
}

// ref: FUN_004a3c50
int32_t FontString_SetTextColor(const char* displayName, CSimpleFontString* string, lua_State* L) {
    CImVector color = { 0x00, 0x00, 0x00, 0x00 };
    FrameScript_GetColor(L, 2, color);

    string->SetVertexColor(color);
    string->m_fontableFlags &= ~FLAG_COLOR_UPDATE;

    return 0;
}

// ref: FUN_004a3ca0
int32_t FontString_GetTextColor(const char* displayName, CSimpleFontString* string, lua_State* L) {
    CImVector color;
    string->GetTextColor(color);

    lua_pushnumber(L, color.r * (1.0f / 255.0f));
    lua_pushnumber(L, color.g * (1.0f / 255.0f));
    lua_pushnumber(L, color.b * (1.0f / 255.0f));
    lua_pushnumber(L, color.a * (1.0f / 255.0f));

    return 4;
}

// ref: FUN_004a3d40
int32_t FontString_SetShadowColor(const char* displayName, CSimpleFontString* string, lua_State* L) {
    CImVector color = { 0x00, 0x00, 0x00, 0x00 };
    FrameScript_GetColor(L, 2, color);

    string->SetShadowColor(color);
    string->m_fontableFlags &= ~FLAG_SHADOW_UPDATE;

    return 0;
}

// ref: FUN_004a3dc0
int32_t FontString_GetShadowColor(const char* displayName, CSimpleFontString* string, lua_State* L) {
    auto& color = string->m_shadowColor;

    lua_pushnumber(L, color.r * (1.0f / 255.0f));
    lua_pushnumber(L, color.g * (1.0f / 255.0f));
    lua_pushnumber(L, color.b * (1.0f / 255.0f));
    lua_pushnumber(L, color.a * (1.0f / 255.0f));

    return 4;
}

// ref: FUN_004a3e60
int32_t FontString_SetShadowOffset(const char* displayName, CSimpleFontString* string, lua_State* L) {
    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetShadowOffset(x, y)", displayName);
    }

    auto x = static_cast<float>(lua_tonumber(L, 2));
    auto y = static_cast<float>(lua_tonumber(L, 3));
    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    C2Vector offset = {
        NDCToDDCWidth(x / scale),
        NDCToDDCWidth(y / scale)
    };

    string->SetShadowOffset(offset);
    string->m_fontableFlags &= ~FLAG_SHADOW_UPDATE;

    return 0;
}

// ref: FUN_004a3f20
int32_t FontString_GetShadowOffset(const char* displayName, CSimpleFontString* string, lua_State* L) {
    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(string->m_shadowOffset.x) * scale);
    lua_pushnumber(L, DDCToNDCWidth(string->m_shadowOffset.y) * scale);

    return 2;
}

// ref: FUN_004a3f90
int32_t FontString_SetSpacing(const char* displayName, CSimpleFontString* string, lua_State* L) {
    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetSpacing(spacing)", displayName);
    }

    auto spacing = static_cast<float>(lua_tonumber(L, 2));
    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    string->SetSpacing(NDCToDDCWidth(spacing / scale));
    string->m_fontableFlags &= ~FLAG_SPACING_UPDATE;

    return 0;
}

// ref: FUN_004a4040
int32_t FontString_GetSpacing(const char* displayName, CSimpleFontString* string, lua_State* L) {
    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(string->m_spacing) * scale);

    return 1;
}

// ref: FUN_004a4930
int32_t FontString_SetJustifyH(const char* displayName, CSimpleFontString* string, lua_State* L) {
    uint32_t justifyH;

    if (!lua_isstring(L, 2) || !StringToJustify(lua_tostring(L, 2), justifyH)) {
        return luaL_error(L, "Usage: %s:SetJustifyH(\"justify\")", displayName);
    }

    string->SetJustifyH(justifyH);
    string->m_fontableFlags &= ~FLAG_STYLE_UPDATE;

    return 0;
}

// ref: FUN_004a4080
int32_t FontString_GetJustifyH(const char* displayName, CSimpleFontString* string, lua_State* L) {
    lua_pushstring(L, JustifyToString(string->m_styleFlags & 0x7));

    return 1;
}

// ref: FUN_004a49c0
int32_t FontString_SetJustifyV(const char* displayName, CSimpleFontString* string, lua_State* L) {
    uint32_t justifyV;

    if (!lua_isstring(L, 2) || !StringToJustify(lua_tostring(L, 2), justifyV)) {
        return luaL_error(L, "Usage: %s:SetJustifyV(\"justify\")", displayName);
    }

    string->SetJustifyV(justifyV);
    string->m_fontableFlags &= ~FLAG_STYLE_UPDATE;

    return 0;
}

// ref: FUN_004a40b0
int32_t FontString_GetJustifyV(const char* displayName, CSimpleFontString* string, lua_State* L) {
    lua_pushstring(L, JustifyToString(string->m_styleFlags & 0x38));

    return 1;
}

// ref: FUN_004a4a50
int32_t FontString_SetIndentedWordWrap(const char* displayName, CSimpleFontString* string, lua_State* L) {
    string->SetIndentedWordWrap(StringToBOOL(L, 2, 1));
    string->m_fontableFlags &= ~FLAG_STYLE_UPDATE;

    return 0;
}

// ref: FUN_004a40e0
int32_t FontString_GetIndentedWordWrap(const char* displayName, CSimpleFontString* string, lua_State* L) {
    if (string->m_styleFlags & 0x20000) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CSimpleFontString_IsObjectType(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:IsObjectType(\"type\")", string->GetDisplayName());
    }

    if (static_cast<CScriptObject*>(string)->IsA(lua_tostring(L, 2))) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CSimpleFontString_GetObjectType(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    lua_pushstring(L, string->GetObjectTypeName());

    return 1;
}

// ref: FUN_0048ce20
// One reference function serves both this and the texture's, the way the FontString helpers are
// shared -- same body, a different region type in front of it.
int32_t CSimpleFontString_GetDrawLayer(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto fontString = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    lua_pushstring(L, DrawLayerToString(fontString->m_drawlayer));

    return 1;
}

int32_t CSimpleFontString_SetDrawLayer(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetDrawLayer(\"layer\")", string->GetDisplayName());
    }

    int32_t drawlayer = string->m_drawlayer;

    if (!StringToDrawLayer(lua_tostring(L, 2), drawlayer)) {
        return luaL_error(L, "%s:SetDrawLayer(): Unknown layer %s", string->GetDisplayName(), lua_tostring(L, 2));
    }

    string->SetFrame(string->m_parent, drawlayer, string->m_shown);

    return 0;
}

int32_t CSimpleFontString_SetVertexColor(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    CImVector currentColor = {};
    string->GetVertexColor(currentColor);

    CImVector newColor = {};
    FrameScript_GetColor(L, 2, newColor);

    if (!lua_isnumber(L, 5)) {
        newColor.a = currentColor.a;
    }

    string->SetVertexColor(newColor);
    string->m_fontableFlags &= ~FLAG_COLOR_UPDATE;

    return 0;
}

// TODO the font string's own GetAlpha is FUN_0048cfc0, which the binding table already matches
// and which has not been decompiled. It is NOT FUN_0049f980: that one reads the frame object type
// and is CSimpleFrame::GetAlpha, tagged there. Reading m_alpha[0] here looks right -- it is the
// entry the region's colour uses -- but which entry the reference reads has not been checked, and
// the other three are the gradient.
// ref: FUN_0048cfc0
// The alpha of the text colour, not a separate field -- 00487ab0, the helper the open note here
// used to call unidentified, is CSimpleFontString::GetTextColor. Scaled by the 1/255 at 00a45564,
// which unlike the colour getters' constant really is the exact reciprocal.
int32_t CSimpleFontString_GetAlpha(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto fontString = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    CImVector color;
    fontString->GetTextColor(color);

    lua_pushnumber(L, color.a / 255.0f);

    return 1;
}

int32_t CSimpleFontString_SetAlpha(lua_State* L) {
    // Region alpha is not tracked separately yet
    return 0;
}

// ref: FUN_0048d0f0
// Start and length of the fade, in characters. The fields were already here and already read by the
// render pass (CSimpleFontString.cpp, the m_alphaGradientStart > 0 branch); nothing wrote them.
//
// The reference's setter at 00482230 always stores both, then returns true when there is no laid-out
// string and otherwise asks the layout whether the range fits, failing if it does not. Frozen stores
// and reports success: the validating call has no counterpart, so the nil return is unreachable
// here. A caller that checks the result will believe every range it asks for.
int32_t CSimpleFontString_SetAlphaGradient(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto fontString = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetAlphaGradient(start, length)", fontString->GetDisplayName());
    }

    fontString->m_alphaGradientStart = static_cast<int16_t>(lua_tonumber(L, 2));
    fontString->m_alphaGradientLength = static_cast<int16_t>(lua_tonumber(L, 3));

    lua_pushnumber(L, 1.0);

    return 1;
}

int32_t CSimpleFontString_Show(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    string->Show();

    return 0;
}

int32_t CSimpleFontString_Hide(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    string->Hide();

    return 0;
}

int32_t CSimpleFontString_IsVisible(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (string->IsVisible()) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CSimpleFontString_IsShown(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (string->IsShown()) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CSimpleFontString_GetFontObject(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetFontObject(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_SetFontObject(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetFontObject(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_GetFont(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetFont(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_SetFont(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetFont(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_GetText(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    auto text = string->GetText();
    if (!text || !*text) {
        text = nullptr;
    }

    lua_pushstring(L, text);

    return 1;
}

// ref: FUN_0048bc70
int32_t CSimpleFontString_GetFieldSize(lua_State* L) {
    lua_pushnumber(L, 8191.0);

    return 1;
}

int32_t CSimpleFontString_SetText(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (!string->m_font) {
        luaL_error(L, "%s:SetText(): Font not set", string->GetDisplayName());
    }

    const char* text = lua_tostring(L, 2);
    string->SetText(text, 1);

    return 0;
}

int32_t CSimpleFontString_SetFormattedText(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (!string->m_font) {
        return luaL_error(L, "%s:SetFormattedText(): Font not set", string->GetDisplayName());
    }

    char buffer[4096];
    auto text = FrameScript_Sprintf(L, 2, buffer, sizeof(buffer));

    string->SetText(text, 1);

    return 0;
}

int32_t CSimpleFontString_GetTextColor(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetTextColor(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_SetTextColor(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetTextColor(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_GetShadowColor(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetShadowColor(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_SetShadowColor(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetShadowColor(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_GetShadowOffset(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetShadowOffset(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_SetShadowOffset(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetShadowOffset(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_GetSpacing(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetSpacing(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_SetSpacing(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetSpacing(string->GetDisplayName(), string, L);
}

// ref: FUN_0048ddb0
int32_t CSimpleFontString_SetTextHeight(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetTextHeight(pixelHeight)", string->GetDisplayName());
    }

    auto height = static_cast<float>(lua_tonumber(L, 2));

    if (height <= 1.1920929e-07f) {
        return luaL_error(L, "%s:SetTextHeight(): invalid texHeight: %f, height must be > 0", string->GetDisplayName(), height);
    }

    string->SetTextHeight(NDCToDDCWidth(height / CoordinateGetAspectCompensation()));

    return 0;
}

int32_t CSimpleFontString_GetStringWidth(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, string->GetStringWidth());

    return 1;
}

int32_t CSimpleFontString_GetStringHeight(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, string->GetStringHeight());

    return 1;
}

int32_t CSimpleFontString_GetJustifyH(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetJustifyH(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_SetJustifyH(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetJustifyH(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_GetJustifyV(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetJustifyV(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_SetJustifyV(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetJustifyV(string->GetDisplayName(), string, L);
}

// ref: FUN_0048e010
int32_t CSimpleFontString_CanNonSpaceWrap(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (string->m_styleFlags & 0x1000) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0048e500
int32_t CSimpleFontString_SetNonSpaceWrap(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    auto wrap = StringToBOOL(L, 2, 1);

    string->m_settableStyleFlags &= ~0x1000;
    string->SetNonSpaceWrap(wrap);

    return 0;
}

// ref: FUN_0048e080
int32_t CSimpleFontString_CanWordWrap(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    if (!(string->m_styleFlags & 0x40)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_0048e580
int32_t CSimpleFontString_SetWordWrap(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    auto wrap = StringToBOOL(L, 2, 1);

    string->m_settableStyleFlags &= ~0x40;
    string->SetNonWordWrap(!wrap);

    return 0;
}

int32_t CSimpleFontString_GetIndentedWordWrap(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetIndentedWordWrap(string->GetDisplayName(), string, L);
}

int32_t CSimpleFontString_SetIndentedWordWrap(lua_State* L) {
    auto type = CSimpleFontString::GetObjectType();
    auto string = static_cast<CSimpleFontString*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetIndentedWordWrap(string->GetDisplayName(), string, L);
}

FrameScript_Method SimpleFontStringMethods[NUM_SIMPLE_FONT_STRING_SCRIPT_METHODS] = {
    { "IsObjectType",           &CSimpleFontString_IsObjectType },
    { "GetObjectType",          &CSimpleFontString_GetObjectType },
    { "GetDrawLayer",           &CSimpleFontString_GetDrawLayer },
    { "SetDrawLayer",           &CSimpleFontString_SetDrawLayer },
    { "SetVertexColor",         &CSimpleFontString_SetVertexColor },
    { "GetAlpha",               &CSimpleFontString_GetAlpha },
    { "SetAlpha",               &CSimpleFontString_SetAlpha },
    { "SetAlphaGradient",       &CSimpleFontString_SetAlphaGradient },
    { "Show",                   &CSimpleFontString_Show },
    { "Hide",                   &CSimpleFontString_Hide },
    { "IsVisible",              &CSimpleFontString_IsVisible },
    { "IsShown",                &CSimpleFontString_IsShown },
    { "GetFontObject",          &CSimpleFontString_GetFontObject },
    { "SetFontObject",          &CSimpleFontString_SetFontObject },
    { "GetFont",                &CSimpleFontString_GetFont },
    { "SetFont",                &CSimpleFontString_SetFont },
    { "GetText",                &CSimpleFontString_GetText },
    { "GetFieldSize",           &CSimpleFontString_GetFieldSize },
    { "SetText",                &CSimpleFontString_SetText },
    { "SetFormattedText",       &CSimpleFontString_SetFormattedText },
    { "GetTextColor",           &CSimpleFontString_GetTextColor },
    { "SetTextColor",           &CSimpleFontString_SetTextColor },
    { "GetShadowColor",         &CSimpleFontString_GetShadowColor },
    { "SetShadowColor",         &CSimpleFontString_SetShadowColor },
    { "GetShadowOffset",        &CSimpleFontString_GetShadowOffset },
    { "SetShadowOffset",        &CSimpleFontString_SetShadowOffset },
    { "GetSpacing",             &CSimpleFontString_GetSpacing },
    { "SetSpacing",             &CSimpleFontString_SetSpacing },
    { "SetTextHeight",          &CSimpleFontString_SetTextHeight },
    { "GetStringWidth",         &CSimpleFontString_GetStringWidth },
    { "GetStringHeight",        &CSimpleFontString_GetStringHeight },
    { "GetJustifyH",            &CSimpleFontString_GetJustifyH },
    { "SetJustifyH",            &CSimpleFontString_SetJustifyH },
    { "GetJustifyV",            &CSimpleFontString_GetJustifyV },
    { "SetJustifyV",            &CSimpleFontString_SetJustifyV },
    { "CanNonSpaceWrap",        &CSimpleFontString_CanNonSpaceWrap },
    { "SetNonSpaceWrap",        &CSimpleFontString_SetNonSpaceWrap },
    { "CanWordWrap",            &CSimpleFontString_CanWordWrap },
    { "SetWordWrap",            &CSimpleFontString_SetWordWrap },
    { "GetIndentedWordWrap",    &CSimpleFontString_GetIndentedWordWrap },
    { "SetIndentedWordWrap",    &CSimpleFontString_SetIndentedWordWrap }
};
