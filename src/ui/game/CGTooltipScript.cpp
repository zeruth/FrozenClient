#include "ui/game/CGTooltipScript.hpp"
#include "ui/game/CGActionBar.hpp"
#include "object/client/SpellBook.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "glue/CCharacterSelection.hpp"
#include "glue/CharacterSelectionDisplay.hpp"
#include "gx/Coordinate.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGBag_C.hpp"
#include "object/client/SpellHistory.hpp"
#include "ui/game/CharacterInfoScript.hpp"
#include <common/Time.hpp>
#include "object/client/DBCacheInstances.hpp"
#include "ui/game/ContainerFrameScript.hpp"
#include "ui/game/AuctionHouse.hpp"
#include "ui/game/ItemSocketInfo.hpp"
#include "ui/game/MerchantFrame.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "ui/game/CGTooltip.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include <storm/String.hpp>
#include "ui/FrameScript.hpp"
#include "object/client/AuraCache.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cmath>

#include "gx/Coordinate.hpp"
#include "ui/Util.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include <tempest/Rect.hpp>

namespace {

// The colour a line takes when the caller names none, NORMAL_FONT_COLOR (DAT_00ad2d2c).
const uint32_t TOOLTIP_DEFAULT_COLOR = 0xFFFFD200;

// The anchor names SetOwner and SetAnchorType take, in TOOLTIP_ANCHORPOINT order. A name not in
// the list -- or no name -- is ANCHOR_LEFT, which is what both bindings fall back to.
const char* const TOOLTIP_ANCHOR_NAMES[] = {
    "ANCHOR_LEFT",
    "ANCHOR_RIGHT",
    "ANCHOR_BOTTOMLEFT",
    "ANCHOR_BOTTOM",
    "ANCHOR_BOTTOMRIGHT",
    "ANCHOR_TOPLEFT",
    "ANCHOR_TOP",
    "ANCHOR_TOPRIGHT",
    "ANCHOR_CURSOR",
    "ANCHOR_NONE",
    "ANCHOR_PRESERVE",
    "ANCHOR_CURSOR_RIGHT",
};

TOOLTIP_ANCHORPOINT TooltipAnchorFromLua(lua_State* L, int32_t idx) {
    if (!lua_isstring(L, idx)) {
        return TOOLTIP_ANCHOR_LEFT;
    }

    auto name = lua_tostring(L, idx);

    for (int32_t i = 0; i < 12; i++) {
        if (!SStrCmpI(name, TOOLTIP_ANCHOR_NAMES[i], STORM_MAX_STR)) {
            return static_cast<TOOLTIP_ANCHORPOINT>(i);
        }
    }

    return TOOLTIP_ANCHOR_LEFT;
}

// A Lua length in UI units as a layout length.
float TooltipUIToDDC(float ui) {
    return NDCToDDCWidth(ui / (CoordinateGetAspectCompensation() * 1024.0f));
}

CImVector TooltipDefaultColor() {
    CImVector color;
    color.value = TOOLTIP_DEFAULT_COLOR;

    return color;
}

// The GlobalStrings key for a creature's classification, from the reference's table at 00ad2e6c.
// Six entries indexed by classification, and only two are set: elite and rare elite both read
// ELITE. Normal, world boss, rare and trivial add nothing -- a world boss is named by its boss
// flag below rather than by its rank.
const char* const CLASSIFICATION_KEYS[] = {
    nullptr,    // normal
    "ELITE",    // elite
    "ELITE",    // rare elite
    nullptr,    // world boss
    nullptr,    // rare
    nullptr,    // trivial
};

// ref: the level / race / class / type block of FUN_00621070 (CGTooltip::SetUnit)
//
// The reference fills three slots and then picks one of six GlobalStrings formats by which came
// out non-empty. The slot names come from the format keys and do NOT mean for a creature what
// they look like:
//
//   race    the race name, players only
//   class   the class name for a player, the CREATURE TYPE for anything else
//   type    "Player" for a player, the classification ("Elite" / "Boss") for anything else
//
// so a hostile elite reads "Level 12 Beast (Elite)" and a player "Level 80 Human Paladin
// (Player)". Written from the decompilation rather than from memory of the screen; if the wording
// looks wrong, check it against FUN_00621070 before "fixing" it here.
void TooltipUnitLevelLine(CGTooltip* tooltip, CGUnit_C* unit) {
    auto data = unit->Unit();

    if (!data) {
        return;
    }

    auto player = static_cast<CGUnit_C*>(
        ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, __FILE__, __LINE__));

    bool isPlayer = unit->IsA(TYPE_PLAYER);
    auto info = unit->m_creatureStats;

    // The boss flag is creature type flag bit 2, and it does two things at once: it replaces the
    // classification with "Boss" and it hides the level.
    bool isBoss = info && (info->m_typeFlags & 0x4) != 0;

    // A corpse takes over the class slot and drops the race.
    bool isCorpse = data->health < 1 || (data->dynamicFlags & 0x20) != 0;

    // "??" for a boss, for a level the client does not know, and for a hostile unit ten or more
    // levels above the player. That last one is the skull.
    char levelText[32];
    int32_t level = data->level;
    bool unknown = isBoss || level < 1;

    if (!unknown && player) {
        auto playerData = player->Unit();

        if (playerData && player->GetReaction(unit) < 2 && playerData->level <= level - 10) {
            unknown = true;
        }
    }

