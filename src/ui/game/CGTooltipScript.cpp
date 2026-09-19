#include "ui/game/CGTooltipScript.hpp"
#include "ui/game/CGActionBar.hpp"
#include "object/client/SpellBook.hpp"
#include "db/Db.hpp"
#include "glue/CCharacterSelection.hpp"
#include "glue/CharacterSelectionDisplay.hpp"
#include "gx/Coordinate.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/NameCache.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "ui/game/CGTooltip.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include <storm/String.hpp>
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"

namespace {

// The tooltip's line font strings are declared in GameTooltipTemplate.xml as children named
// <tooltip>TextLeft<n> and <tooltip>TextRight<n>, so a line is looked up by name.
//
// The template declares 8 of each. The reference creates more when a tooltip needs them; that is
// not ported, so a line past the eighth exists only if AddFontStrings was handed a pair for it.
// Asking for one that is not there returns null and the line is dropped rather than overwriting
// line 8, which would silently corrupt a long tooltip instead of merely truncating it.
const int32_t TOOLTIP_MAX_LINES = 8;

// Lines the template declares, plus whatever pairs AddFontStrings has been handed.
int32_t TooltipMaxLines(CGTooltip* tooltip) {
    return TOOLTIP_MAX_LINES + static_cast<int32_t>(tooltip->m_extraLines.Count());
}

CSimpleFontString* TooltipLine(CGTooltip* tooltip, int32_t line, bool right) {
    if (line < 1) {
        return nullptr;
    }

    // Past the template's lines the font strings are the ones AddFontStrings registered, in the
    // order they arrived.
    if (line > TOOLTIP_MAX_LINES) {
        uint32_t extra = static_cast<uint32_t>(line - TOOLTIP_MAX_LINES - 1);

        if (extra >= tooltip->m_extraLines.Count()) {
            return nullptr;
        }

        auto& pair = tooltip->m_extraLines[extra];

        return right ? pair.right : pair.left;
    }

    const char* name = tooltip->GetName();

    if (!name) {
        return nullptr;
    }

    char path[260];
    SStrPrintf(path, sizeof(path), "%sText%s%d", name, right ? "Right" : "Left", line);

    auto found = tooltip->GetLayoutFrameByName(path);

    // CSimpleTop is the other CLayoutFrame subclass and is not a CScriptRegion, so casting one would
    // adjust the pointer past a base it does not have -- the same trap as CScriptRegion_GetPoint.
    if (!found || found == static_cast<CLayoutFrame*>(CSimpleTop::s_instance)) {
        return nullptr;
    }

    auto region = static_cast<CScriptRegion*>(found);

    // And the name could in principle belong to something that is not a font string.
    if (!region->IsA(CSimpleFontString::GetObjectType())) {
        return nullptr;
    }

    return static_cast<CSimpleFontString*>(region);
}

void TooltipSetLine(CGTooltip* tooltip, int32_t line, bool right, const char* text) {
    auto fontString = TooltipLine(tooltip, line, right);

    if (!fontString) {
        return;
    }

    fontString->SetText(text ? text : "", 0);

    if (text && *text) {
        fontString->Show();
    } else {
        fontString->Hide();
    }
}

CGTooltip* TooltipThis(lua_State* L) {
    auto type = CGTooltip::GetObjectType();

    return static_cast<CGTooltip*>(FrameScript_GetObjectThis(L, type));
}

// The template's line geometry, taken from GameTooltipTemplate.xml rather than guessed: TextLeft1
// sits 10 in and 10 down from the tooltip's TOPLEFT, and every line after it hangs 2 below the one
// above.
const float TOOLTIP_INSET = 10.0f;
const float TOOLTIP_LINE_GAP = 2.0f;

// Space kept between a line's left and right text. The reference's own resize pass has not been
// decompiled, so unlike the two above this is Frozen's own number, not the reference's.
const float TOOLTIP_COLUMN_GAP = 12.0f;

// Lua hands out lengths in UI units; frame widths and heights are layout units. Both of
// CScriptRegion's size bindings convert with exactly this expression.
float TooltipUIToLayout(float ui) {
    return NDCToDDCWidth(ui / (CoordinateGetAspectCompensation() * 1024.0f));
}

// The name a unit tooltip is headed with. Resolved the way Script_UnitName does it -- the glue's
// selected character first while it is still around, because it is authoritative and immediate, and
// the GUID-keyed name cache otherwise.
const char* TooltipUnitName(CGUnit_C* unit) {
    if (unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
        auto selected = CCharacterSelection::GetSelectedCharacter();

        if (selected && selected->m_info.name[0]) {
            return selected->m_info.name;
        }
    }

    auto cached = NameCacheGetName(unit);

    return cached ? cached : "Unknown";
}

// Size the tooltip to the lines it is carrying.
//
// The reference does this in its own layout pass, which has not been decompiled; what is ported
// here is only the template's geometry (above). Without it the frame keeps the zero size the
// template gives it, which leaves the backdrop invisible and the text floating over the world.
// KNOWN DEFECT, two-column lines overlap. The width below reserves room for a right-hand column,
// but nothing positions that column: GameTooltipTemplate.xml anchors $parentTextRightN by its RIGHT
// to $parentTextLeftN's LEFT with x=40, and no FrameXML file touches it afterwards, so the
// reference must re-anchor it from C++ in a layout pass that is not decompiled here. Until that
// pass is recovered, any line with right-hand text -- the spell rank from TooltipSetSpellRec, and
// every AddDoubleLine -- draws on top of its own left text. The width term is kept because it is
// what a correct two-column layout needs; inventing an anchor to match it would be guessing the
// layout from how the tooltip ought to look, which is how this codebase acquired its graphics bugs.
void TooltipResizeToFit(CGTooltip* tooltip) {
    if (tooltip->m_lineCount < 1) {
        return;
    }

    float textWidth = 0.0f;
    float textHeight = 0.0f;

    for (int32_t line = 1; line <= tooltip->m_lineCount; line++) {
        auto left = TooltipLine(tooltip, line, false);
        auto right = TooltipLine(tooltip, line, true);

        const char* leftText = left ? left->GetText() : nullptr;
        const char* rightText = right ? right->GetText() : nullptr;

        float lineWidth = 0.0f;
        float lineHeight = 0.0f;

        if (leftText && *leftText) {
            lineWidth += left->GetStringWidth();
            lineHeight = left->GetStringHeight();
        }

        if (rightText && *rightText) {
            if (lineWidth > 0.0f) {
                lineWidth += TooltipUIToLayout(TOOLTIP_COLUMN_GAP);
            }

            lineWidth += right->GetStringWidth();

            float rightHeight = right->GetStringHeight();
            lineHeight = lineHeight > rightHeight ? lineHeight : rightHeight;
        }

        if (lineWidth > textWidth) {
            textWidth = lineWidth;
        }

        if (line > 1) {
            textHeight += TooltipUIToLayout(TOOLTIP_LINE_GAP);
        }

        textHeight += lineHeight;
    }

    float inset = TooltipUIToLayout(TOOLTIP_INSET);
    float width = textWidth + inset + inset;
    float minimumWidth = TooltipUIToLayout(tooltip->m_minimumWidth);

    if (width < minimumWidth) {
        width = minimumWidth;
    }

    // The padding FrameXML sets aside at the bottom for the money frames it parents to the tooltip.
    float height = textHeight + inset + inset + TooltipUIToLayout(tooltip->m_padding);

    tooltip->SetSize(width, height);
}

// What the reference's tooltip fillers end with: FUN_006205c0 (SetTotem) clears, adds its two lines
// and tail-calls FUN_0048f660. That is why FrameXML never calls GameTooltip:Show() after
// GameTooltip:SetAction or :SetUnit -- the C++ side shows the tooltip itself.
void TooltipShow(CGTooltip* tooltip) {
    TooltipResizeToFit(tooltip);
    tooltip->Show();
}

// Empty every line and drop what the tooltip was filled from. The reference's fillers all start
// here (FUN_0061c620).
void TooltipClear(CGTooltip* tooltip) {
    for (int32_t line = 1; line <= TooltipMaxLines(tooltip); line++) {
        TooltipSetLine(tooltip, line, false, nullptr);
        TooltipSetLine(tooltip, line, true, nullptr);
    }

    tooltip->m_lineCount = 0;
    tooltip->m_unitGUID = 0;
    tooltip->m_spellID = 0;

    // FrameXML hangs the money-line and decoration resets off this.
    tooltip->RunOnTooltipClearedScript();
}

// Fill the tooltip for a spell: its name on the first line, rank on the right of it. Costs, cast
// time, range and the description are Spell.dbc columns not read yet, so the tooltip stops there.
int32_t TooltipSetSpellRec(lua_State* L, CGTooltip* tooltip, const SpellRec* spell) {
    if (!spell || !spell->m_name || !*spell->m_name) {
        return 0;
    }

    TooltipClear(tooltip);

    tooltip->m_lineCount = 1;
    tooltip->m_spellID = spell->m_ID;

    if (spell->m_rank && *spell->m_rank) {
        TooltipSetLine(tooltip, 1, false, spell->m_name);
        TooltipSetLine(tooltip, 1, true, spell->m_rank);
    } else {
        TooltipSetLine(tooltip, 1, false, spell->m_name);
    }

    TooltipShow(tooltip);

    return 0;
}

// ref: FUN_006201f0
int32_t CGTooltip_AddFontStrings(lua_State* L) {
    auto tooltip = TooltipThis(L);

    CSimpleFontString* strings[2] = { nullptr, nullptr };

    for (int32_t index = 0; index < 2; index++) {
        if (lua_type(L, index + 2) != LUA_TTABLE) {
            return luaL_error(L, "Usage: %s:AddFontStrings(leftstring, rightstring)",
                              tooltip->GetDisplayName());
        }

        lua_rawgeti(L, index + 2, 0);
        auto region = static_cast<CScriptRegion*>(lua_touserdata(L, -1));
        lua_settop(L, -2);

        if (!region) {
            return luaL_error(L, "%s:AddFontStrings(): Couldn't find 'this' in fontstring",
                              tooltip->GetDisplayName());
        }

        if (!region->IsA(CSimpleFontString::GetObjectType())) {
            return luaL_error(L, "%s:AddFontStrings(): Wrong object type, expected fontstring",
                              tooltip->GetDisplayName());
        }

        strings[index] = static_cast<CSimpleFontString*>(region);
    }

    tooltip->AddFontStrings(strings[0], strings[1]);

    return 0;
}

int32_t CGTooltip_SetMinimumWidth(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (lua_isnumber(L, 2)) {
        tooltip->m_minimumWidth = static_cast<float>(lua_tonumber(L, 2));
    }

    return 0;
}

int32_t CGTooltip_GetMinimumWidth(lua_State* L) {
    lua_pushnumber(L, TooltipThis(L)->m_minimumWidth);

    return 1;
}

int32_t CGTooltip_SetPadding(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (lua_isnumber(L, 2)) {
        tooltip->m_padding = static_cast<float>(lua_tonumber(L, 2));
    }

    return 0;
}

int32_t CGTooltip_GetPadding(lua_State* L) {
    lua_pushnumber(L, TooltipThis(L)->m_padding);

    return 1;
}

int32_t CGTooltip_IsOwned(lua_State* L) {
    auto type = CGTooltip::GetObjectType();
    auto tooltip = static_cast<CGTooltip*>(FrameScript_GetObjectThis(L, type));

    if (lua_type(L, 2) != LUA_TTABLE) {
        luaL_error(L, "Usage: %s:IsOwned(frame)", tooltip->GetDisplayName());
        return 0;
    }

    lua_rawgeti(L, 2, 0);
    auto frame = static_cast<CSimpleFrame*>(lua_touserdata(L, -1));
    lua_settop(L, -2);

    if (!frame) {
        luaL_error(L, "%s:IsOwned(): Couldn't find 'this' in frame object", tooltip->GetDisplayName());
        return 0;
    }

    if (!frame->IsA(CSimpleFrame::GetObjectType())) {
        luaL_error(L, "%s:IsOwned(): Wrong object type, expected frame", tooltip->GetDisplayName());
        return 0;
    }

    if (tooltip->m_owner == frame) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CGTooltip_GetOwner(lua_State* L) {
    auto type = CGTooltip::GetObjectType();
    auto tooltip = static_cast<CGTooltip*>(FrameScript_GetObjectThis(L, type));

    auto owner = tooltip->m_owner;

    if (!owner) {
        lua_pushnil(L);

        return 1;
    }

    if (!owner->lua_registered) {
        owner->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, owner->lua_objectRef);

    return 1;
}

int32_t CGTooltip_SetOwner(lua_State* L) {
    auto tooltip = TooltipThis(L);

    // Every OnEnter handler calls this first; while it was a stub, IsOwned answered false for every
    // frame and no tooltip could ever be shown.
    if (lua_type(L, 2) != LUA_TTABLE) {
        return luaL_error(L, "Usage: %s:SetOwner(frame [, anchorType, xOffset, yOffset])",
                          tooltip->GetDisplayName());
    }

    lua_rawgeti(L, 2, 0);
    auto owner = static_cast<CSimpleFrame*>(lua_touserdata(L, -1));
    lua_settop(L, -2);

    tooltip->m_owner = owner;
    tooltip->m_anchorPoint = TOOLTIP_ANCHOR_TOPLEFT;

    TOOLTIP_ANCHORPOINT anchor;

    if (lua_isstring(L, 3) && StringToTooltipAnchor(lua_tostring(L, 3), anchor)) {
        tooltip->m_anchorPoint = anchor;
    }

    tooltip->m_offset.x = lua_isnumber(L, 4) ? static_cast<float>(lua_tonumber(L, 4)) : 0.0f;
    tooltip->m_offset.y = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 0.0f;

    return 0;
}

int32_t CGTooltip_GetAnchorType(lua_State* L) {
    auto tooltip = TooltipThis(L);
    const char* name = TooltipAnchorToString(tooltip->m_anchorPoint);

    if (name) {
        lua_pushstring(L, name);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CGTooltip_SetAnchorType(lua_State* L) {
    auto tooltip = TooltipThis(L);
    TOOLTIP_ANCHORPOINT anchor;

    if (lua_isstring(L, 2) && StringToTooltipAnchor(lua_tostring(L, 2), anchor)) {
        tooltip->m_anchorPoint = anchor;
    }

    if (lua_isnumber(L, 3)) {
        tooltip->m_offset.x = static_cast<float>(lua_tonumber(L, 3));
    }

    if (lua_isnumber(L, 4)) {
        tooltip->m_offset.y = static_cast<float>(lua_tonumber(L, 4));
    }

    return 0;
}

int32_t CGTooltip_ClearLines(lua_State* L) {
    TooltipClear(TooltipThis(L));

    return 0;
}

int32_t CGTooltip_AddLine(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        return 0;
    }

    if (tooltip->m_lineCount >= TooltipMaxLines(tooltip)) {
        return 0;
    }

    tooltip->m_lineCount++;
    TooltipSetLine(tooltip, tooltip->m_lineCount, false, lua_tostring(L, 2));
    TooltipResizeToFit(tooltip);

    return 0;
}

int32_t CGTooltip_AddDoubleLine(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2) || !lua_isstring(L, 3)) {
        return 0;
    }

    if (tooltip->m_lineCount >= TooltipMaxLines(tooltip)) {
        return 0;
    }

    tooltip->m_lineCount++;
    TooltipSetLine(tooltip, tooltip->m_lineCount, false, lua_tostring(L, 2));
    TooltipSetLine(tooltip, tooltip->m_lineCount, true, lua_tostring(L, 3));
    TooltipResizeToFit(tooltip);

    return 0;
}

int32_t CGTooltip_AddTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetText(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        return 0;
    }

