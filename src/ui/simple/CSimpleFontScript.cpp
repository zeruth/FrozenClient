#include "ui/simple/CSimpleFontScript.hpp"
#include "ui/simple/CSimpleFont.hpp"
#include "ui/Util.hpp"
#include "ui/FrameScript.hpp"
#include "gx/Coordinate.hpp"
#include "ui/simple/CSimpleFontStringAttributes.hpp"
#include "ui/CScriptObject.hpp"
#include "ui/simple/CSimpleFontable.hpp"
#include "util/Lua.hpp"
#include "gx/font/TextBlock.hpp"
#include <storm/String.hpp>
#include "util/Unimplemented.hpp"
#include <cstdint>

// TODO the reference answers both of these from a virtual at vtable+0x1c that returns the object's
// type name as a string. CSimpleFont does not derive from CScriptObject here -- it is a
// FrameScript_Object plus a CSimpleFontable -- so it has neither that virtual nor the string form
// of IsA that CSimpleFontString uses. Adding the type-name virtual to FrameScript_Object is the
// missing piece; pushing a literal "Font" instead would be inventing the answer the virtual gives.
// TODO the font's pair is FUN_004a4120 (GetObjectType) and FUN_004a4170 (IsObjectType), found
// through its method-table run at 00ac1818-00ac18d0 and confirmed by both reading DAT_00b499b0.
// NOT the 004a8240/004a8290 the binding-table matcher offers: those read DAT_00b4997c, whose
// run holds Play, Pause, CreateAnimation and SetCurve -- the animation group, not a font.
//
// Still blocked, but on one thing rather than on the address. The reference reaches a type
// name through vtable+0x1c and a name-based IsA through +0x18; frozen's CSimpleFont derives
// from FrameScript_Object, not CScriptObject, so it has neither, and what string the
// reference returns is not recovered. An earlier attempt this session pushed a literal "Font"
// and was reverted; nothing in the shipped interface corroborates that spelling, so adding
// the virtuals means finding what they return first.
int32_t CSimpleFont_GetObjectType(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// TODO the IsObjectType the binding-table matcher offers here, FUN_0048be30, reads
// DAT_00b4793c -- the texture. The font's is FUN_004a4170, per the note above.
int32_t CSimpleFont_IsObjectType(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFont_GetName(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));

    lua_pushstring(L, font->GetName());

    return 1;
}

// ref: FUN_004a4280
int32_t CSimpleFont_SetFontObject(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));

    CSimpleFont* inherited = nullptr;

    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        inherited = static_cast<CSimpleFont*>(lua_touserdata(L, -1));
        lua_settop(L, -2);

        if (!inherited) {
            return luaL_error(L, "%s:SetFontObject(): Couldn't find 'this' in font object", font->GetDisplayName());
        }

        if (!inherited->IsA(CSimpleFont::GetObjectType())) {
            return luaL_error(L, "%s:SetFontObject(): Wrong object type, expected font", font->GetDisplayName());
        }
    } else if (lua_type(L, 2) == LUA_TSTRING) {
        auto fontName = lua_tostring(L, 2);
        inherited = CSimpleFont::GetFont(fontName, 0);

        if (!inherited) {
            return luaL_error(L, "%s:SetFontObject(): Couldn't find font named %s", font->GetDisplayName(), fontName);
        }
    } else if (lua_type(L, 2) != LUA_TNIL) {
        return luaL_error(L, "Usage: %s:SetFontObject(font or \"font\" or nil)", font->GetDisplayName());
    }

    for (CSimpleFontable* fontable = inherited; fontable; fontable = fontable->m_fontObject) {
        if (fontable == static_cast<CSimpleFontable*>(font)) {
            return luaL_error(L, "%s:SetFontObject(): Can't create a font object loop", font->GetDisplayName());
        }
    }

    font->SetFontObject(inherited);

    return 0;
}

// ref: FUN_004a42d0
int32_t CSimpleFont_GetFontObject(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));

    auto inherited = font->GetFontObject();

    if (!inherited) {
        lua_pushnil(L);

        return 1;
    }

    if (!inherited->lua_registered) {
        inherited->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, inherited->lua_objectRef);

    return 1;
}