    if (unknown) {
        SStrCopy(levelText, "??", sizeof(levelText));
    } else {
        SStrPrintf(levelText, sizeof(levelText), "%d", level);
    }

    const char* raceText = nullptr;
    const char* classText = nullptr;

    if (isCorpse) {
        classText = FrameScript_GetText("CORPSE", -1, GENDER_NOT_APPLICABLE);
    } else if (isPlayer) {
        auto raceRec = g_chrRacesDB.GetRecord(data->bytes0 & 0xFF);
        auto classRec = g_chrClassesDB.GetRecord((data->bytes0 >> 8) & 0xFF);

        // Both or neither: the reference only fills the race slot when the class resolves too.
        if (raceRec && classRec) {
            raceText = raceRec->m_name;
            classText = classRec->m_name;
        }
    } else if (player) {
        // A creature names its type here, but only one the player could fight. Creature type 10 is
        // "Not specified" and never prints. The second gate is a creature type flag frozen cannot
        // name: the reference reads bit 26 through a one-line accessor (FUN_00715df0) used only
        // here, to suppress the type.
        bool suppressed = info && (info->m_typeFlags & 0x04000000) != 0;
        auto type = unit->GetCreatureType();

        if (type != 10 && !suppressed && unit->GetReaction(player) < 4) {
            auto typeRec = type ? g_creatureTypeDB.GetRecord(type) : nullptr;

            if (typeRec) {
                classText = typeRec->m_name;
            }
        }
    }

    const char* typeText = nullptr;

    if (isPlayer) {
        typeText = FrameScript_GetText("PLAYER", -1, GENDER_NOT_APPLICABLE);
    } else if (isBoss) {
        typeText = FrameScript_GetText("BOSS", -1, GENDER_NOT_APPLICABLE);
    } else {
        auto classification = unit->GetClassification();
        auto count = static_cast<int32_t>(sizeof(CLASSIFICATION_KEYS) / sizeof(CLASSIFICATION_KEYS[0]));

        if (classification >= 0 && classification < count && CLASSIFICATION_KEYS[classification]) {
            typeText = FrameScript_GetText(CLASSIFICATION_KEYS[classification], -1, GENDER_NOT_APPLICABLE);
        }
    }

    // Every one of these takes the level as %s, which is why it was printed into a buffer above
    // rather than passed as a number.
    bool hasRace = raceText && *raceText;
    bool hasClass = classText && *classText;
    bool hasType = typeText && *typeText;

    char text[1024];

    if (hasRace && hasClass && hasType) {
        SStrPrintf(text, sizeof(text), FrameScript_GetText("TOOLTIP_UNIT_LEVEL_RACE_CLASS_TYPE", -1, GENDER_NOT_APPLICABLE),
                   levelText, raceText, classText, typeText);
    } else if (hasRace && hasClass) {
        SStrPrintf(text, sizeof(text), FrameScript_GetText("TOOLTIP_UNIT_LEVEL_RACE_CLASS", -1, GENDER_NOT_APPLICABLE),
                   levelText, raceText, classText);
    } else if (hasClass && hasType) {
        SStrPrintf(text, sizeof(text), FrameScript_GetText("TOOLTIP_UNIT_LEVEL_CLASS_TYPE", -1, GENDER_NOT_APPLICABLE),
                   levelText, classText, typeText);
    } else if (hasClass) {
        SStrPrintf(text, sizeof(text), FrameScript_GetText("TOOLTIP_UNIT_LEVEL_CLASS", -1, GENDER_NOT_APPLICABLE),
                   levelText, classText);
    } else if (hasType) {
        SStrPrintf(text, sizeof(text), FrameScript_GetText("TOOLTIP_UNIT_LEVEL_TYPE", -1, GENDER_NOT_APPLICABLE),
                   levelText, typeText);
    } else {
        SStrPrintf(text, sizeof(text), FrameScript_GetText("TOOLTIP_UNIT_LEVEL", -1, GENDER_NOT_APPLICABLE),
                   levelText);
    }

    tooltip->AddLine(text, nullptr, 0);
}

CGTooltip* TooltipThis(lua_State* L) {
    auto type = CGTooltip::GetObjectType();

    return static_cast<CGTooltip*>(FrameScript_GetObjectThis(L, type));
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

    return unit->GetUnitName(nullptr, 1);
}

// Fill the tooltip for a spell through the spell builder with no cooldown, pet or talent. The
// setters that still call this pass the reference's own arguments once each is ported on its own.
int32_t TooltipSetSpellRec(lua_State* L, CGTooltip* tooltip, const SpellRec* spell) {
    if (!spell) {
        return 0;
    }

    tooltip->SetSpell(spell->m_ID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0);

    return 0;
}