    // SetText replaces the tooltip with a single line rather than appending one.
    TooltipSetLine(tooltip, 1, false, lua_tostring(L, 2));
    tooltip->m_lineCount = tooltip->m_lineCount > 1 ? tooltip->m_lineCount : 1;
    TooltipResizeToFit(tooltip);

    return 0;
}

// ref: FUN_0061ee90
int32_t CGTooltip_AppendText(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:AppendText(\"text\")", tooltip->GetDisplayName());
    }

    // The reference hands the text to CGTooltip::AppendText (FUN_0061eab0), which puts it after
    // what the last line already carries -- MainMenuBarBagButtons.lua appends the key binding to
    // the bag name that way.
    auto fontString = TooltipLine(tooltip, tooltip->m_lineCount, false);

    if (!fontString) {
        return 0;
    }

    const char* existing = fontString->GetText();

    char text[1024];
    SStrPrintf(text, sizeof(text), "%s%s", existing ? existing : "", lua_tostring(L, 2));

    fontString->SetText(text, 0);
    fontString->Show();

    TooltipResizeToFit(tooltip);

    return 0;
}

// ref: FUN_0061d940
int32_t CGTooltip_FadeOut(lua_State* L) {
    auto tooltip = TooltipThis(L);

    // Deliberate divergence. The reference hides at once only for two of its own states and
    // otherwise starts a timed fade -- it stores a flag and the current time, and its update pass
    // takes the tooltip down later. Neither that pass nor the state it tests has been decompiled,
    // so this ends where the reference ends, hidden, rather than inventing a duration. It cannot be
    // left as a no-op: UnitFrame_OnLeave is the one FrameXML caller, so a tooltip that never hid
    // would sit over the world for the rest of the session.
    tooltip->Hide();

    return 0;
}