// ref: FUN_004a4350
// Takes the same font-or-name argument as SetFontObject, with its own three error messages, but
// does something different with it: SetFontObject makes this font INHERIT from the other, while
// this one copies the other's attributes across and leaves the two unrelated afterwards. Note it
// does NOT accept nil, and has no loop guard, because nothing is linked.
int32_t CSimpleFont_CopyFontObject(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));

    CSimpleFont* source = nullptr;

    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        source = static_cast<CSimpleFont*>(lua_touserdata(L, -1));
        lua_settop(L, -2);

        if (!source) {
            return luaL_error(L, "%s:CopyFontObject(): Couldn't find 'this' in font object", font->GetDisplayName());
        }

        if (!source->IsA(CSimpleFont::GetObjectType())) {
            return luaL_error(L, "%s:CopyFontObject(): Wrong object type, expected font", font->GetDisplayName());
        }
    } else if (lua_type(L, 2) == LUA_TSTRING) {
        auto fontName = lua_tostring(L, 2);
        source = CSimpleFont::GetFont(fontName, 0);

        if (!source) {
            return luaL_error(L, "%s:CopyFontObject(): Couldn't find font named %s", font->GetDisplayName(), fontName);
        }
    } else {
        return luaL_error(L, "Usage: %s:CopyFontObject(font or \"font\")", font->GetDisplayName());
    }

    // Update copies FROM the receiver TO its argument, and only the attributes the source actually
    // has set, which is what the reference's copy does.
    source->m_attributes.Update(font->m_attributes, FLAG_COMPLETE_UPDATE);
    font->UpdateObjects();

    return 0;
}

// ref: FUN_004a43a0
int32_t CSimpleFont_SetFont(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetFont(\"font\", fontHeight [, flags])", font->GetDisplayName());
    }

    uint32_t fontFlags = 0x0;

    if (lua_isstring(L, 4)) {
        fontFlags = StringToFontFlags(lua_tostring(L, 4));
    }

    // See FontString_SetFont for why the height is not converted here: frozen keeps font heights in
    // the units the caller passed, consistently in both directions.
    if (attributes.SetFont(lua_tostring(L, 2), static_cast<float>(lua_tonumber(L, 3)), fontFlags)) {
        font->UpdateObjects();

        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CSimpleFont_GetFont(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));

    auto& attributes = font->m_attributes;
    auto fontFlags = attributes.m_fontFlags;
    char flags[64] = "";

    if (fontFlags & FONT_OUTLINE) {
        SStrPack(flags, (fontFlags & FONT_THICKOUTLINE) ? "THICKOUTLINE" : "OUTLINE", sizeof(flags));
    }

    if (fontFlags & FONT_MONOCHROME) {
        if (*flags) {
            SStrPack(flags, ",", sizeof(flags));
        }

        SStrPack(flags, "MONOCHROME", sizeof(flags));
    }

    lua_pushstring(L, attributes.m_font.GetString());
    lua_pushnumber(L, attributes.m_fontHeight);
    lua_pushstring(L, flags);

    return 3;
}

// ref: FUN_004a4440
int32_t CSimpleFont_SetAlpha(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetAlpha(alpha)", font->GetDisplayName());
    }

    // The reference rounds against a 255.0 stored as a double, and writes the byte straight into
    // the colour's alpha rather than going through SetColor.
    auto alpha = static_cast<uint8_t>(static_cast<int32_t>(lua_tonumber(L, 2) * 255.0 + 0.5));

    if (attributes.m_color.a != alpha) {
        attributes.m_color.a = alpha;
        font->UpdateObjects();
    }

    return 0;
}

// ref: FUN_004a4490
int32_t CSimpleFont_GetAlpha(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    lua_pushnumber(L, attributes.m_color.a * (1.0f / 255.0f));

    return 1;
}

// ref: FUN_004a4500
int32_t CSimpleFont_SetTextColor(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    CImVector color = { 0x00, 0x00, 0x00, 0x00 };
    FrameScript_GetColor(L, 2, color);

    attributes.SetColor(color);

    return 0;
}

int32_t CSimpleFont_GetTextColor(const char* name, CSimpleFont* font, lua_State* L) {
    auto& color = font->m_attributes.m_color;

    lua_pushnumber(L, color.r / 255.0);
    lua_pushnumber(L, color.g / 255.0);
    lua_pushnumber(L, color.b / 255.0);
    lua_pushnumber(L, color.a / 255.0);

    return 4;
}

int32_t CSimpleFont_GetTextColor(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));

    return CSimpleFont_GetTextColor(font->GetDisplayName(), font, L);
}

// ref: FUN_004a45e0
int32_t CSimpleFont_SetShadowColor(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    CImVector color = { 0x00, 0x00, 0x00, 0x00 };
    FrameScript_GetColor(L, 2, color);

    attributes.SetShadow(color, attributes.m_shadowOffset);

    return 0;
}

// ref: FUN_004a4630
int32_t CSimpleFont_GetShadowColor(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    auto& color = attributes.m_shadowColor;

    lua_pushnumber(L, color.r * (1.0f / 255.0f));
    lua_pushnumber(L, color.g * (1.0f / 255.0f));
    lua_pushnumber(L, color.b * (1.0f / 255.0f));
    lua_pushnumber(L, color.a * (1.0f / 255.0f));

    return 4;
}