// ref: FUN_006201f0
int32_t CGTooltip_AddFontStrings(lua_State* L) {
    auto tooltip = TooltipThis(L);

    CSimpleFontString* strings[2] = { nullptr, nullptr };

    for (int32_t index = 0; index < 2; index++) {
        if (lua_type(L, index + 2) != LUA_TTABLE) {
            luaL_error(L, "Usage: %s:AddFontStrings(leftstring, rightstring)", tooltip->GetDisplayName());
            continue;
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

// ref: FUN_0061d040
int32_t CGTooltip_SetMinimumWidth(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetMinimumWidth(width [,force])", tooltip->GetDisplayName());
        return 0;
    }

    auto force = StringToBOOL(L, 3, 0);

    tooltip->m_minimumWidth = static_cast<float>(lua_tonumber(L, 2));
    tooltip->m_minimumWidthForced = force;

    return 0;
}

// ref: FUN_0061d0d0
int32_t CGTooltip_GetMinimumWidth(lua_State* L) {
    auto tooltip = TooltipThis(L);

    lua_pushnumber(L, tooltip->m_minimumWidth);

    if (tooltip->m_minimumWidthForced) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 2;
}

// ref: FUN_0061d150
int32_t CGTooltip_SetPadding(lua_State* L) {
    auto tooltip = TooltipThis(L);

    tooltip->m_padding = TooltipUIToDDC(static_cast<float>(lua_tonumber(L, 2)));

    return 0;
}

// ref: FUN_0061d1c0
// The padding comes back in the layout units it is kept in, not converted back.
int32_t CGTooltip_GetPadding(lua_State* L) {
    lua_pushnumber(L, TooltipThis(L)->m_padding);

    return 1;
}

// ref: FUN_0061d210
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

// ref: FUN_0061d350
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

// ref: FUN_0061eb40
// The tooltip hides, then takes the owner, the anchor (ANCHOR_LEFT when none is named) and the
// offsets, which clears it.
int32_t CGTooltip_SetOwner(lua_State* L) {
    auto tooltip = TooltipThis(L);

    tooltip->Hide();

    if (lua_type(L, 2) != LUA_TTABLE) {
        luaL_error(L, "Usage: %s:SetOwner(frame)", tooltip->GetDisplayName());
        return 0;
    }

    lua_rawgeti(L, 2, 0);
    auto owner = static_cast<CSimpleFrame*>(lua_touserdata(L, -1));
    lua_settop(L, -2);

    if (!owner) {
        luaL_error(L, "%s:SetOwner(): Couldn't find 'this' in frame object", tooltip->GetDisplayName());
        return 0;
    }

    if (!owner->IsA(CSimpleFrame::GetObjectType())) {
        luaL_error(L, "%s:SetOwner(): Wrong object type, expected frame", tooltip->GetDisplayName());
        return 0;
    }

    if (static_cast<void*>(owner) == static_cast<void*>(tooltip)) {
        luaL_error(L, "%s:SetOwner(): Can't set owner to self", tooltip->GetDisplayName());
        return 0;
    }

    auto anchor = TooltipAnchorFromLua(L, 3);

    float x = 0.0f;

    if (lua_isnumber(L, 4)) {
        x = TooltipUIToDDC(static_cast<float>(lua_tonumber(L, 4)));
    }

    float y = 0.0f;

    if (lua_isnumber(L, 5)) {
        y = TooltipUIToDDC(static_cast<float>(lua_tonumber(L, 5)));
    }

    tooltip->SetOwner(owner, anchor, x, y);

    return 0;
}

// ref: FUN_0061d650
int32_t CGTooltip_GetAnchorType(lua_State* L) {
    auto tooltip = TooltipThis(L);
    auto anchor = static_cast<uint32_t>(tooltip->m_anchorType);

    lua_pushstring(L, anchor < 12 ? TOOLTIP_ANCHOR_NAMES[anchor] : "ANCHOR_NONE");

    return 1;
}

// ref: FUN_0061d3d0
// The offsets always change; the anchor only while the tooltip has an owner.
int32_t CGTooltip_SetAnchorType(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:SetAnchorType( anchorType [,Xoffset] [,Yoffset] )", tooltip->GetDisplayName());
        return 0;
    }

    auto anchor = TooltipAnchorFromLua(L, 2);

    float x = 0.0f;

    if (lua_isnumber(L, 3)) {
        x = TooltipUIToDDC(static_cast<float>(lua_tonumber(L, 3)));
    }

    float y = 0.0f;

    if (lua_isnumber(L, 4)) {
        y = TooltipUIToDDC(static_cast<float>(lua_tonumber(L, 4)));
    }

    tooltip->m_anchorOffset.x = x;
    tooltip->m_anchorOffset.y = y;

    if (tooltip->m_owner) {
        tooltip->m_anchorType = anchor;
    }

    tooltip->SetAnchor(1);

    return 0;
}

// ref: FUN_0061d7d0
int32_t CGTooltip_ClearLines(lua_State* L) {
    TooltipThis(L)->ClearTooltip();

    return 0;
}

// ref: FUN_00620340
// AddLine(text [, r, g, b [, wrap]]). The layout waits for the tooltip's Show.
int32_t CGTooltip_AddLine(lua_State* L) {
    auto tooltip = TooltipThis(L);

    const char* text = nullptr;
    auto color = TooltipDefaultColor();

    if (lua_isstring(L, 2)) {
        text = lua_tostring(L, 2);
    }

    if (lua_isnumber(L, 3)) {
        FrameScript_GetColorNoAlpha(L, 3, color);
    }

    auto wrap = StringToBOOL(L, 6, 0);

    tooltip->AddLine(text, nullptr, color, color, wrap);

    return 0;
}

// ref: FUN_006203f0
// AddDoubleLine(left, right [, lr, lg, lb [, rr, rg, rb [, wrap]]]).
int32_t CGTooltip_AddDoubleLine(lua_State* L) {
    auto tooltip = TooltipThis(L);

    const char* left = nullptr;
    const char* right = nullptr;
    auto leftColor = TooltipDefaultColor();
    auto rightColor = TooltipDefaultColor();

    if (lua_isstring(L, 2)) {
        left = lua_tostring(L, 2);
    }

    if (lua_isstring(L, 3)) {
        right = lua_tostring(L, 3);
    }

    if (lua_isnumber(L, 4)) {
        FrameScript_GetColorNoAlpha(L, 4, leftColor);
    }

    if (lua_isnumber(L, 7)) {
        FrameScript_GetColorNoAlpha(L, 7, rightColor);
    }

    auto wrap = StringToBOOL(L, 10, 0);

    tooltip->AddLine(left, right, leftColor, rightColor, wrap);

    return 0;
}

// ref: FUN_0061d810
// AddTexture(file [, minx, maxx, miny, maxy]): all four coordinates or none.
int32_t CGTooltip_AddTexture(lua_State* L) {
    auto tooltip = TooltipThis(L);

    const char* fileName = nullptr;

    if (lua_isstring(L, 2)) {
        fileName = lua_tostring(L, 2);
    }

    CImVector color;
    color.value = 0xFFFFFFFF;

    if (lua_isnumber(L, 3)) {
        if (!lua_isnumber(L, 4) || !lua_isnumber(L, 5) || !lua_isnumber(L, 6)) {
            luaL_error(L, "Usage: %s:AddTexture(\"filename\" [, minx, maxx, miny, maxy])", tooltip->GetDisplayName());
            return 0;
        }

        // ref: FUN_0061a150
        CRect texCoords;
        texCoords.minX = static_cast<float>(lua_tonumber(L, 3));
        texCoords.maxX = static_cast<float>(lua_tonumber(L, 4));
        texCoords.minY = static_cast<float>(lua_tonumber(L, 5));
        texCoords.maxY = static_cast<float>(lua_tonumber(L, 6));

        tooltip->AddTexture(fileName, texCoords, color);

        return 0;
    }

    CRect texCoords;
    texCoords.minY = 0.0f;
    texCoords.minX = 0.0f;
    texCoords.maxY = 1.0f;
    texCoords.maxX = 1.0f;

    tooltip->AddTexture(fileName, texCoords, color);

    return 0;
}

// ref: FUN_006204e0
// SetText(text [, r, g, b [, a [, wrap]]]): the tooltip is emptied, takes the one line and shows.
int32_t CGTooltip_SetText(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:SetText(\"text\" [, color])", tooltip->GetDisplayName());
        return 0;
    }

    auto text = lua_tostring(L, 2);
    auto color = TooltipDefaultColor();

    if (lua_isnumber(L, 3)) {
        FrameScript_GetColor(L, 3, color);
    }

    auto wrap = StringToBOOL(L, 7, 0);

    tooltip->ClearTooltip();
    tooltip->AddLine(text, nullptr, color, color, wrap);
    tooltip->Show();

    return 0;
}