// ref: FUN_0062dae0
int32_t CGTooltip_SetHyperlink(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetHyperlink(link)", tooltip->GetDisplayName());
    }

    const char* link = lua_tostring(L, 2);

    // The reference tests the link types in this order, each by substring, and hands the id it
    // parses out to that type's tooltip filler. Only the two spell-backed types can be filled here;
    // the rest are named with the filler they would reach so they are not mistaken for missing
    // cases, and they return quietly rather than falling through to the unknown-type error, which
    // would put a Lua error on screen for a chat link the reference handles.
    if (SStrStr(link, "item:")) {
        // FUN_006277f0. Needs the item system.
        return 0;
    }

    const char* found = SStrStr(link, "enchant:");

    if (found) {
        // The reference gates this one on a flag in the spell record (a byte at +0x10 of the record
        // it copies, tested against 0x20) that has not been identified, so the spell is shown
        // whenever the record exists.
        return TooltipSetSpellRec(L, tooltip, g_spellDB.GetRecord(SStrToInt(found + 8)));
    }

    if (SStrStr(link, "dance:")) {
        // FUN_00575d70. Needs the dance/emote browser.
        return 0;
    }

    found = SStrStr(link, "spell:");

    if (found) {
        return TooltipSetSpellRec(L, tooltip, g_spellDB.GetRecord(SStrToInt(found + 6)));
    }

    if (SStrStr(link, "unit:")) {
        // FUN_00621070, the same unit filler SetUnit reaches. It takes the GUID the link carries,
        // and Frozen's partial unit fill works from a unit token instead, so this is left alone.
        return 0;
    }

    if (SStrStr(link, "quest:")) {
        // FUN_00622960. Needs the quest log.
        return 0;
    }

    if (SStrStr(link, "talent:")) {
        // FUN_00626e20. Needs the talent system.
        return 0;
    }

    if (SStrStr(link, "trade:")) {
        // FUN_005de300. Needs the trade skill system.
        return 0;
    }

    if (SStrStr(link, "achievement:")) {
        // FUN_00627220. Needs the achievement system.
        return 0;
    }

    if (SStrStr(link, "glyph:")) {
        // FUN_00622ba0. Needs the glyph system.
        return 0;
    }

    return luaL_error(L, "%s:SetHyperlink(): Unknown link type", tooltip->GetDisplayName());
}

