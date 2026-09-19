#include "ui/simple/CSimpleHTMLScript.hpp"
#include "ui/simple/CSimpleHTML.hpp"
#include "ui/simple/CSimpleFont.hpp"
#include "ui/simple/CSimpleFontable.hpp"
#include "ui/simple/CSimpleFontedFrameFont.hpp"
#include "ui/FrameScript.hpp"
#include "ui/Util.hpp"
#include "gx/Coordinate.hpp"
#include "util/Lua.hpp"
#include <storm/String.hpp>
#include <cstdint>

// ---------------------------------------------------------------------------------------------
// A SimpleHTML frame keeps one font object per HTML element -- body, h1, h2, h3 -- and every font
// string it lays out inherits from the one for its element. So the FontInstance half of its script
// API never touches a font string directly: it names an element, picks that font object out of
// m_fonts, and applies the change there, from where CSimpleFont::UpdateObjects pushes it down to
// every string already drawn with it.
//
// The reference funnels all eighteen of those methods through one shared helper family
// (FUN_004a3790, FUN_004a38f0, FUN_004a3a50 ...) taking the owner's display name, the font object
// and the Lua state. The same family serves CSimpleMessageFrame (+0x2dc),
// CSimpleScrollingMessageFrame (+0x2cc) and CSimpleEditBox (+0x2ac), each of which passes the one
// CSimpleFontedFrameFont it owns; CSimpleHTML passes m_fonts[element] (+0x2d0 + 4 * element).
// Frozen's ports of that family live in CSimpleFontStringScript.cpp typed for CSimpleFontString,
// which cannot take a font object, so the helpers below are the font-object form of the same
// operations, kept local to this file. They carry no "// ref:" tag of their own: the family's
// addresses are already claimed in CSimpleFontStringScript.cpp, and one address can only have one
// counterpart.
// ---------------------------------------------------------------------------------------------

// ref: FUN_009748f0
// Every method below takes an optional element name as its first argument -- SetFont("h2", ...)
// addresses the header-2 font, SetFont(...) the body font. The reference reads that argument,
// removes it from the stack so the arguments after it land where the shared helpers look for them,
// and returns the text type it named.
static HTML_TEXT_TYPE CSimpleHTML_GetTextTypeArg(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) {
        return HTML_TEXT_NORMAL;
    }

    auto element = lua_tostring(L, 2);

    if (!SStrCmpI(element, "p", STORM_MAX_STR)) {
        lua_remove(L, 2);

        return HTML_TEXT_NORMAL;
    }

    if (!SStrCmpI(element, "h1", STORM_MAX_STR)) {
        lua_remove(L, 2);

        return HTML_TEXT_HEADER1;
    }

    if (!SStrCmpI(element, "h2", STORM_MAX_STR)) {
        lua_remove(L, 2);

        return HTML_TEXT_HEADER2;
    }

    if (!SStrCmpI(element, "h3", STORM_MAX_STR)) {
        lua_remove(L, 2);

        return HTML_TEXT_HEADER3;
    }

    return HTML_TEXT_NORMAL;
}

// Resolves both halves of the call at once: the frame the method was called on, and the font object
// for the element its first argument named.
static CSimpleFontedFrameFont* CSimpleHTML_GetElementFont(lua_State* L, CSimpleHTML*& html) {
    auto type = CSimpleHTML::GetObjectType();
    html = static_cast<CSimpleHTML*>(FrameScript_GetObjectThis(L, type));

    return html->m_fonts[CSimpleHTML_GetTextTypeArg(L)];
}

static int32_t HTMLFont_SetFontObject(const char* displayName, CSimpleFont* font, lua_State* L) {
    CSimpleFont* fontObject = nullptr;

    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        fontObject = static_cast<CSimpleFont*>(lua_touserdata(L, -1));
        lua_settop(L, -2);

        if (!fontObject) {
            return luaL_error(L, "%s:SetFontObject(): Couldn't find 'this' in font object", displayName);
        }

        if (!fontObject->IsA(CSimpleFont::GetObjectType())) {
            return luaL_error(L, "%s:SetFontObject(): Wrong object type, expected font", displayName);
        }
    } else if (lua_type(L, 2) == LUA_TSTRING) {
        auto fontName = lua_tostring(L, 2);
        fontObject = CSimpleFont::GetFont(fontName, 0);

        if (!fontObject) {
            return luaL_error(L, "%s:SetFontObject(): Couldn't find font named %s", displayName, fontName);
        }
    } else if (lua_type(L, 2) != LUA_TNIL) {
        return luaL_error(L, "Usage: %s:SetFontObject(font or \"font\" or nil)", displayName);
    }

    // A font object inherits from another font object, so adopting one that already inherits from
    // this element's font would make the chain cyclic and hang every later walk of it.
    for (CSimpleFontable* fontable = fontObject; fontable; fontable = fontable->m_fontObject) {
        if (fontable == static_cast<CSimpleFontable*>(font)) {
            return luaL_error(L, "%s:SetFontObject(): Can't create a font object loop", displayName);
        }
    }

    font->SetFontObject(fontObject);

    return 0;
}