// ref: FUN_0061ee90
int32_t CGTooltip_AppendText(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:AppendText(\"text\")", tooltip->GetDisplayName());
        return 0;
    }

    tooltip->AppendText(lua_tostring(L, 2));

    return 0;
}

// ref: FUN_0061d940
int32_t CGTooltip_FadeOut(lua_State* L) {
    TooltipThis(L)->FadeOut();

    return 0;
}

// ref: FUN_0062dae0
// SetHyperlink(link): by the link's type. A shown tooltip asked for the item, spell, unit, quest
// or achievement it already shows hides instead.
int32_t CGTooltip_SetHyperlink(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:SetHyperlink(link)", tooltip->GetDisplayName());

        return 0;
    }

    const char* link = lua_tostring(L, 2);

    if (auto found = SStrStr(link, "item:")) {
        auto itemID = SStrToInt(found + 5);

        if (!tooltip->m_shown || itemID != tooltip->m_itemID) {
            if (itemID < 1) {
                return 0;
            }

            tooltip->m_linkInfo.Reset();
            tooltip->m_linkInfo.Parse(link);

            WOWGUID requester = 0;
            WOWGUID none = 0;
            tooltip->SetItem(itemID, &requester, &none, 0, 0, 1, 0, 0, 0, nullptr, 1, &tooltip->m_linkInfo, 0, 0, 0);

            return 0;
        }

        tooltip->Hide();

        return 0;
    }

    if (auto found = SStrStr(link, "enchant:")) {
        auto spellID = SStrToInt(found + 8);

        if (!tooltip->m_shown || spellID != tooltip->m_spellID) {
            auto spell = g_spellDB.GetRecord(spellID);

            // Only a trade skill spell (SPELL_ATTR0 0x20) shows from an enchant link.
            if (spell && (spell->m_attributes & 0x20)) {
                tooltip->SetSpell(spellID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0);
            }

            return 0;
        }

        tooltip->Hide();

        return 0;
    }

    if (SStrStr(link, "dance:")) {
        // PARTIAL: FUN_00575d70, the dance studio's tooltip; the dance studio is not ported.
        return 0;
    }

    if (auto found = SStrStr(link, "spell:")) {
        auto spellID = SStrToInt(found + 6);

        if (!tooltip->m_shown || spellID != tooltip->m_spellID) {
            tooltip->SetSpell(spellID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0);

            return 0;
        }

        tooltip->Hide();

        return 0;
    }

    if (SStrStr(link, "unit:")) {
        // PARTIAL: FUN_00621070 through TooltipUnitLevelLine, the unit tooltip, which is ported with
        // SetUnit's own builder.
        return 0;
    }

    if (SStrStr(link, "quest:")) {
        // PARTIAL: FUN_00622960, the quest tooltip.
        return 0;
    }

    if (SStrStr(link, "talent:")) {
        // PARTIAL: FUN_00626e20, the talent tooltip.
        return 0;
    }

    if (SStrStr(link, "trade:")) {
        // PARTIAL: FUN_005de300, a trade skill link opens the profession window; not ported.
        return 0;
    }

    if (SStrStr(link, "achievement:")) {
        // PARTIAL: FUN_00627220, the achievement tooltip.
        return 0;
    }

    if (SStrStr(link, "glyph:")) {
        // PARTIAL: FUN_00622ba0, the glyph tooltip.
        return 0;
    }

    luaL_error(L, "%s:SetHyperlink(): Unknown link type", tooltip->GetDisplayName());

    return 0;
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