// ref: FUN_004a4680
int32_t CSimpleFont_SetShadowOffset(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetShadowOffset(x, y)", font->GetDisplayName());
    }

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    C2Vector offset = {
        NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 2)) / scale),
        NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 3)) / scale)
    };

    attributes.SetShadow(attributes.m_shadowColor, offset);

    return 0;
}

// ref: FUN_004a46d0
int32_t CSimpleFont_GetShadowOffset(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(attributes.m_shadowOffset.x) * scale);
    lua_pushnumber(L, DDCToNDCWidth(attributes.m_shadowOffset.y) * scale);

    return 2;
}

// ref: FUN_004a4720
int32_t CSimpleFont_SetSpacing(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetSpacing(spacing)", font->GetDisplayName());
    }

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    attributes.SetSpacing(NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 2)) / scale));

    return 0;
}

// ref: FUN_004a4770
int32_t CSimpleFont_GetSpacing(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(attributes.m_spacing) * scale);

    return 1;
}

// ref: FUN_004a4ab0
int32_t CSimpleFont_SetJustifyH(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    uint32_t justifyH;

    if (!lua_isstring(L, 2) || !StringToJustify(lua_tostring(L, 2), justifyH)) {
        return luaL_error(L, "Usage: %s:SetJustifyH(\"justify\")", font->GetDisplayName());
    }

    attributes.SetJustifyH(justifyH);

    return 0;
}

// ref: FUN_004a47e0
int32_t CSimpleFont_GetJustifyH(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    lua_pushstring(L, JustifyToString(attributes.m_styleFlags & 0x7));

    return 1;
}

// ref: FUN_004a4b00
int32_t CSimpleFont_SetJustifyV(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    uint32_t justifyV;

    if (!lua_isstring(L, 2) || !StringToJustify(lua_tostring(L, 2), justifyV)) {
        return luaL_error(L, "Usage: %s:SetJustifyV(\"justify\")", font->GetDisplayName());
    }

    attributes.SetJustifyV(justifyV);

    return 0;
}

// ref: FUN_004a4840
int32_t CSimpleFont_GetJustifyV(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    lua_pushstring(L, JustifyToString(attributes.m_styleFlags & 0x38));

    return 1;
}

// ref: FUN_004a4b50
int32_t CSimpleFont_SetIndentedWordWrap(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    attributes.SetIndented(StringToBOOL(L, 2, 1));

    return 0;
}

// ref: FUN_004a48a0
int32_t CSimpleFont_GetIndentedWordWrap(lua_State* L) {
    auto type = CSimpleFont::GetObjectType();
    auto font = static_cast<CSimpleFont*>(FrameScript_GetObjectThis(L, type));
    auto& attributes = font->m_attributes;

    if (attributes.m_styleFlags & 0x20000) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

FrameScript_Method SimpleFontMethods[NUM_SIMPLE_FONT_SCRIPT_METHODS] = {
    { "GetObjectType",          &CSimpleFont_GetObjectType },
    { "IsObjectType",           &CSimpleFont_IsObjectType },
    { "GetName",                &CSimpleFont_GetName },
    { "SetFontObject",          &CSimpleFont_SetFontObject },
    { "GetFontObject",          &CSimpleFont_GetFontObject },
    { "CopyFontObject",         &CSimpleFont_CopyFontObject },
    { "SetFont",                &CSimpleFont_SetFont },
    { "GetFont",                &CSimpleFont_GetFont },
    { "SetAlpha",               &CSimpleFont_SetAlpha },
    { "GetAlpha",               &CSimpleFont_GetAlpha },
    { "SetTextColor",           &CSimpleFont_SetTextColor },
    { "GetTextColor",           &CSimpleFont_GetTextColor },
    { "SetShadowColor",         &CSimpleFont_SetShadowColor },
    { "GetShadowColor",         &CSimpleFont_GetShadowColor },
    { "SetShadowOffset",        &CSimpleFont_SetShadowOffset },
    { "GetShadowOffset",        &CSimpleFont_GetShadowOffset },
    { "SetSpacing",             &CSimpleFont_SetSpacing },
    { "GetSpacing",             &CSimpleFont_GetSpacing },
    { "SetJustifyH",            &CSimpleFont_SetJustifyH },
    { "GetJustifyH",            &CSimpleFont_GetJustifyH },
    { "SetJustifyV",            &CSimpleFont_SetJustifyV },
    { "GetJustifyV",            &CSimpleFont_GetJustifyV },
    { "SetIndentedWordWrap",    &CSimpleFont_SetIndentedWordWrap },
    { "GetIndentedWordWrap",    &CSimpleFont_GetIndentedWordWrap }
};