static int32_t HTMLFont_GetFontObject(const char* displayName, CSimpleFont* font, lua_State* L) {
    auto fontObject = font->GetFontObject();

    if (!fontObject) {
        lua_pushnil(L);

        return 1;
    }

    if (!fontObject->lua_registered) {
        fontObject->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, fontObject->lua_objectRef);

    return 1;
}

static int32_t HTMLFont_SetFont(const char* displayName, CSimpleFont* font, lua_State* L) {
    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetFont(\"font\", fontHeight [, flags])", displayName);
    }

    // Divergence, deliberate and shared with the font string and font object bindings: the
    // reference converts the height through NDCToDDCWidth(height / CoordinateGetAspectCompensation())
    // here and back again in GetFont. Frozen keeps the height in the units the caller passed.
    auto fontHeight = static_cast<float>(lua_tonumber(L, 3));

    uint32_t fontFlags = 0x0;

    if (lua_isstring(L, 4)) {
        fontFlags = StringToFontFlags(lua_tostring(L, 4));
    }

    if (font->m_attributes.SetFont(lua_tostring(L, 2), fontHeight, fontFlags)) {
        font->m_fontableFlags &= ~FLAG_FONT_UPDATE;
        font->UpdateObjects();

        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

static int32_t HTMLFont_GetFont(const char* displayName, CSimpleFont* font, lua_State* L) {
    auto fontName = font->m_attributes.m_font.GetString();

    if (!fontName) {
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushnil(L);

        return 3;
    }

    lua_pushstring(L, fontName);
    lua_pushnumber(L, font->m_attributes.m_fontHeight);
    lua_pushstring(L, FontFlagsToString(font->m_attributes.m_fontFlags));

    return 3;
}

static int32_t HTMLFont_SetTextColor(const char* displayName, CSimpleFont* font, lua_State* L) {
    CImVector color = { 0x00, 0x00, 0x00, 0x00 };
    FrameScript_GetColor(L, 2, color);

    font->m_attributes.SetColor(color);
    font->m_fontableFlags &= ~FLAG_COLOR_UPDATE;
    font->UpdateObjects();

    return 0;
}

static int32_t HTMLFont_GetTextColor(const char* displayName, CSimpleFont* font, lua_State* L) {
    auto& color = font->m_attributes.m_color;

    lua_pushnumber(L, color.r * (1.0f / 255.0f));
    lua_pushnumber(L, color.g * (1.0f / 255.0f));
    lua_pushnumber(L, color.b * (1.0f / 255.0f));
    lua_pushnumber(L, color.a * (1.0f / 255.0f));

    return 4;
}

static int32_t HTMLFont_SetShadowColor(const char* displayName, CSimpleFont* font, lua_State* L) {
    CImVector color = { 0x00, 0x00, 0x00, 0x00 };
    FrameScript_GetColor(L, 2, color);

    // The attribute block carries the shadow colour and offset together, so the offset rides along
    // unchanged.
    font->m_attributes.SetShadow(color, font->m_attributes.m_shadowOffset);
    font->m_fontableFlags &= ~FLAG_SHADOW_UPDATE;
    font->UpdateObjects();

    return 0;
}

static int32_t HTMLFont_GetShadowColor(const char* displayName, CSimpleFont* font, lua_State* L) {
    auto& color = font->m_attributes.m_shadowColor;

    lua_pushnumber(L, color.r * (1.0f / 255.0f));
    lua_pushnumber(L, color.g * (1.0f / 255.0f));
    lua_pushnumber(L, color.b * (1.0f / 255.0f));
    lua_pushnumber(L, color.a * (1.0f / 255.0f));

    return 4;
}

static int32_t HTMLFont_SetShadowOffset(const char* displayName, CSimpleFont* font, lua_State* L) {
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

    font->m_attributes.SetShadow(font->m_attributes.m_shadowColor, offset);
    font->m_fontableFlags &= ~FLAG_SHADOW_UPDATE;
    font->UpdateObjects();

    return 0;
}

static int32_t HTMLFont_GetShadowOffset(const char* displayName, CSimpleFont* font, lua_State* L) {
    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(font->m_attributes.m_shadowOffset.x) * scale);
    lua_pushnumber(L, DDCToNDCWidth(font->m_attributes.m_shadowOffset.y) * scale);

    return 2;
}