// TODO FUN_00625630. Reachable only once there are stance buttons: GetNumShapeshiftForms
// answers zero (MiscScript.cpp binds it to Script_ReturnZero), so FrameXML builds no stance
// bar and nothing ever calls this. Implementing it first would be inert, the same way the
// tooltip line-growth path was before AddLine was taught to ask for a new line.
int32_t CGTooltip_SetShapeshift(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetPossession(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00625940
// The minimap tracking button's tooltip. The reference picks between three answers: the name of
// the selected tracking type, the tooltip of a tracking *spell* when one is active instead, and
// otherwise the localized MINIMAP_TRACKING_TOOLTIP_NONE.
//
// Frozen keeps no minimap tracking state, so the third answer is always the right one -- not a
// placeholder for the other two but the same line the reference shows when nothing is tracked,
// which is the client's actual condition. When tracking arrives, the two branches above it go
// here, reading the selected record's name and the tracking spell respectively.
int32_t CGTooltip_SetTracking(lua_State* L) {
    auto tooltip = TooltipThis(L);

    tooltip->ClearTooltip();
    tooltip->AddLine(FrameScript_GetText("MINIMAP_TRACKING_TOOLTIP_NONE", -1, GENDER_NOT_APPLICABLE), nullptr, 0);
    tooltip->Show();

    return 0;
}

// ref: FUN_006259e0
// SetSpell(slot, bookType): a spellbook entry, the player's or ("pet") the pet's. Returns 1 when a
// line will change as time passes, otherwise nil.
// PARTIAL: the spellbook is frozen's stand-in (the reference reads its slot array through
// FUN_0053b4a0, and the pet's is not ported), and the cooldown left needs the spell cooldown
// tracking (FUN_00809000), which is not ported, so none is shown.
int32_t CGTooltip_SetSpell(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (lua_isnumber(L, 2) && lua_isstring(L, 3)) {
        auto slot = static_cast<int32_t>(lua_tonumber(L, 2));

        if (slot >= 0 && slot < 0x400) {
            auto pet = !SStrCmpI("pet", lua_tostring(L, 3), STORM_MAX_STR) ? 1 : 0;
            auto spellID = pet ? 0 : static_cast<int32_t>(SpellBookSpellAt(slot - 1));

            if (spellID > 0) {
                uint32_t cooldown = 0;

                if (tooltip->SetSpell(spellID, 0, cooldown, pet, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0)) {
                    lua_pushnumber(L, 1.0);

                    return 1;
                }
            }

            lua_pushnil(L);

            return 1;
        }
    }

    luaL_error(L, "Invalid spell slot in %s:SetSpell", tooltip->GetDisplayName());

    return 0;
}

// ref: FUN_00625b90
// SetSpellByID(id, isPet, showRank): a spell the player (or the pet) knows.
// PARTIAL: as SetSpell -- the spellbook stand-in decides what is known (FUN_0053b930), the pet's
// spells answer no, and no cooldown is shown.
int32_t CGTooltip_SetSpellByID(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (lua_isnumber(L, 2)) {
        auto spellID = static_cast<int32_t>(lua_tointeger(L, 2));

        if (spellID >= 0) {
            auto pet = StringToBOOL(L, 3, 0);
            auto showRank = StringToBOOL(L, 4, 0);

            if (!pet && SpellBookKnows(static_cast<uint32_t>(spellID))) {
                uint32_t cooldown = 0;

                if (tooltip->SetSpell(spellID, 0, cooldown, pet, showRank, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0)) {
                    lua_pushnumber(L, 1.0);

                    return 1;
                }
            }

            lua_pushnil(L);

            return 1;
        }
    }

    luaL_error(L, "Invalid spell ID in %s:SetSpellByID", tooltip->GetDisplayName());

    return 0;
}

int32_t CGTooltip_SetGlyph(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0062e050
// SetInventoryItem(unit, slot [, nameOnly]) -> hasItem, hasCooldown, repairCost. Slot -1 is the
// ammo. An item cooling down is shown from link info carrying the cooldown and its charges; an
// inspected player's gear with no item object, from the inspect record.
int32_t CGTooltip_SetInventoryItem(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:SetInventoryItem(unit, slot [, nameOnly])", tooltip->GetDisplayName());

        return 0;
    }

    if (!lua_isnumber(L, 3)) {
        luaL_error(L, "Invalid inventory slot in SetInventoryItem");

        return 0;
    }

    auto slot = static_cast<int32_t>(lua_tonumber(L, 3));

    if ((slot < 0 || slot > 0x16) && (slot < 0x27 || slot > 0x42) && (slot < 0x43 || slot > 0x49)
        && (slot < 0x56 || slot > 0x75) && (slot < 0x76 || slot > 0x95) && slot != -1) {
        luaL_error(L, "Invalid inventory slot in SetInventoryItem");

        return 0;
    }

    auto nameOnly = StringToBOOL(L, 4, 0);
    auto unit = Script_GetUnitFromName(lua_tostring(L, 2));

    if (IsActivePlayerOrInspectTarget(unit)) {
        WOWGUID unitGUID = unit->GetGUID();

        if (slot == -1) {
            auto player = static_cast<CGPlayer_C*>(unit);
            auto ammo = unit->GetGUID() == ClntObjMgrGetActivePlayer() ? static_cast<int32_t>(player->Player()->ammoID) : 0;

            if (ammo) {
                auto item = unit->GetBag()->FindItemByID(ammo, 0);

                if (item) {
                    WOWGUID itemGUID = item->GetGUID();
                    tooltip->SetItem(ammo, &unitGUID, &itemGUID, 0, 0, 0, 0, unitGUID, 0, nullptr, 0, nullptr, 0, 0, 0);
                    lua_pushnumber(L, 1.0);
                    lua_pushnil(L);

                    return 2;
                }
            }

            lua_pushnil(L);
            lua_pushnil(L);

            return 2;
        }

        auto item = unit->GetBag()->GetItem(static_cast<uint32_t>(slot));

        if (item) {
            WOWGUID itemGUID = item->GetGUID();
            int32_t useLinkInfo = 0;

            if (item->GetMaxCharges() == 0 || item->GetCharges() != 0) {
                int32_t duration = 0;
                int32_t start = 0;
                uint32_t enabled = 0;
                ItemGetCooldown(item, &duration, &start, &enabled);

                if (enabled && duration && start) {
                    tooltip->m_linkInfo.Reset();
                    tooltip->m_linkInfo.cooldown = duration + (start - static_cast<int32_t>(OsGetAsyncTimeMs()));
                    tooltip->m_linkInfo.charges = item->GetCharges();
                    useLinkInfo = 1;
                }
            }

            auto changing = tooltip->SetItem(item->GetEntryID(), &unitGUID, &itemGUID, nameOnly, 0, useLinkInfo, 0, unitGUID, 0, nullptr, 0, nullptr, 0, 0, 0);

            lua_pushnumber(L, 1.0);

            if (changing) {
                lua_pushnumber(L, 1.0);
            } else {
                lua_pushnil(L);
            }

            lua_pushnumber(L, static_cast<double>(static_cast<uint32_t>(MerchantGetRepairCost(item))));

            return 3;
        }

        auto info = static_cast<CGPlayer_C*>(unit)->GetInventoryItemInfo(static_cast<uint32_t>(slot));

        if (info && info->itemID) {
            WOWGUID none = 0;
            g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(info->itemID)), &none, &CGTooltip::OnItemArrived, tooltip, false);

            tooltip->m_linkInfo.Reset();
            tooltip->m_linkInfo.randomPropertyID = info->randomPropertyID;

            for (int32_t i = 0; i < 12; i++) {
                tooltip->m_linkInfo.enchant[i] = info->enchant[i];
                tooltip->m_linkInfo.enchantDuration[i] = 0;
                tooltip->m_linkInfo.enchantCharges[i] = 0;
            }

            tooltip->m_linkInfo.suffixFactor = info->suffixFactor;
            tooltip->m_linkInfo.creator = info->creator;

            tooltip->SetItem(info->itemID, &unitGUID, &none, nameOnly, 0, 1, 0, unitGUID, 0, nullptr, 0, nullptr, 0, 0, 0);

            lua_pushnumber(L, 1.0);
            lua_pushnil(L);
            lua_pushnumber(L, 0.0);

            return 3;
        }
    }

    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);

    return 3;
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

