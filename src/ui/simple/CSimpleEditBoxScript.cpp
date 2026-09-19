#include "ui/simple/CSimpleEditBoxScript.hpp"
#include "ui/simple/CSimpleEditBox.hpp"
#include "ui/simple/CSimpleFontStringScript.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "gx/Coordinate.hpp"
#include <storm/String.hpp>
#include "util/Lua.hpp"
#include "ui/Util.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>

// ref: FUN_00975310
int32_t CSimpleEditBox_SetFontObject(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetFontObject(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975370
int32_t CSimpleEditBox_GetFontObject(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetFontObject(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_009753d0
int32_t CSimpleEditBox_SetFont(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetFont(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975430
int32_t CSimpleEditBox_GetFont(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetFont(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975490
int32_t CSimpleEditBox_SetTextColor(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetTextColor(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_009754f0
int32_t CSimpleEditBox_GetTextColor(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetTextColor(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975550
int32_t CSimpleEditBox_SetShadowColor(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetShadowColor(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_009755b0
int32_t CSimpleEditBox_GetShadowColor(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetShadowColor(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975610
int32_t CSimpleEditBox_SetShadowOffset(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetShadowOffset(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975670
int32_t CSimpleEditBox_GetShadowOffset(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetShadowOffset(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_009756d0
int32_t CSimpleEditBox_SetSpacing(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetSpacing(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975730
int32_t CSimpleEditBox_GetSpacing(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetSpacing(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975790
int32_t CSimpleEditBox_SetJustifyH(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetJustifyH(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_009757f0
int32_t CSimpleEditBox_GetJustifyH(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetJustifyH(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975850
int32_t CSimpleEditBox_SetJustifyV(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetJustifyV(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_009758b0
int32_t CSimpleEditBox_GetJustifyV(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetJustifyV(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975910
int32_t CSimpleEditBox_SetIndentedWordWrap(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_SetIndentedWordWrap(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_00975970
int32_t CSimpleEditBox_GetIndentedWordWrap(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    return FontString_GetIndentedWordWrap(editBox->GetDisplayName(), editBox->m_string, L);
}

// ref: FUN_009759d0
int32_t CSimpleEditBox_SetAutoFocus(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    editBox->SetAutoFocus(StringToBOOL(L, 2, 1));

    return 0;
}

// ref: FUN_00975a20
int32_t CSimpleEditBox_IsAutoFocus(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (editBox->m_autoFocus) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_00975a80
int32_t CSimpleEditBox_SetCountInvisibleLetters(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    editBox->SetCountInvisibleLetters(StringToBOOL(L, 2, 1));

    return 0;
}

// ref: FUN_00975ad0
int32_t CSimpleEditBox_IsCountInvisibleLetters(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (editBox->m_countInvisibleLetters) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_00975b30
int32_t CSimpleEditBox_SetMultiLine(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    editBox->SetMultiLine(StringToBOOL(L, 2, 1));

    return 0;
}

// ref: FUN_00975b80
int32_t CSimpleEditBox_IsMultiLine(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (editBox->m_multiline) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_00975be0
int32_t CSimpleEditBox_SetNumeric(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    editBox->SetNumeric(StringToBOOL(L, 2, 1));

    return 0;
}

// ref: FUN_00975c30
int32_t CSimpleEditBox_IsNumeric(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (editBox->m_numeric) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_00975c90
int32_t CSimpleEditBox_SetPassword(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    editBox->SetPassword(StringToBOOL(L, 2, 1));

    return 0;
}

// ref: FUN_00975ce0
int32_t CSimpleEditBox_IsPassword(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (editBox->m_password) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_00975d40
int32_t CSimpleEditBox_SetBlinkSpeed(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetBlinkSpeed(speed)", editBox->GetDisplayName());
    }

    editBox->m_cursorBlinkSpeed = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

// ref: FUN_00975dc0
int32_t CSimpleEditBox_GetBlinkSpeed(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, editBox->m_cursorBlinkSpeed);

    return 1;
}

// ref: FUN_00975e10
int32_t CSimpleEditBox_Insert(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (lua_isstring(L, 2)) {
        // TODO lua_tainted
        editBox->Insert(lua_tostring(L, 2), nullptr, 1, 0, 0);
    }

    return 0;
}

int32_t CSimpleEditBox_SetText(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetText(\"text\")", editBox->GetDisplayName());
    }

    // TODO lua_tainted
    auto text = lua_tostring(L, 2);
    editBox->SetText(text, nullptr);

    return 0;
}

int32_t CSimpleEditBox_GetText(lua_State* L) {
    int32_t type = CSimpleEditBox::GetObjectType();
    CSimpleEditBox* editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    // TODO
    // - taint management
    // if (editBox->m_dwordC && lua_taintexpected && !lua_taintedclosure) {
    //     lua_tainted = editBox->simpleeditbox_dwordC;
    // }

    lua_pushstring(L, editBox->m_text);

    return 1;
}

// ref: FUN_00975f80
int32_t CSimpleEditBox_SetNumber(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetNumber(number)", editBox->GetDisplayName());
    }

    // Lua renders the number for us, so this is SetText on its decimal spelling.
    // TODO lua_tainted
    editBox->SetText(lua_tostring(L, 2), nullptr);

    return 0;
}

// ref: FUN_00976010
int32_t CSimpleEditBox_GetNumber(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, SStrToFloat(editBox->m_text));

    return 1;
}

int32_t CSimpleEditBox_HighlightText(lua_State* L) {
    // HighlightText() with no arguments selects everything; with a start and end it selects that
    // range. The interface calls the one-argument form to clear a selection.
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    int32_t start = 0;
    int32_t end = editBox->m_textLength;

    if (lua_type(L, 2) == LUA_TNUMBER) {
        start = static_cast<int32_t>(lua_tonumber(L, 2));
        end = lua_type(L, 3) == LUA_TNUMBER ? static_cast<int32_t>(lua_tonumber(L, 3)) : start;
    }

    editBox->m_highlightLeft = start < 0 ? 0 : start;
    editBox->m_highlightRight = end > editBox->m_textLength ? editBox->m_textLength : end;
    editBox->m_dirtyFlags |= 0x2;

    return 0;
}

// ref: FUN_00976110
int32_t CSimpleEditBox_AddHistoryLine(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (lua_gettop(L) != 2) {
        return luaL_error(L, "Usage: %s:AddHistoryLine(\"text\")", editBox->GetDisplayName());
    }

    // TODO lua_tainted
    editBox->AddHistoryLine(lua_tostring(L, 2), nullptr);

    return 0;
}

// ref: FUN_009761a0
int32_t CSimpleEditBox_ClearHistory(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    editBox->ClearHistory();

    return 0;
}

// ref: FUN_009761e0
int32_t CSimpleEditBox_SetTextInsets(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4) || !lua_isnumber(L, 5)) {
        return luaL_error(L, "Usage: %s:SetTextInsets(l, r, t, b)", editBox->GetDisplayName());
    }

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    editBox->SetTextInsets(
        NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 2)) / scale),
        NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 3)) / scale),
        NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 4)) / scale),
        NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 5)) / scale)
    );

    return 0;
}

// ref: FUN_00976330
int32_t CSimpleEditBox_GetTextInsets(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    float left, right, top, bottom;
    editBox->GetTextInsets(left, right, top, bottom);

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(left) * scale);
    lua_pushnumber(L, DDCToNDCWidth(right) * scale);
    lua_pushnumber(L, DDCToNDCWidth(top) * scale);
    lua_pushnumber(L, DDCToNDCWidth(bottom) * scale);

    return 4;
}

int32_t CSimpleEditBox_SetFocus(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    CSimpleEditBox::SetKeyboardFocus(editBox);

    return 0;
}

int32_t CSimpleEditBox_ClearFocus(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    CSimpleEditBox::ClearKeyboardFocus(editBox, true);

    return 0;
}

// ref: FUN_00976490
int32_t CSimpleEditBox_HasFocus(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, editBox == CSimpleEditBox::s_currentFocus);

    return 1;
}

// ref: FUN_009764e0
int32_t CSimpleEditBox_SetMaxBytes(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (lua_type(L, 2) != LUA_TNUMBER) {
        return luaL_error(L, "Usage: %s:SetMaxBytes(max)", editBox->GetDisplayName());
    }

    int32_t max = static_cast<int32_t>(lua_tonumber(L, 2));
    editBox->m_textLengthMax = max > 0 ? max - 1 : -1;

    return 0;
}

// ref: FUN_00976580
int32_t CSimpleEditBox_GetMaxBytes(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, editBox->m_textLengthMax + 1);

    return 1;
}

// ref: FUN_009765d0
int32_t CSimpleEditBox_SetMaxLetters(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (lua_type(L, 2) != LUA_TNUMBER) {
        return luaL_error(L, "Usage: %s:SetMaxLetters(max)", editBox->GetDisplayName());
    }

    editBox->m_textLettersMax = static_cast<int32_t>(lua_tonumber(L, 2));

    return 0;
}

// ref: FUN_00976650
int32_t CSimpleEditBox_GetMaxLetters(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, editBox->m_textLettersMax);

    return 1;
}

// ref: FUN_009766a0
int32_t CSimpleEditBox_GetNumLetters(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, editBox->LetterCount());

    return 1;
}

// ref: FUN_00976720
int32_t CSimpleEditBox_GetHistoryLines(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, editBox->m_historyLines);

    return 1;
}

// ref: FUN_00976770
int32_t CSimpleEditBox_SetHistoryLines(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (lua_isnumber(L, 2)) {
        int32_t lines = static_cast<int32_t>(lua_tonumber(L, 2));

        if (lines > 0) {
            editBox->SetHistoryLines(lines);

            return 0;
        }
    }

    return luaL_error(L, "Usage: %s:SetHistoryLines(numLines)", editBox->GetDisplayName());
}

// ref: FUN_00976800
int32_t CSimpleEditBox_GetInputLanguage(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    lua_pushstring(L, editBox->GetInputLanguage());

    return 1;
}

// ref: FUN_00976850
int32_t CSimpleEditBox_ToggleInputLanguage(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    editBox->ToggleInputLanguage();

    return 0;
}

// ref: FUN_00976890
int32_t CSimpleEditBox_SetAltArrowKeyMode(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    editBox->SetAltArrowKeyMode(StringToBOOL(L, 2, 1));

    return 0;
}

// ref: FUN_009768e0
int32_t CSimpleEditBox_GetAltArrowKeyMode(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (editBox->m_ignoreArrows) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_00976940
int32_t CSimpleEditBox_IsInIMECompositionMode(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    // TODO the reference reports its IME composition state here; Frozen has no IME, so never.
    lua_pushnil(L);

    return 1;
}

int32_t CSimpleEditBox_SetCursorPosition(lua_State* L) {
    // Not cosmetic. FrameXML's shared scrolling-edit template positions the cursor and then reads
    // back self.cursorOffset, which only the OnCursorChanged script writes -- and that script only
    // runs off the back of this call. Left unimplemented, the field stayed nil for ever and every
    // error the script error frame displayed raised another error displaying it, ~4400 per run.
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    if (lua_type(L, 2) != LUA_TNUMBER) {
        return luaL_error(L, "Usage: %s:SetCursorPosition(position)", editBox->GetDisplayName());
    }

    editBox->SetCursorPosition(static_cast<int32_t>(lua_tonumber(L, 2)));

    return 0;
}

int32_t CSimpleEditBox_GetCursorPosition(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, editBox->m_cursorPos);

    return 1;
}

// ref: FUN_00976a70
int32_t CSimpleEditBox_GetUTF8CursorPosition(lua_State* L) {
    auto type = CSimpleEditBox::GetObjectType();
    auto editBox = static_cast<CSimpleEditBox*>(FrameScript_GetObjectThis(L, type));

    // Characters before the cursor, not bytes: continuation bytes do not advance the count.
    int32_t n = 0;

    for (int32_t i = 0; i < editBox->m_cursorPos && editBox->m_text[i]; i++) {
        if ((editBox->m_text[i] & 0xC0) != 0x80) {
            n++;
        }
    }

    lua_pushnumber(L, n);

    return 1;
}

FrameScript_Method SimpleEditBoxMethods[NUM_SIMPLE_EDIT_BOX_SCRIPT_METHODS] = {
    { "SetFontObject",                  &CSimpleEditBox_SetFontObject },
    { "GetFontObject",                  &CSimpleEditBox_GetFontObject },
    { "SetFont",                        &CSimpleEditBox_SetFont },
    { "GetFont",                        &CSimpleEditBox_GetFont },
    { "SetTextColor",                   &CSimpleEditBox_SetTextColor },
    { "GetTextColor",                   &CSimpleEditBox_GetTextColor },
    { "SetShadowColor",                 &CSimpleEditBox_SetShadowColor },
    { "GetShadowColor",                 &CSimpleEditBox_GetShadowColor },
    { "SetShadowOffset",                &CSimpleEditBox_SetShadowOffset },
    { "GetShadowOffset",                &CSimpleEditBox_GetShadowOffset },
    { "SetSpacing",                     &CSimpleEditBox_SetSpacing },
    { "GetSpacing",                     &CSimpleEditBox_GetSpacing },
    { "SetJustifyH",                    &CSimpleEditBox_SetJustifyH },
    { "GetJustifyH",                    &CSimpleEditBox_GetJustifyH },
    { "SetJustifyV",                    &CSimpleEditBox_SetJustifyV },
    { "GetJustifyV",                    &CSimpleEditBox_GetJustifyV },
    { "SetIndentedWordWrap",            &CSimpleEditBox_SetIndentedWordWrap },
    { "GetIndentedWordWrap",            &CSimpleEditBox_GetIndentedWordWrap },
    { "SetAutoFocus",                   &CSimpleEditBox_SetAutoFocus },
    { "IsAutoFocus",                    &CSimpleEditBox_IsAutoFocus },
    { "SetCountInvisibleLetters",       &CSimpleEditBox_SetCountInvisibleLetters },
    { "IsCountInvisibleLetters",        &CSimpleEditBox_IsCountInvisibleLetters },
    { "SetMultiLine",                   &CSimpleEditBox_SetMultiLine },
    { "IsMultiLine",                    &CSimpleEditBox_IsMultiLine },
    { "SetNumeric",                     &CSimpleEditBox_SetNumeric },
    { "IsNumeric",                      &CSimpleEditBox_IsNumeric },
    { "SetPassword",                    &CSimpleEditBox_SetPassword },
    { "IsPassword",                     &CSimpleEditBox_IsPassword },
    { "SetBlinkSpeed",                  &CSimpleEditBox_SetBlinkSpeed },
    { "GetBlinkSpeed",                  &CSimpleEditBox_GetBlinkSpeed },
    { "Insert",                         &CSimpleEditBox_Insert },
    { "SetText",                        &CSimpleEditBox_SetText },
    { "GetText",                        &CSimpleEditBox_GetText },
    { "SetNumber",                      &CSimpleEditBox_SetNumber },
    { "GetNumber",                      &CSimpleEditBox_GetNumber },
    { "HighlightText",                  &CSimpleEditBox_HighlightText },
    { "AddHistoryLine",                 &CSimpleEditBox_AddHistoryLine },
    { "ClearHistory",                   &CSimpleEditBox_ClearHistory },
    { "SetTextInsets",                  &CSimpleEditBox_SetTextInsets },
    { "GetTextInsets",                  &CSimpleEditBox_GetTextInsets },
    { "SetFocus",                       &CSimpleEditBox_SetFocus },
    { "ClearFocus",                     &CSimpleEditBox_ClearFocus },
    { "HasFocus",                       &CSimpleEditBox_HasFocus },
    { "SetMaxBytes",                    &CSimpleEditBox_SetMaxBytes },
    { "GetMaxBytes",                    &CSimpleEditBox_GetMaxBytes },
    { "SetMaxLetters",                  &CSimpleEditBox_SetMaxLetters },
    { "GetMaxLetters",                  &CSimpleEditBox_GetMaxLetters },
    { "GetNumLetters",                  &CSimpleEditBox_GetNumLetters },
    { "GetHistoryLines",                &CSimpleEditBox_GetHistoryLines },
    { "SetHistoryLines",                &CSimpleEditBox_SetHistoryLines },
    { "GetInputLanguage",               &CSimpleEditBox_GetInputLanguage },
    { "ToggleInputLanguage",            &CSimpleEditBox_ToggleInputLanguage },
    { "SetAltArrowKeyMode",             &CSimpleEditBox_SetAltArrowKeyMode },
    { "GetAltArrowKeyMode",             &CSimpleEditBox_GetAltArrowKeyMode },
    { "IsInIMECompositionMode",         &CSimpleEditBox_IsInIMECompositionMode },
    { "SetCursorPosition",              &CSimpleEditBox_SetCursorPosition },
    { "GetCursorPosition",              &CSimpleEditBox_GetCursorPosition },
    { "GetUTF8CursorPosition",          &CSimpleEditBox_GetUTF8CursorPosition }
};