static int32_t HTMLFont_SetSpacing(const char* displayName, CSimpleFont* font, lua_State* L) {
    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetSpacing(spacing)", displayName);
    }

    auto spacing = static_cast<float>(lua_tonumber(L, 2));
    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    font->m_attributes.SetSpacing(NDCToDDCWidth(spacing / scale));
    font->m_fontableFlags &= ~FLAG_SPACING_UPDATE;
    font->UpdateObjects();

    return 0;
}

static int32_t HTMLFont_GetSpacing(const char* displayName, CSimpleFont* font, lua_State* L) {
    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(font->m_attributes.m_spacing) * scale);

    return 1;
}

static int32_t HTMLFont_SetJustifyH(const char* displayName, CSimpleFont* font, lua_State* L) {
    uint32_t justifyH;

    if (!lua_isstring(L, 2) || !StringToJustify(lua_tostring(L, 2), justifyH)) {
        return luaL_error(L, "Usage: %s:SetJustifyH(\"justify\")", displayName);
    }

    font->m_attributes.SetJustifyH(justifyH);
    font->m_fontableFlags &= ~FLAG_STYLE_UPDATE;
    font->UpdateObjects();

    return 0;
}

static int32_t HTMLFont_GetJustifyH(const char* displayName, CSimpleFont* font, lua_State* L) {
    lua_pushstring(L, JustifyToString(font->m_attributes.m_styleFlags & 0x7));

    return 1;
}

static int32_t HTMLFont_SetJustifyV(const char* displayName, CSimpleFont* font, lua_State* L) {
    uint32_t justifyV;

    if (!lua_isstring(L, 2) || !StringToJustify(lua_tostring(L, 2), justifyV)) {
        return luaL_error(L, "Usage: %s:SetJustifyV(\"justify\")", displayName);
    }

    font->m_attributes.SetJustifyV(justifyV);
    font->m_fontableFlags &= ~FLAG_STYLE_UPDATE;
    font->UpdateObjects();

    return 0;
}

static int32_t HTMLFont_GetJustifyV(const char* displayName, CSimpleFont* font, lua_State* L) {
    lua_pushstring(L, JustifyToString(font->m_attributes.m_styleFlags & 0x38));

    return 1;
}

static int32_t HTMLFont_SetIndentedWordWrap(const char* displayName, CSimpleFont* font, lua_State* L) {
    font->m_attributes.SetIndented(StringToBOOL(L, 2, 1));
    font->m_fontableFlags &= ~FLAG_STYLE_UPDATE;
    font->UpdateObjects();

    return 0;
}

static int32_t HTMLFont_GetIndentedWordWrap(const char* displayName, CSimpleFont* font, lua_State* L) {
    if (font->m_attributes.m_styleFlags & 0x20000) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_00974a10
int32_t CSimpleHTML_SetFontObject(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_SetFontObject(html->GetDisplayName(), font, L);
}

// ref: FUN_00974a70
int32_t CSimpleHTML_GetFontObject(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_GetFontObject(html->GetDisplayName(), font, L);
}

// ref: FUN_00974ad0
int32_t CSimpleHTML_SetFont(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_SetFont(html->GetDisplayName(), font, L);
}

// ref: FUN_00974b30
int32_t CSimpleHTML_GetFont(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_GetFont(html->GetDisplayName(), font, L);
}

// ref: FUN_00974b90
int32_t CSimpleHTML_SetTextColor(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_SetTextColor(html->GetDisplayName(), font, L);
}

// ref: FUN_00974bf0
int32_t CSimpleHTML_GetTextColor(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_GetTextColor(html->GetDisplayName(), font, L);
}

// ref: FUN_00974c50
int32_t CSimpleHTML_SetShadowColor(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_SetShadowColor(html->GetDisplayName(), font, L);
}

// ref: FUN_00974cb0
int32_t CSimpleHTML_GetShadowColor(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_GetShadowColor(html->GetDisplayName(), font, L);
}

// ref: FUN_00974d10
int32_t CSimpleHTML_SetShadowOffset(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_SetShadowOffset(html->GetDisplayName(), font, L);
}

// ref: FUN_00974d70
int32_t CSimpleHTML_GetShadowOffset(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_GetShadowOffset(html->GetDisplayName(), font, L);
}

// ref: FUN_00974dd0
int32_t CSimpleHTML_SetSpacing(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_SetSpacing(html->GetDisplayName(), font, L);
}

// ref: FUN_00974e30
int32_t CSimpleHTML_GetSpacing(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_GetSpacing(html->GetDisplayName(), font, L);
}

// ref: FUN_00974e90
int32_t CSimpleHTML_SetJustifyH(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_SetJustifyH(html->GetDisplayName(), font, L);
}

// ref: FUN_00974ef0
int32_t CSimpleHTML_GetJustifyH(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_GetJustifyH(html->GetDisplayName(), font, L);
}

// ref: FUN_00974f50
int32_t CSimpleHTML_SetJustifyV(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_SetJustifyV(html->GetDisplayName(), font, L);
}

// ref: FUN_00974fb0
int32_t CSimpleHTML_GetJustifyV(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_GetJustifyV(html->GetDisplayName(), font, L);
}

// ref: FUN_00975010
int32_t CSimpleHTML_SetIndentedWordWrap(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_SetIndentedWordWrap(html->GetDisplayName(), font, L);
}

// ref: FUN_00975070
int32_t CSimpleHTML_GetIndentedWordWrap(lua_State* L) {
    CSimpleHTML* html;
    auto font = CSimpleHTML_GetElementFont(L, html);

    return HTMLFont_GetIndentedWordWrap(html->GetDisplayName(), font, L);
}

int32_t CSimpleHTML_SetText(lua_State* L) {
    auto type = CSimpleHTML::GetObjectType();
    auto html = static_cast<CSimpleHTML*>(FrameScript_GetObjectThis(L, type));

    auto text = lua_tostring(L, 2);
    html->SetText(text, nullptr);

    return 0;
}

// ref: FUN_00975120
int32_t CSimpleHTML_SetHyperlinkFormat(lua_State* L) {
    auto type = CSimpleHTML::GetObjectType();
    auto html = static_cast<CSimpleHTML*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetHyperlinkFormat(\"format\")", html->GetDisplayName());
    }

    html->SetHyperlinkFormat(lua_tostring(L, 2));

    return 0;
}