// ref: FUN_0062ed70
int32_t CGTooltip_SetMerchantItem(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Invalid merchant slot in SetMerchantItem");

        return 0;
    }

    auto index = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 2) - 1.0));
    auto item = MerchantGetItem(index);

    if (item && item->m_unk04) {
        WOWGUID merchant = MerchantGetGUID();
        WOWGUID none = 0;
        tooltip->SetItem(item->m_unk04, &merchant, &none, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr, 0, 0, 1);
    }

    return 0;
}

// ref: FUN_0062ee70
// SetMerchantCostItem(index, costIndex): the costIndex'th item the vendor's extended cost asks for.
int32_t CGTooltip_SetMerchantCostItem(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        luaL_error(L, "Invalid merchant slot in SetMerchantCostItem");

        return 0;
    }

    auto index = static_cast<int32_t>(lua_tonumber(L, 2) - 1.0);
    auto costIndex = static_cast<int32_t>(lua_tonumber(L, 3) - 1.0);
    auto item = MerchantGetItem(index);

    if (!item || !item->m_unk04) {
        return 0;
    }

    auto cost = g_itemExtendedCostDB.GetRecord(item->m_extendedCostID);

    if (!cost) {
        return 0;
    }

    int32_t seen = 0;

    for (int32_t i = 0; i < 5; i++) {
        if (cost->m_itemID[i] == 0) {
            continue;
        }

        if (seen == costIndex) {
            WOWGUID merchant = MerchantGetGUID();
            WOWGUID none = 0;
            tooltip->SetItem(cost->m_itemID[i], &merchant, &none, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr, 0, 0, 1);

            return 0;
        }

        seen++;
    }

    return 0;
}

