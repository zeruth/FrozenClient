#include "ui/simple/CSimpleFontScript.hpp"
#include "ui/simple/CSimpleFont.hpp"
#include "ui/Util.hpp"
#include "ui/FrameScript.hpp"
#include "gx/Coordinate.hpp"
#include "ui/simple/CSimpleFontStringAttributes.hpp"
#include "util/Lua.hpp"
#include "gx/font/TextBlock.hpp"
#include <storm/String.hpp>
#include "util/Unimplemented.hpp"
#include <cstdint>

int32_t CSimpleFont_GetObjectType(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFont_IsObjectType(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFont_GetName(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFont_SetFontObject(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFont_GetFontObject(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFont_CopyFontObject(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFont_SetFont(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
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

int32_t CSimpleFont_SetAlpha(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFont_GetAlpha(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
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
