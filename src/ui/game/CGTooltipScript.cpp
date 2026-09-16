#include "ui/game/CGTooltipScript.hpp"
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
// The template declares 8 of each. The reference creates more when a tooltip needs them; until that
// is ported, asking for line 9 returns null and the line is dropped rather than overwriting line 8,
// which would silently corrupt a long tooltip instead of merely truncating it.
const int32_t TOOLTIP_MAX_LINES = 8;

CSimpleFontString* TooltipLine(CGTooltip* tooltip, int32_t line, bool right) {
    if (line < 1 || line > TOOLTIP_MAX_LINES) {
        return nullptr;
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

int32_t CGTooltip_AddFontStrings(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
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
    auto tooltip = TooltipThis(L);

    for (int32_t line = 1; line <= TOOLTIP_MAX_LINES; line++) {
        TooltipSetLine(tooltip, line, false, nullptr);
        TooltipSetLine(tooltip, line, true, nullptr);
    }

    tooltip->m_lineCount = 0;
    tooltip->m_unitGUID = 0;

    // FrameXML hangs the money-line and decoration resets off this.
    tooltip->RunOnTooltipClearedScript();

    return 0;
}

int32_t CGTooltip_AddLine(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        return 0;
    }

    if (tooltip->m_lineCount >= TOOLTIP_MAX_LINES) {
        return 0;
    }

    tooltip->m_lineCount++;
    TooltipSetLine(tooltip, tooltip->m_lineCount, false, lua_tostring(L, 2));

    return 0;
}

int32_t CGTooltip_AddDoubleLine(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2) || !lua_isstring(L, 3)) {
        return 0;
    }

    if (tooltip->m_lineCount >= TOOLTIP_MAX_LINES) {
        return 0;
    }

    tooltip->m_lineCount++;
    TooltipSetLine(tooltip, tooltip->m_lineCount, false, lua_tostring(L, 2));
    TooltipSetLine(tooltip, tooltip->m_lineCount, true, lua_tostring(L, 3));

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

    return 0;
}

int32_t CGTooltip_AppendText(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_FadeOut(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetHyperlink(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetAction(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
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

int32_t CGTooltip_SetSpell(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetSpellByID(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
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

int32_t CGTooltip_SetUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
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

    // SetUnit is still a stub, so m_unitGUID is never set and this can only answer false. Said
    // plainly here rather than left looking like a working comparison.
    (void)tooltip;

    lua_pushboolean(L, 0);

    return 1;
}

int32_t CGTooltip_GetUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_GetItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_GetSpell(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
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