int32_t CGTooltip_SetTradePlayerItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetTradeTargetItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0062f420
// SetBagItem(bag, slot) -> hasCooldown, repairCost. Bag 0 is the backpack, slots 1-based. The item
// asks its own record; a cooling down item is shown through link info carrying the cooldown.
int32_t CGTooltip_SetBagItem(lua_State* L) {
    auto tooltip = TooltipThis(L);
    auto player = CGPlayer_C::GetActivePtr();

    if (!player || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        return 0;
    }

    auto bagIndex = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 2)) - 1);
    CGBag_C* bag;

    if (bagIndex == 0xFFFFFFFF) {
        bag = &player->m_bag;
    } else {
        if (bagIndex > 10) {
            return 0;
        }

        auto container = ClntObjMgrObjectPtr(ContainerGetBagGuid(bagIndex), TYPE_CONTAINER, ".\\Tooltip.cpp", 0x1ebd);

        if (!container) {
            return 0;
        }

        bag = container->GetBag();
    }

    if (!bag) {
        return 0;
    }

    auto slot = static_cast<int32_t>(lua_tonumber(L, 3));
    auto index = slot - 1;

    if (bag == &player->m_bag) {
        index = slot + 0x16;
    }

    if (index < 0 || static_cast<uint32_t>(index) >= bag->m_numSlots) {
        lua_pushnil(L);

        return 1;
    }

    auto item = bag->GetItem(static_cast<uint32_t>(index));

    if (!item) {
        lua_pushnil(L);

        return 1;
    }

    WOWGUID itemGUID = item->GetGUID();
    int32_t useLinkInfo = 0;

    if (item->GetMaxCharges() == 0 || item->GetCharges() != 0) {
        int32_t duration = 0;
        int32_t start = 0;
        uint32_t enabled = 0;
        ItemGetCooldown(item, &duration, &start, &enabled);

        if (enabled && duration && start) {
            auto& link = tooltip->m_linkInfo;
            link.Reset();

            for (int32_t i = 0; i < 12; i++) {
                link.enchant[i] = 0;
                link.enchantDuration[i] = 0;
                link.enchantCharges[i] = 0;
            }

            link.cooldown = start + duration - static_cast<int32_t>(OsGetAsyncTimeMs());
            link.proposedEnchant = 0;
            link.creator = item->Item()->creator;
            link.wrapped = (item->Item()->flags >> 3) & 1;
            link.giftCreator = item->Item()->giftCreator;
            link.charges = item->GetCharges();
            useLinkInfo = 1;
        }
    }

    auto changing = tooltip->SetItem(item->GetEntryID(), &itemGUID, &itemGUID, 0, 0, useLinkInfo, 0, 0, 0, nullptr, 0, nullptr, 0, 0, 0);
    auto repair = MerchantGetRepairCost(item);

    if (changing) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    lua_pushnumber(L, static_cast<double>(static_cast<uint32_t>(repair)));

    return 2;
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

    // Partial port of CGTooltip::SetUnit (FUN_00621070). The name, the title and the level line
    // are filled here, in the reference's order. Still missing from that function: the guild line,
    // the faction line, the colourblind faction-standing line, the PvP and offline lines, and the
    // status bars. hideStatus (argument 3) suppresses the health bar, which frozen's tooltip does
    // not have, so nothing is done with it yet.
    tooltip->ClearTooltip();

    tooltip->m_unitGUID = guid;

    tooltip->AddLine(TooltipUnitName(unit), nullptr, 0);

    // The title sits between the name and the level line -- "Innkeeper" under "Amy Davenport".
    auto subName = unit->GetSubName();

    if (subName && *subName) {
        tooltip->AddLine(subName, nullptr, 0);
    }

    TooltipUnitLevelLine(tooltip, unit);

    // UnitFrame_UpdateTooltip colours TextLeft1 by reaction right after this, and FrameXML's
    // OnTooltipSetUnit handlers add their own lines, so the layout is taken afterwards.
    if (tooltip->m_onTooltipSetUnit.luaRef) {
        tooltip->RunScript(tooltip->m_onTooltipSetUnit, 0, nullptr);
    }

    tooltip->Show();

    lua_pushnumber(L, 1.0);

    return 1;
}

// Defined below, with the other aura tooltip code; declared here because SetUnitBuff and
// SetUnitDebuff appear above it in this file.
static int32_t TooltipSetAura(lua_State* L, CGTooltip* tooltip, uint8_t required, uint8_t forbidden);

// ref: FUN_006262c0
int32_t CGTooltip_SetUnitBuff(lua_State* L) {
    auto tooltip = TooltipThis(L);

    return TooltipSetAura(L, tooltip, AURA_FLAG_POSITIVE, 0);
}

// ref: FUN_00626350
int32_t CGTooltip_SetUnitDebuff(lua_State* L) {
    auto tooltip = TooltipThis(L);

    return TooltipSetAura(L, tooltip, 0, AURA_FLAG_POSITIVE);
}