// SetAction(slot): the tooltip for an action button. Only spell actions resolve today.
int32_t CGTooltip_SetAction(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isnumber(L, 2)) {
        return 0;
    }

    int32_t slot = static_cast<int32_t>(lua_tonumber(L, 2)) - 1;
    uint32_t type = CGActionBar::GetActionType(slot);
    uint32_t id = CGActionBar::GetActionID(slot);

    if (!id || (type != CGActionBar::ACTION_BUTTON_SPELL && type != CGActionBar::ACTION_BUTTON_C)) {
        return 0;
    }

    return TooltipSetSpellRec(L, tooltip, g_spellDB.GetRecord(static_cast<int32_t>(id)));
}

int32_t CGTooltip_SetPetAction(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetShapeshift(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetPossession(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetTracking(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// SetSpell(slot, bookType): a spellbook entry.
int32_t CGTooltip_SetSpell(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isnumber(L, 2)) {
        return 0;
    }

    uint32_t id = SpellBookSpellAt(static_cast<int32_t>(lua_tonumber(L, 2)) - 1);

    return TooltipSetSpellRec(L, tooltip, id ? g_spellDB.GetRecord(static_cast<int32_t>(id)) : nullptr);
}

int32_t CGTooltip_SetSpellByID(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isnumber(L, 2)) {
        return 0;
    }

    return TooltipSetSpellRec(L, tooltip, g_spellDB.GetRecord(static_cast<int32_t>(lua_tonumber(L, 2))));
}

int32_t CGTooltip_SetGlyph(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetInventoryItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetLootItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetQuestItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetQuestLogItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetTrainerService(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetTradeSkillItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetMerchantItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetMerchantCostItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetTradePlayerItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetTradeTargetItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetBagItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00625e10
int32_t CGTooltip_SetUnit(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetUnit(\"unit\"[, hideStatus])", tooltip->GetDisplayName());
    }

    const char* token = lua_tostring(L, 2);

    WOWGUID guid = 0;
    Script_GetGUIDFromToken(token, guid, false);

    auto unit = guid ? Script_GetUnitFromName(token) : nullptr;

    if (!unit) {
        lua_pushnil(L);

        return 1;
    }

    // Partial port. The reference passes the GUID to CGTooltip::SetUnit (FUN_00621070), which fills
    // the name, the level and class line, the faction and guild lines and the status bars; that
    // function has not been decompiled, so only the name line is filled here. hideStatus (argument
    // 3) is read by the reference to suppress the health bar, which Frozen's tooltip does not have,
    // so nothing is done with it yet.
    TooltipClear(tooltip);

    tooltip->m_unitGUID = guid;
    tooltip->m_lineCount = 1;
    TooltipSetLine(tooltip, 1, false, TooltipUnitName(unit));

    // UnitFrame_UpdateTooltip colours TextLeft1 by reaction right after this, and FrameXML's
    // OnTooltipSetUnit handlers add their own lines, so the layout is taken afterwards.
    tooltip->RunOnTooltipSetUnitScript();
    TooltipShow(tooltip);

    lua_pushnumber(L, 1.0);

    return 1;
}

int32_t CGTooltip_SetUnitBuff(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetUnitDebuff(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetUnitAura(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetTalent(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetSendMailItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetInboxItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetAuctionSellItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetAuctionItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_NumLines(lua_State* L) {
    lua_pushnumber(L, TooltipThis(L)->m_lineCount);

    return 1;
}

int32_t CGTooltip_SetQuestRewardSpell(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetQuestLogRewardSpell(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetHyperlinkCompareItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetBuybackItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetLootRollItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetSocketedItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetSocketGem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetExistingSocketGem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetGuildBankItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_IsUnit(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:IsUnit(\"unit\")", tooltip->GetDisplayName());
    }

    WOWGUID guid = 0;
    Script_GetGUIDFromToken(lua_tostring(L, 2), guid, false);

    lua_pushboolean(L, tooltip->m_unitGUID != 0 && guid == tooltip->m_unitGUID);

    return 1;
}

// ref: FUN_0061dad0
int32_t CGTooltip_GetUnit(lua_State* L) {
    auto tooltip = TooltipThis(L);

    // The reference turns the stored GUID back into a unit token (FUN_0060b0b0) and reads the name
    // from that token. The token table it walks is not ported, so the tokens a tooltip can be
    // opened from are tried in turn instead; a unit that is not one of them answers nil, where the
    // reference would still name it from the object manager.
    static const char* const s_tokens[] = { "player", "pet", "target", "focus", "mouseover" };

    const char* token = nullptr;
    CGUnit_C* unit = nullptr;

    if (tooltip->m_unitGUID) {
        for (int32_t i = 0; i < 5; i++) {
            WOWGUID guid = 0;

            if (Script_GetGUIDFromToken(s_tokens[i], guid, false) && guid == tooltip->m_unitGUID) {
                token = s_tokens[i];
                unit = Script_GetUnitFromName(s_tokens[i]);

                break;
            }
        }
    }

    if (!unit) {
        // The reference returns no values at all here, not two nils: a caller using select("#")
        // or forwarding varargs sees a different arity otherwise.
        return 0;
    }

    lua_pushstring(L, TooltipUnitName(unit));
    lua_pushstring(L, token);

    return 2;
}

int32_t CGTooltip_GetItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0061f0f0
int32_t CGTooltip_GetSpell(lua_State* L) {
    auto tooltip = TooltipThis(L);

    // The reference holds two spell slots and answers name/rank/id for each one whose record is
    // found, so up to six values; Frozen records only the spell the tooltip was filled from.
    auto spell = tooltip->m_spellID ? g_spellDB.GetRecord(tooltip->m_spellID) : nullptr;

    if (!spell) {
        return 0;
    }

    lua_pushstring(L, spell->m_name ? spell->m_name : "");
    lua_pushstring(L, spell->m_rank ? spell->m_rank : "");
    lua_pushnumber(L, tooltip->m_spellID);

    return 3;
}

int32_t CGTooltip_SetTotem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetCurrencyToken(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetBackpackToken(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_IsEquippedItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetQuestLogSpecialItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetEquipmentSet(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetFrameStack(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetLFGDungeonReward(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetLFGCompletionReward(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

}

FrameScript_Method CGTooltipMethods[] = {
    { "AddFontStrings",         &CGTooltip_AddFontStrings },
    { "SetMinimumWidth",        &CGTooltip_SetMinimumWidth },
    { "GetMinimumWidth",        &CGTooltip_GetMinimumWidth },
    { "SetPadding",             &CGTooltip_SetPadding },
    { "GetPadding",             &CGTooltip_GetPadding },
    { "IsOwned",                &CGTooltip_IsOwned },
    { "GetOwner",               &CGTooltip_GetOwner },
    { "SetOwner",               &CGTooltip_SetOwner },
    { "GetAnchorType",          &CGTooltip_GetAnchorType },
    { "SetAnchorType",          &CGTooltip_SetAnchorType },
    { "ClearLines",             &CGTooltip_ClearLines },
    { "AddLine",                &CGTooltip_AddLine },
    { "AddDoubleLine",          &CGTooltip_AddDoubleLine },
    { "AddTexture",             &CGTooltip_AddTexture },
    { "SetText",                &CGTooltip_SetText },
    { "AppendText",             &CGTooltip_AppendText },
    { "FadeOut",                &CGTooltip_FadeOut },
    { "SetHyperlink",           &CGTooltip_SetHyperlink },
    { "SetAction",              &CGTooltip_SetAction },
    { "SetPetAction",           &CGTooltip_SetPetAction },
    { "SetShapeshift",          &CGTooltip_SetShapeshift },
    { "SetPossession",          &CGTooltip_SetPossession },
    { "SetTracking",            &CGTooltip_SetTracking },
    { "SetSpell",               &CGTooltip_SetSpell },
    { "SetSpellByID",           &CGTooltip_SetSpellByID },
    { "SetGlyph",               &CGTooltip_SetGlyph },
    { "SetInventoryItem",       &CGTooltip_SetInventoryItem },
    { "SetLootItem",            &CGTooltip_SetLootItem },
    { "SetQuestItem",           &CGTooltip_SetQuestItem },
    { "SetQuestLogItem",        &CGTooltip_SetQuestLogItem },
    { "SetTrainerService",      &CGTooltip_SetTrainerService },
    { "SetTradeSkillItem",      &CGTooltip_SetTradeSkillItem },
    { "SetMerchantItem",        &CGTooltip_SetMerchantItem },
    { "SetMerchantCostItem",    &CGTooltip_SetMerchantCostItem },
    { "SetTradePlayerItem",     &CGTooltip_SetTradePlayerItem },
    { "SetTradeTargetItem",     &CGTooltip_SetTradeTargetItem },
    { "SetBagItem",             &CGTooltip_SetBagItem },
    { "SetUnit",                &CGTooltip_SetUnit },
    { "SetUnitBuff",            &CGTooltip_SetUnitBuff },
    { "SetUnitDebuff",          &CGTooltip_SetUnitDebuff },
    { "SetUnitAura",            &CGTooltip_SetUnitAura },
    { "SetTalent",              &CGTooltip_SetTalent },
    { "SetSendMailItem",        &CGTooltip_SetSendMailItem },
    { "SetInboxItem",           &CGTooltip_SetInboxItem },
    { "SetAuctionSellItem",     &CGTooltip_SetAuctionSellItem },
    { "SetAuctionItem",         &CGTooltip_SetAuctionItem },
    { "NumLines",               &CGTooltip_NumLines },
    { "SetQuestRewardSpell",    &CGTooltip_SetQuestRewardSpell },
    { "SetQuestLogRewardSpell", &CGTooltip_SetQuestLogRewardSpell },
    { "SetHyperlinkCompareItem", &CGTooltip_SetHyperlinkCompareItem },
    { "SetBuybackItem",         &CGTooltip_SetBuybackItem },
    { "SetLootRollItem",        &CGTooltip_SetLootRollItem },
    { "SetSocketedItem",        &CGTooltip_SetSocketedItem },
    { "SetSocketGem",           &CGTooltip_SetSocketGem },
    { "SetExistingSocketGem",   &CGTooltip_SetExistingSocketGem },
    { "SetGuildBankItem",       &CGTooltip_SetGuildBankItem },
    { "IsUnit",                 &CGTooltip_IsUnit },
    { "GetUnit",                &CGTooltip_GetUnit },
    { "GetItem",                &CGTooltip_GetItem },
    { "GetSpell",               &CGTooltip_GetSpell },
    { "SetTotem",               &CGTooltip_SetTotem },
    { "SetCurrencyToken",       &CGTooltip_SetCurrencyToken },
    { "SetBackpackToken",       &CGTooltip_SetBackpackToken },
    { "IsEquippedItem",         &CGTooltip_IsEquippedItem },
    { "SetQuestLogSpecialItem", &CGTooltip_SetQuestLogSpecialItem },
    { "SetEquipmentSet",        &CGTooltip_SetEquipmentSet },
    { "SetFrameStack",          &CGTooltip_SetFrameStack },
    { "SetLFGDungeonReward",    &CGTooltip_SetLFGDungeonReward },
    { "SetLFGCompletionReward", &CGTooltip_SetLFGCompletionReward },
};