// ref: FUN_009751a0
int32_t CSimpleHTML_GetHyperlinkFormat(lua_State* L) {
    auto type = CSimpleHTML::GetObjectType();
    auto html = static_cast<CSimpleHTML*>(FrameScript_GetObjectThis(L, type));

    lua_pushstring(L, html->m_hyperlinkFormat);

    return 1;
}

// ref: FUN_009751e0
int32_t CSimpleHTML_SetHyperlinksEnabled(lua_State* L) {
    auto type = CSimpleHTML::GetObjectType();
    auto html = static_cast<CSimpleHTML*>(FrameScript_GetObjectThis(L, type));

    auto enabled = StringToBOOL(L, 2, 1);

    if (enabled != html->m_hyperlinksEnabled) {
        html->m_hyperlinksEnabled = enabled;

        // Divergence: the reference follows the flag with a rebuild of the frame's hyperlink
        // buttons over the content it already holds (FUN_0096d500), so the change takes effect
        // without the text being set again. Frozen's content nodes do not create hyperlink buttons
        // yet, so there is nothing to rebuild.
    }

    return 0;
}

// ref: FUN_00975240
int32_t CSimpleHTML_GetHyperlinksEnabled(lua_State* L) {
    auto type = CSimpleHTML::GetObjectType();
    auto html = static_cast<CSimpleHTML*>(FrameScript_GetObjectThis(L, type));

    if (html->m_hyperlinksEnabled) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

FrameScript_Method SimpleHTMLMethods[NUM_SIMPLE_HTML_SCRIPT_METHODS] = {
    { "SetFontObject",              &CSimpleHTML_SetFontObject },
    { "GetFontObject",              &CSimpleHTML_GetFontObject },
    { "SetFont",                    &CSimpleHTML_SetFont },
    { "GetFont",                    &CSimpleHTML_GetFont },
    { "SetTextColor",               &CSimpleHTML_SetTextColor },
    { "GetTextColor",               &CSimpleHTML_GetTextColor },
    { "SetShadowColor",             &CSimpleHTML_SetShadowColor },
    { "GetShadowColor",             &CSimpleHTML_GetShadowColor },
    { "SetShadowOffset",            &CSimpleHTML_SetShadowOffset },
    { "GetShadowOffset",            &CSimpleHTML_GetShadowOffset },
    { "SetSpacing",                 &CSimpleHTML_SetSpacing },
    { "GetSpacing",                 &CSimpleHTML_GetSpacing },
    { "SetJustifyH",                &CSimpleHTML_SetJustifyH },
    { "GetJustifyH",                &CSimpleHTML_GetJustifyH },
    { "SetJustifyV",                &CSimpleHTML_SetJustifyV },
    { "GetJustifyV",                &CSimpleHTML_GetJustifyV },
    { "SetIndentedWordWrap",        &CSimpleHTML_SetIndentedWordWrap },
    { "GetIndentedWordWrap",        &CSimpleHTML_GetIndentedWordWrap },
    { "SetText",                    &CSimpleHTML_SetText },
    { "SetHyperlinkFormat",         &CSimpleHTML_SetHyperlinkFormat },
    { "GetHyperlinkFormat",         &CSimpleHTML_GetHyperlinkFormat },
    { "SetHyperlinksEnabled",       &CSimpleHTML_SetHyperlinksEnabled },
    { "GetHyperlinksEnabled",       &CSimpleHTML_GetHyperlinksEnabled }
};