// The three aura tooltips below share this. The reference fills them from an 814-byte routine
// (FUN_00625f00) that adds the remaining duration, the caster and a stack count on top of the
// spell text; frozen has no line for any of those yet, so this fills the spell body through the
// same TooltipSetSpellRec the spellbook tooltip uses and stops there.
//
// DIVERGENCE, and a visible one: the tooltip will show the spell but not how long is left on it.
// Recorded rather than faked, because a duration line invented here would be wrong in a way nobody
// would notice until they timed a buff by it.
static int32_t TooltipSetAura(lua_State* L, CGTooltip* tooltip, uint8_t required, uint8_t forbidden) {
    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3)) {
        return 0;
    }

    auto unit = Script_GetUnitFromName(lua_tostring(L, 2));

    if (!unit) {
        return 0;
    }

    // 1-based on the Lua side, as every index in this API is.
    auto index = static_cast<int32_t>(lua_tonumber(L, 3)) - 1;
    auto aura = AuraCacheGet(unit->GetGUID(), index, required, forbidden);

    if (!aura) {
        return 0;
    }

    return TooltipSetSpellRec(L, tooltip, g_spellDB.GetRecord(aura->spellID));
}

// ref: FUN_00626240
int32_t CGTooltip_SetUnitAura(lua_State* L) {
    auto tooltip = TooltipThis(L);

    return TooltipSetAura(L, tooltip, 0, 0);
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

// ref: FUN_0062fc20
int32_t CGTooltip_SetAuctionSellItem(lua_State* L) {
    auto tooltip = TooltipThis(L);
    WOWGUID guid = AuctionHouse::s_sellItem;

    if (guid) {
        auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, ".\\Tooltip.cpp", 0x1ff7));

        if (item && tooltip->SetItem(item->GetEntryID(), &guid, &guid, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr, 0, 0, 0)) {
            lua_pushnumber(L, 1.0);

            return 1;
        }
    }

    lua_pushnil(L);

    return 1;
}

int32_t CGTooltip_SetAuctionItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0061d9c0
int32_t CGTooltip_NumLines(lua_State* L) {
    lua_pushnumber(L, TooltipThis(L)->m_numLines);

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

// ref: FUN_006301a0
// The item in the socketing frame, as it will be with the gems placed.
int32_t CGTooltip_SetSocketedItem(lua_State* L) {
    auto tooltip = TooltipThis(L);
    auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(s_socketItem, TYPE_ITEM, ".\\Tooltip.cpp", 0x20ff));

    if (item) {
        WOWGUID itemGUID = item->GetGUID();
        WOWGUID none = 0;
        tooltip->SetItem(item->GetEntryID(), &none, &itemGUID, 0, 0, 0, 0, 0, 1, nullptr, 0, nullptr, 0, 0, 0);
    }

    return 0;
}

// ref: FUN_00630250
int32_t CGTooltip_SetSocketGem(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetSocketGem(index)", tooltip->GetDisplayName());

        return 0;
    }

    auto index = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 2)) - 1);
    WOWGUID guid = index < 3 ? s_socketGems[index] : 0;
    auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, ".\\Tooltip.cpp", 0x210f));

    if (item) {
        WOWGUID itemGUID = item->GetGUID();
        WOWGUID none = 0;
        tooltip->SetItem(item->GetEntryID(), &none, &itemGUID, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr, 0, 0, 0);
    }

    return 0;
}

// ref: FUN_00630370
// SetExistingSocketGem(index [, toDestroy]): the gem already in a socket of the item being
// socketed, under a "will be destroyed" header when asked.
int32_t CGTooltip_SetExistingSocketGem(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetSocketGem(index, [toDestroy])", tooltip->GetDisplayName());

        return 0;
    }

    auto number = static_cast<int32_t>(lua_tonumber(L, 2));
    auto index = static_cast<uint32_t>(number - 1);
    auto toDestroy = StringToBOOL(L, 3, 0);
    auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(s_socketItem, TYPE_ITEM, ".\\Tooltip.cpp", 0x2120));

    if (item && index < 3) {
        // FUN_00518b30: the enchantment in the socket's slot, none on a charter.
        auto enchantID = item->IsCharter() ? 0 : item->Item()->enchantments[number + 1].id;
        auto enchant = g_spellItemEnchantmentDB.GetRecord(enchantID);

        if (enchant && enchant->m_srcItemID) {
            WOWGUID none = 0;
            WOWGUID noItem = 0;
            tooltip->SetItem(enchant->m_srcItemID, &none, &noItem, 0, toDestroy, 0, 0, 0, 0, nullptr, 0, nullptr, 0, 0, 0);
        }
    }

    return 0;
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

// TODO FUN_0061f1f0 -- the tooltip's own IsEquippedItem, entry 00ad2cd8, not the global
// IsEquippedItem at 0051c690 that the name matcher offers first. It reads an item GUID the
// tooltip keeps at +0x340 and searches the player's equipped slots for it. The search is
// portable; the guid is not, because only the item-tooltip path sets it and that is the
// 24KB builder at 006277f0. Implementing the search alone would answer nil forever.
int32_t CGTooltip_IsEquippedItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetQuestLogSpecialItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTooltip_SetEquipmentSet(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// TODO FUN_00626560 toggles one global through FUN_006230d0 -- the frame-stack debug overlay.
// Frozen has neither the flag nor the overlay that reads it.
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
