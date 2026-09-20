#include "ui/game/CGTooltipScript.hpp"
#include "ui/game/CGActionBar.hpp"
#include "object/client/SpellBook.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "glue/CCharacterSelection.hpp"
#include "glue/CharacterSelectionDisplay.hpp"
#include "gx/Coordinate.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/NameCache.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/ItemCache.hpp"
#include "ui/game/ContainerFrameScript.hpp"
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

CSimpleFontString* TooltipLine(CGTooltip* tooltip, int32_t line, bool right);
static bool TooltipCreateLine(CGTooltip* tooltip);

// The offsets the reference anchors new lines with, in UI units before the coordinate conversion:
// each line sits two below the one above it, and the right column's right edge sits forty to the
// right of its own line's left edge. Both read out of the image at 009e8d00 and 00a0ff3c.
static const float TOOLTIP_LINE_ANCHOR_Y = -2.0f;
static const float TOOLTIP_COLUMN_ANCHOR_X = 40.0f;

// Makes the next line's pair of font strings and registers it, which is how a tooltip grows past
// the eight lines GameTooltipTemplate.xml declares. The reference does this inside its shared
// add-a-line helper (0061fec0); frozen keeps the template's lines and its own extras apart, so this
// only ever appends to the extras.
//
// Anchoring is the reference's, and it is the same anchoring the template already uses for lines
// 1..8: the left string hangs off the previous line's bottom-left, and the right string's RIGHT
// edge attaches to its own line's LEFT edge.
static bool TooltipCreateLine(CGTooltip* tooltip) {
    auto name = tooltip->GetName();

    if (!name) {
        return false;
    }

    auto line = TOOLTIP_MAX_LINES + static_cast<int32_t>(tooltip->m_extraLines.Count()) + 1;
    auto previousLeft = TooltipLine(tooltip, line - 1, false);

    if (!previousLeft) {
        return false;
    }

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    auto leftMem = SMemAlloc(sizeof(CSimpleFontString), __FILE__, __LINE__, 0x0);
    auto left = new (leftMem) CSimpleFontString(tooltip, DRAWLAYER_ARTWORK, 1);

    auto rightMem = SMemAlloc(sizeof(CSimpleFontString), __FILE__, __LINE__, 0x0);
    auto rightString = new (rightMem) CSimpleFontString(tooltip, DRAWLAYER_ARTWORK, 1);

    char path[260];

    SStrPrintf(path, sizeof(path), "%sTextLeft%d", name, line);
    left->SetName(path);
    left->SetFontObject(previousLeft->GetFontObject());
    left->SetPoint(
        FRAMEPOINT_TOPLEFT, previousLeft, FRAMEPOINT_BOTTOMLEFT,
        0.0f, NDCToDDCWidth(TOOLTIP_LINE_ANCHOR_Y / scale), 0
    );

    SStrPrintf(path, sizeof(path), "%sTextRight%d", name, line);
    rightString->SetName(path);
    rightString->SetFontObject(previousLeft->GetFontObject());
    rightString->SetPoint(
        FRAMEPOINT_RIGHT, left, FRAMEPOINT_LEFT,
        NDCToDDCWidth(TOOLTIP_COLUMN_ANCHOR_X / scale), 0.0f, 0
    );

    CGTooltip::TOOLTIPLINE pair;
    pair.left = left;
    pair.right = rightString;
    tooltip->m_extraLines.Add(1, &pair);

    return true;
}

CSimpleFontString* TooltipLine(CGTooltip* tooltip, int32_t line, bool right) {
    if (line < 1) {
        return nullptr;
    }

    // Past the template's lines the font strings are the ones AddFontStrings registered, in the
    // order they arrived.
    if (line > TOOLTIP_MAX_LINES) {
        uint32_t extra = static_cast<uint32_t>(line - TOOLTIP_MAX_LINES - 1);

        // One past the end is the line being added right now, so make it. Anything further is a
        // caller reaching for a line that was never added, which still returns null.
        //
        // This accessor therefore has a side effect, which is worth being careful about because two
        // loops in this file bound themselves on TooltipMaxLines -- and that grows when a line is
        // created. Neither can run away, and the reason is arithmetic rather than luck: the bound
        // is TOOLTIP_MAX_LINES + Count(), and the highest line it reaches maps to extra index
        // Count() - 1, which already exists. The branch below needs index Count(), one further on.
        // So a loop over every line never creates one, and only an add can.
        if (extra == tooltip->m_extraLines.Count() && !TooltipCreateLine(tooltip)) {
            return nullptr;
        }

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

// Whether a line was added with wrapping asked for. Lines never marked do not wrap, which is the
// reference's default too -- its helper reads the flag with a default of 0.
bool TooltipLineWraps(CGTooltip* tooltip, int32_t line) {
    auto index = static_cast<size_t>(line - 1);

    return index < tooltip->m_lineWrap.size() && tooltip->m_lineWrap[index] != 0;
}

void TooltipSetLineWrap(CGTooltip* tooltip, int32_t line, bool wrap) {
    if (line < 1) {
        return;
    }

    auto index = static_cast<size_t>(line - 1);

    if (tooltip->m_lineWrap.size() <= index) {
        tooltip->m_lineWrap.resize(index + 1, 0);
    }

    tooltip->m_lineWrap[index] = wrap ? 1 : 0;
}

void TooltipSetLine(CGTooltip* tooltip, int32_t line, bool right, const char* text) {
    auto fontString = TooltipLine(tooltip, line, right);

    if (!fontString) {
        return;
    }

    // The reference passes 1 here, which runs the text through the language pass -- that is what
    // resolves the |4 plural and gender escapes FrameXML puts in tooltip strings.
    fontString->SetText(text ? text : "", 1);

    if (text && *text) {
        fontString->Show();
    } else {
        fontString->Hide();
    }
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
void TooltipUnitLevelLine(CGTooltip* tooltip, CGUnit_C* unit, int32_t line) {
    auto data = unit->Unit();

    if (!data) {
        return;
    }

    auto player = static_cast<CGUnit_C*>(
        ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, __FILE__, __LINE__));

    bool isPlayer = unit->IsA(TYPE_PLAYER);
    auto info = NameCacheGetCreatureInfo(unit->GetEntryID());

    // The boss flag is creature type flag bit 2, and it does two things at once: it replaces the
    // classification with "Boss" and it hides the level.
    bool isBoss = info && (info->typeFlags & 0x4) != 0;

    // A corpse takes over the class slot and drops the race.
    bool isCorpse = data->health < 1 || (data->dynamicFlags & 0x20) != 0;

    // --- level -------------------------------------------------------------------------------
    //
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

    // --- race and class ----------------------------------------------------------------------
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
        // "Not specified" and never prints.
        //
        // The second gate is a creature type flag frozen cannot name: the reference has a one-line
        // accessor for bit 26 (FUN_00715df0) and uses it only here, to suppress the type. What the
        // bit means has not been recovered -- only what it does.
        bool suppressed = info && (info->typeFlags & 0x04000000) != 0;
        auto type = unit->GetCreatureType();

        if (type != 10 && !suppressed && unit->GetReaction(player) < 4) {
            auto typeRec = type ? g_creatureTypeDB.GetRecord(type) : nullptr;

            if (typeRec) {
                classText = typeRec->m_name;
            }
        }
    }

    // --- type --------------------------------------------------------------------------------
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

    // --- pick the format ---------------------------------------------------------------------
    //
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

    TooltipSetLine(tooltip, line, false, text);
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

// Space kept between a line's left and right text. This WAS Frozen's own guess at 12; the
// reference's layout pass has since been read (FUN_0061caf0) and it uses 38.4, from 00a246cc.
const float TOOLTIP_COLUMN_GAP = 38.4f;

// How wide a wrapping line is allowed to get before it is broken. The reference's layout clamps to
// this (00a246dc) and then fits the text inside it.
const float TOOLTIP_WRAP_MAX_WIDTH = 230.4f;

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
// A note corrected in place, because the first version of it was wrong. It claimed the reference
// must re-anchor the right-hand column from C++ in a pass that had not been decompiled, and that
// two-column lines therefore overlap here. That pass has since been found -- it is the reference's
// on-demand line creation at 0061fec0 -- and it anchors the right region by its RIGHT to the same
// line's LEFT region at point LEFT with x=40, which is exactly what GameTooltipTemplate.xml already
// declares. So the reference and the template agree, and frozen's geometry for the eight
// template-declared lines matches the reference without doing anything.
//
// What that leaves genuinely open is narrower: whether that anchoring visually overlaps, and what
// the width term below is for if it does. Both are questions for a run, not for more decompiling.
// The width term is kept because it is what the reference's own resize computes.
// ref: the width half of FUN_0061caf0, the tooltip's layout pass.
//
// Two passes, and the order is the point. The tooltip's width is decided by the lines that do NOT
// wrap; a wrapping line is then fitted into whatever width those settled on. Letting a long
// wrapping line vote on the width first would just make the tooltip as wide as the line and there
// would be nothing left to wrap.
//
// A wrapping line can still widen the tooltip, but only as far as TOOLTIP_WRAP_MAX_WIDTH. That cap
// is why a long description becomes a paragraph instead of a single line running off the screen.
//
// NOT a complete port: the reference measures each broken segment and takes the widest, walking up
// to thirty break points per line (FUN_00482450). This asks the font string for the wrapped height
// at the chosen width instead and lets the text block do the breaking, so the width can come out
// narrower than the reference would choose on a line whose longest word is wider than the cap.
void TooltipResizeToFit(CGTooltip* tooltip) {
    if (tooltip->m_lineCount < 1) {
        return;
    }

    float inset = TooltipUIToLayout(TOOLTIP_INSET);
    float columnGap = TooltipUIToLayout(TOOLTIP_COLUMN_GAP);
    float wrapMax = TooltipUIToLayout(TOOLTIP_WRAP_MAX_WIDTH);

    // The minimum width is a whole-tooltip measurement; the passes below work in text width.
    float textWidth = TooltipUIToLayout(tooltip->m_minimumWidth) - inset - inset;

    if (textWidth < 0.0f) {
        textWidth = 0.0f;
    }

    // Pass 1 -- the lines that do not wrap.
    //
    // Every line's width is cleared first. A font string keeps whatever width it was last given,
    // so without this a line that wrapped in the previous tooltip would still be measuring itself
    // against that old width.
    for (int32_t line = 1; line <= tooltip->m_lineCount; line++) {
        auto left = TooltipLine(tooltip, line, false);
        auto right = TooltipLine(tooltip, line, true);

        if (left) {
            left->SetWidth(0.0f);
        }

        if (right) {
            right->SetWidth(0.0f);
        }

        if (TooltipLineWraps(tooltip, line)) {
            continue;
        }

        const char* leftText = left ? left->GetText() : nullptr;
        const char* rightText = right ? right->GetText() : nullptr;

        float lineWidth = 0.0f;

        if (leftText && *leftText) {
            lineWidth += left->GetStringWidth();
        }

        if (rightText && *rightText) {
            if (lineWidth > 0.0f) {
                lineWidth += columnGap;
            }

            lineWidth += right->GetStringWidth();
        }

        if (lineWidth > textWidth) {
            textWidth = lineWidth;
        }
    }

    // Pass 2 -- a wrapping line may widen the tooltip, but not past the cap.
    for (int32_t line = 1; line <= tooltip->m_lineCount; line++) {
        if (!TooltipLineWraps(tooltip, line)) {
            continue;
        }

        auto left = TooltipLine(tooltip, line, false);
        const char* leftText = left ? left->GetText() : nullptr;

        if (!leftText || !*leftText) {
            continue;
        }

        // Measured with no width set, so this is the natural single-line extent.
        float natural = left->GetStringWidth();
        float target = natural > wrapMax ? wrapMax : natural;

        if (target > textWidth) {
            textWidth = target;
        }
    }

    // Give every wrapping line the final width so it breaks against it, then measure heights. This
    // has to follow both passes: the width is not known until they are done.
    float textHeight = 0.0f;

    for (int32_t line = 1; line <= tooltip->m_lineCount; line++) {
        auto left = TooltipLine(tooltip, line, false);
        auto right = TooltipLine(tooltip, line, true);

        if (TooltipLineWraps(tooltip, line) && left) {
            left->SetWidth(textWidth);
        }

        const char* leftText = left ? left->GetText() : nullptr;
        const char* rightText = right ? right->GetText() : nullptr;

        float lineHeight = 0.0f;

        if (leftText && *leftText) {
            lineHeight = left->GetStringHeight();
        }

        if (rightText && *rightText) {
            float rightHeight = right->GetStringHeight();
            lineHeight = lineHeight > rightHeight ? lineHeight : rightHeight;
        }

        if (line > 1) {
            textHeight += TooltipUIToLayout(TOOLTIP_LINE_GAP);
        }

        textHeight += lineHeight;
    }

    float width = textWidth + inset + inset;

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
    tooltip->m_lineWrap.clear();

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

// ref: FUN_0061eb40
int32_t CGTooltip_SetOwner(lua_State* L) {
    auto tooltip = TooltipThis(L);

    // Every OnEnter handler calls this first; while it was a stub, IsOwned answered false for every
    // frame and no tooltip could ever be shown.
    if (lua_type(L, 2) != LUA_TTABLE) {
        return luaL_error(L, "Usage: %s:SetOwner(frame)", tooltip->GetDisplayName());
    }

    lua_rawgeti(L, 2, 0);
    auto owner = static_cast<CSimpleFrame*>(lua_touserdata(L, -1));
    lua_settop(L, -2);

    // Three checks the reference makes and this did not. Each one used to fail silently: a table
    // that is not a frame object left the tooltip owned by garbage, and a tooltip set to own itself
    // anchors to its own moving edge. FrameXML passes whatever an add-on hands it, so these are the
    // errors an add-on author is meant to see.
    if (!owner) {
        return luaL_error(L, "%s:SetOwner(): Couldn't find 'this' in frame object",
                          tooltip->GetDisplayName());
    }

    if (!owner->IsA(CSimpleFrame::GetObjectType())) {
        return luaL_error(L, "%s:SetOwner(): Wrong object type, expected frame",
                          tooltip->GetDisplayName());
    }

    if (static_cast<void*>(owner) == static_cast<void*>(tooltip)) {
        return luaL_error(L, "%s:SetOwner(): Can't set owner to self", tooltip->GetDisplayName());
    }

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

    // Full means full only if another line cannot be made. TooltipMaxLines counts the template's
    // eight plus whatever has been created so far, so without this the tooltip could never grow:
    // nothing would ask for line nine, so line nine would never be created, so the limit would stay
    // at eight forever.
    //
    // There is deliberately no ceiling here. The reference does not impose one either -- its own
    // growth is bounded only by the caller running out of lines to add -- so a runaway caller can
    // allocate font strings without limit in both. Matching that rather than inventing a cap.
    if (tooltip->m_lineCount >= TooltipMaxLines(tooltip) && !TooltipCreateLine(tooltip)) {
        return 0;
    }

    tooltip->m_lineCount++;

    // AddLine(text, r, g, b, wrapText) -- argument 6, confirmed against FUN_00620340, which reads
    // it with a default of 0.
    TooltipSetLineWrap(tooltip, tooltip->m_lineCount, lua_toboolean(L, 6) != 0);
    TooltipSetLine(tooltip, tooltip->m_lineCount, false, lua_tostring(L, 2));
    TooltipResizeToFit(tooltip);

    return 0;
}

int32_t CGTooltip_AddDoubleLine(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2) || !lua_isstring(L, 3)) {
        return 0;
    }

    // Full means full only if another line cannot be made. TooltipMaxLines counts the template's
    // eight plus whatever has been created so far, so without this the tooltip could never grow:
    // nothing would ask for line nine, so line nine would never be created, so the limit would stay
    // at eight forever.
    //
    // There is deliberately no ceiling here. The reference does not impose one either -- its own
    // growth is bounded only by the caller running out of lines to add -- so a runaway caller can
    // allocate font strings without limit in both. Matching that rather than inventing a cap.
    if (tooltip->m_lineCount >= TooltipMaxLines(tooltip) && !TooltipCreateLine(tooltip)) {
        return 0;
    }

    tooltip->m_lineCount++;

    // Never wraps: the reference's helper clears the flag as soon as there is right-hand text.
    TooltipSetLineWrap(tooltip, tooltip->m_lineCount, false);
    TooltipSetLine(tooltip, tooltip->m_lineCount, false, lua_tostring(L, 2));
    TooltipSetLine(tooltip, tooltip->m_lineCount, true, lua_tostring(L, 3));
    TooltipResizeToFit(tooltip);

    return 0;
}

int32_t CGTooltip_AddTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_006204e0
//
// SetText RESETS the tooltip to one line. The reference is three calls -- clear (FUN_0061c620,
// the same clear every filler starts with, ported as TooltipClear), add one line, then show --
// and the clear is the whole point of it.
//
// This used to rewrite line 1 and keep the old line count, which reads as correct and is not:
// the stale lines stay on screen and the next AddLine appends after them. Hovering the same
// thing twice showed its text twice, three times on the third hover, because FrameXML's usual
// shape is SetText followed by AddLine. Seen in game before it was traced back here.
int32_t CGTooltip_SetText(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetText(\"text\" [, color])", tooltip->GetDisplayName());
    }

    TooltipClear(tooltip);

    tooltip->m_lineCount = 1;

    // SetText(text, r, g, b, alpha, textWrap) -- argument 7, which is where FUN_006204e0 reads it.
    TooltipSetLineWrap(tooltip, 1, lua_toboolean(L, 7) != 0);
    TooltipSetLine(tooltip, 1, false, lua_tostring(L, 2));

    // The reference tail-calls its show here, which is why FrameXML never calls GameTooltip:Show()
    // after SetText either.
    TooltipShow(tooltip);

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
// Defined below, next to the setters that are its other callers.
void TooltipSetItemInfo(CGTooltip* tooltip, const ItemInfo* info, int32_t durability, int32_t maxDurability);

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
    const char* item = SStrStr(link, "item:");

    if (item) {
        // The link carries the entry as the first field after "item:", with the enchant, the three
        // gems and the rest following behind colons. Only the entry is read here; the suffix and
        // the gems change what the reference draws, and neither is ported.
        auto info = ItemCacheGet(SStrToInt(item + 5));

        if (!info) {
            return 0;
        }

        TooltipSetItemInfo(tooltip, info, 0, 0);

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

    TooltipClear(tooltip);

    tooltip->m_lineCount = 1;
    TooltipSetLine(tooltip, 1, false,
                   FrameScript_GetText("MINIMAP_TRACKING_TOOLTIP_NONE", -1, GENDER_NOT_APPLICABLE));

    TooltipShow(tooltip);

    return 0;
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

// ref: FUN_006277f0, its leading lines.
//
// The reference's item tooltip body is one 3,200 line function that also covers petitions, glyphs,
// keys, sockets, enchants, set bonuses, arena requirements, refund timers and disenchant skills.
// This is its spine -- the lines an ordinary weapon or piece of armour shows -- emitted in the
// order the reference emits them, which was recovered by reading where each global string is
// fetched: the damage band, then SPEED, then DPS_TEMPLATE, then ARMOR_TEMPLATE, then (far below
// the parts not ported) DURABILITY_TEMPLATE and the description.
//
// Every value here comes from the item record read in ItemCache.cpp, whose field order was
// verified against the reference's own itemcache.wdb. The formats come from GlobalStrings through
// FrameScript_GetText rather than being spelled out, so a non-English client formats its own way.
//
// Not ported, and each one is a line an item can legitimately want: item level, bind and unique
// lines, the stat block, resistances, sockets and their bonus, set bonuses, spell triggers,
// requirements (level, skill, reputation), and the sell price.
// The record's delay is in milliseconds; every line that shows a speed shows seconds
// (_DAT_009e1134 in the reference).
static const float DELAY_TO_SECONDS = 0.001f;

// The order the stat block lists stats in, read out of the reference's DAT_00a262f0. This is a
// display order, not the record's storage order, and the loop over it is what decides which stat
// types appear at all: the rating stats (12 upward) are absent on purpose.
static const int32_t STAT_DISPLAY_ORDER[] = { 4, 3, 7, 5, 6, 1, 0, 8, 9, 2, 10 };

// The GlobalStrings key per stat type, from the reference's table at 00ad6640. Only the entries
// the order above can reach are here; the full table runs to 48 and then continues into the socket
// colour names. The empty ones are empty in the reference too and emit no line.
static const char* const STAT_NAME_KEYS[] = {
    "ITEM_MOD_MANA",      // 0
    "ITEM_MOD_HEALTH",    // 1
    "",                   // 2
    "ITEM_MOD_AGILITY",   // 3
    "ITEM_MOD_STRENGTH",  // 4
    "ITEM_MOD_INTELLECT", // 5
    "ITEM_MOD_SPIRIT",    // 6
    "ITEM_MOD_STAMINA",   // 7
    "",                   // 8
    "",                   // 9
    "",                   // 10
};

static const int32_t STAT_NAME_KEY_COUNT =
    static_cast<int32_t>(sizeof(STAT_NAME_KEYS) / sizeof(STAT_NAME_KEYS[0]));

// Inventory type 16. A cloak's subclass row reads "Cloth", which would say the wrong thing beside
// the slot, so the reference leaves the right half of the type line empty for one.
static const int32_t INVENTORY_TYPE_CLOAK = 16;

// ItemSubClass has no id column -- a row is the (class, subclass) pair -- so this scans its 119.
static const ItemSubClassRec* ItemSubClassRecord(int32_t itemClass, int32_t subClass) {
    for (int32_t i = 0; i < g_itemSubClassDB.GetNumRecords(); i++) {
        auto rec = g_itemSubClassDB.GetRecordByIndex(i);

        if (rec && rec->m_classID == itemClass && rec->m_subClassID == subClass) {
            return rec;
        }
    }

    return nullptr;
}

// Item classes the tooltip gates lines on. The speed and damage-per-second lines want a weapon;
// the item level line wants any of the four below.
static const int32_t ITEM_CLASS_WEAPON = 2;
static const int32_t ITEM_CLASS_ARMOR = 4;
static const int32_t ITEM_CLASS_REAGENT = 5;
static const int32_t ITEM_CLASS_PROJECTILE = 6;

void TooltipSetItemInfo(CGTooltip* tooltip, const ItemInfo* info, int32_t durability, int32_t maxDurability) {
    TooltipClear(tooltip);

    int32_t line = 1;
    char text[512];

    // The name carries the quality colour as an escape rather than through a colour call: the
    // reference colours the font string directly, which frozen's tooltip lines have no API for
    // yet, and the escape reaches the same place through the text pass.
    static const uint32_t s_qualityColors[] = {
        0x9D9D9D, 0xFFFFFF, 0x1EFF00, 0x0070DD, 0xA335EE, 0xFF8000, 0xE6CC80, 0xE6CC80
    };

    auto quality = (info->quality >= 0 && info->quality <= 7) ? info->quality : 1;

    SStrPrintf(text, sizeof(text), "|cff%06x%s|r", s_qualityColors[quality], info->name.c_str());
    TooltipSetLine(tooltip, line++, false, text);

    // The equip slot on the left, the item's subtype on the right -- "Chest" / "Plate".
    //
    // The right half is the ItemSubClass row's display name, and the reference suppresses it in
    // two cases: a cloak (inventory type 16), whose subclass would read "Cloth" and mislead, and
    // any subclass whose flags carry bit 0. Both tests are the reference's.
    //
    // Not ported: class 6 (projectile) takes a different left half entirely, out of an ammo-slot
    // table rather than the equip locations, so a quiver's arrows would name the wrong thing here.
    {
        const char* slotKey = (info->inventoryType > 0 && info->inventoryType < EQUIP_LOCATION_COUNT)
            ? s_equipLocations[info->inventoryType]
            : nullptr;

        const char* left = (slotKey && *slotKey && info->itemClass != ITEM_CLASS_PROJECTILE)
            ? FrameScript_GetText(slotKey, -1, GENDER_NOT_APPLICABLE)
            : nullptr;

        auto subClass = ItemSubClassRecord(info->itemClass, info->subClass);

        const char* right = nullptr;

        if (subClass && info->inventoryType != INVENTORY_TYPE_CLOAK && !(subClass->m_flags & 1)
            && subClass->m_displayName && *subClass->m_displayName) {
            right = subClass->m_displayName;
        }

        if ((left && *left) || right) {
            TooltipSetLine(tooltip, line, false, left ? left : "");

            if (right) {
                TooltipSetLine(tooltip, line, true, right);
            }

            line++;
        }
    }

    // The damage band. delay is in milliseconds and the speed shown is seconds to one decimal.
    if (info->delay > 0 && (info->damageMin[0] > 0.0f || info->damageMax[0] > 0.0f)) {
        auto low = static_cast<int32_t>(info->damageMin[0]);
        auto high = static_cast<int32_t>(info->damageMax[0]);

        // The reference picks the single-value template when the band has no spread, rather than
        // printing "5 - 5 Damage".
        if (low == high) {
            SStrPrintf(text, sizeof(text),
                       FrameScript_GetText("SINGLE_DAMAGE_TEMPLATE", -1, GENDER_NOT_APPLICABLE), low);
        } else {
            SStrPrintf(text, sizeof(text),
                       FrameScript_GetText("DAMAGE_TEMPLATE", -1, GENDER_NOT_APPLICABLE), low, high);
        }

        TooltipSetLine(tooltip, line, false, text);

        // Speed and damage-per-second are WEAPON lines, not damage lines: the reference gates both
        // on itemClass == 2. A thrown potion or a wand-less caster offhand can carry a damage band
        // without being a weapon, and showing it a speed would be wrong.
        if (info->itemClass == ITEM_CLASS_WEAPON) {
            char speed[64];
            SStrPrintf(speed, sizeof(speed), "%s %.2f",
                       FrameScript_GetText("SPEED", -1, GENDER_NOT_APPLICABLE),
                       info->delay * DELAY_TO_SECONDS);
            TooltipSetLine(tooltip, line, true, speed);
        }

        line++;

        if (info->itemClass == ITEM_CLASS_WEAPON) {
            // The reference accumulates (min + max) * 0.5 across every damage band and divides the
            // total by the speed, so a weapon with a second band counts both -- not just the one
            // the line above prints.
            float total = 0.0f;

            for (int32_t i = 0; i < ItemInfo::MAX_DAMAGES; i++) {
                total += (info->damageMin[i] + info->damageMax[i]) * 0.5f;
            }

            SStrPrintf(text, sizeof(text),
                       FrameScript_GetText("DPS_TEMPLATE", -1, GENDER_NOT_APPLICABLE),
                       total / (info->delay * DELAY_TO_SECONDS));
            TooltipSetLine(tooltip, line++, false, text);
        }
    }

    if (info->armor) {
        SStrPrintf(text, sizeof(text),
                   FrameScript_GetText("ARMOR_TEMPLATE", -1, GENDER_NOT_APPLICABLE), info->armor);
        TooltipSetLine(tooltip, line++, false, text);
    }

    if (info->block) {
        SStrPrintf(text, sizeof(text),
                   FrameScript_GetText("SHIELD_BLOCK_TEMPLATE", -1, GENDER_NOT_APPLICABLE), info->block);
        TooltipSetLine(tooltip, line++, false, text);
    }

    // The stat block.
    //
    // Two things here are the reference's and would not survive being guessed from a screenshot.
    //
    // The stats are shown in a FIXED display order (DAT_00a262f0), not in the order the record
    // carries them, so a chest with stamina stored before strength still lists strength first.
    // And the order table only names eleven stat types, all of them primary -- the rating stats
    // (types 12 upward: crit, haste, expertise and the rest) are not part of this block at all.
    // Those appear as green "Equip:" lines out of the spell block, which is not ported.
    //
    // The whole block is skipped for an item with a scaling stat distribution, which computes its
    // stats instead of storing them.
    if (info->scalingStatValue == 0) {
        for (auto wanted : STAT_DISPLAY_ORDER) {
            for (int32_t i = 0; i < ItemInfo::MAX_STATS; i++) {
                if (info->statValue[i] == 0 || info->statType[i] == -1 || info->statType[i] != wanted) {
                    continue;
                }

                auto key = (wanted >= 0 && wanted < STAT_NAME_KEY_COUNT) ? STAT_NAME_KEYS[wanted] : "";
                auto format = *key ? FrameScript_GetText(key, -1, GENDER_NOT_APPLICABLE) : nullptr;

                // Several entries in the reference's table are deliberately empty; it tests the
                // resolved string and emits nothing rather than a line reading "%c%d".
                if (format && *format) {
                    auto value = info->statValue[i];

                    // The format's leading %c is the sign and the %d after it is the magnitude, so
                    // the value is split rather than printed signed.
                    SStrPrintf(text, sizeof(text), format,
                               value < 1 ? '-' : '+',
                               value < 0 ? -value : value);
                    TooltipSetLine(tooltip, line, false, text);
                    TooltipSetLineWrap(tooltip, line, true);
                    line++;
                }

                break;
            }
        }
    }

    // Resistances.
    //
    // The reference first asks whether all six are the same value and, if they are, prints one
    // "to All Resistances" line instead of six. Otherwise it walks schools 2 through 6 -- fire,
    // nature, frost, shadow, arcane -- and prints the non-zero ones.
    //
    // Holy (resistance[0], school 1) is never printed on its own. It takes part in the all-equal
    // test and nothing else, which is why no 3.3.5 item shows a holy resistance line.
    {
        bool allEqual = true;

        for (int32_t i = 1; i < 6; i++) {
            if (info->resistance[i] != info->resistance[0]) {
                allEqual = false;
                break;
            }
        }

        if (allEqual) {
            if (info->resistance[0] != 0) {
                auto value = info->resistance[0];

                SStrPrintf(text, sizeof(text),
                           FrameScript_GetText("ITEM_RESIST_ALL", -1, GENDER_NOT_APPLICABLE),
                           value < 1 ? '-' : '+',
                           value < 0 ? -value : value);
                TooltipSetLine(tooltip, line++, false, text);
            }
        } else {
            for (int32_t school = 2; school < 7; school++) {
                auto value = info->resistance[school - 1];

                if (value == 0) {
                    continue;
                }

                char schoolKey[32];
                SStrPrintf(schoolKey, sizeof(schoolKey), "SPELL_SCHOOL%d_CAP", school);

                SStrPrintf(text, sizeof(text),
                           FrameScript_GetText("ITEM_RESIST_SINGLE", -1, GENDER_NOT_APPLICABLE),
                           value < 1 ? '-' : '+',
                           value < 0 ? -value : value,
                           FrameScript_GetText(schoolKey, -1, GENDER_NOT_APPLICABLE));
                TooltipSetLine(tooltip, line++, false, text);
            }
        }
    }

    // Durability comes from the item instance, not the record, so it is passed in; an item with no
    // durability at all shows no line rather than "0 / 0".
    if (maxDurability > 0) {
        SStrPrintf(text, sizeof(text),
                   FrameScript_GetText("DURABILITY_TEMPLATE", -1, GENDER_NOT_APPLICABLE),
                   durability, maxDurability);
        TooltipSetLine(tooltip, line++, false, text);
    }

    // "Requires Level N". The test is > 1, not > 0: an item anyone can use from the first level
    // shows no line at all, and using > 0 would put "Requires Level 1" on half the starting gear.
    if (info->requiredLevel > 1) {
        SStrPrintf(text, sizeof(text),
                   FrameScript_GetText("ITEM_MIN_LEVEL", -1, GENDER_NOT_APPLICABLE),
                   info->requiredLevel);
        TooltipSetLine(tooltip, line++, false, text);
    }

    // "Item Level N" is off by default and is not shown for every item even when it is on: the
    // reference gates it on the showItemLevel CVar AND on the item being a weapon, a piece of
    // armour, a reagent or a projectile. A consumable never shows one however the CVar is set.
    auto showItemLevel = CVar::Lookup("showItemLevel");

    if (showItemLevel && showItemLevel->GetInt()
        && (info->itemClass == ITEM_CLASS_WEAPON
            || info->itemClass == ITEM_CLASS_ARMOR
            || info->itemClass == ITEM_CLASS_REAGENT
            || info->itemClass == ITEM_CLASS_PROJECTILE)) {
        SStrPrintf(text, sizeof(text),
                   FrameScript_GetText("ITEM_LEVEL", -1, GENDER_NOT_APPLICABLE), info->itemLevel);
        TooltipSetLine(tooltip, line++, false, text);
    }

    if (!info->description.empty()) {
        // The reference shows the flavour text in quotes and lets it wrap.
        SStrPrintf(text, sizeof(text), "\"%s\"", info->description.c_str());
        TooltipSetLine(tooltip, line, false, text);
        TooltipSetLineWrap(tooltip, line, true);
        line++;
    }

    tooltip->m_lineCount = line - 1;

    TooltipShow(tooltip);
}

// ref: FUN_0062e050
//
// SetInventoryItem(unit, slot [, nameOnly]) -> hasItem, hasCooldown, repairCost.
//
// Only the player's own slots resolve, which is what InventoryItem already enforces and what the
// server actually sends. The reference also handles the bank bag slots, the equipment-set overlay
// and a repair cost read from the merchant frame; none of those exist here, so the third return is
// zero rather than a guess.
int32_t CGTooltip_SetInventoryItem(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetInventoryItem(unit, slot [, nameOnly])",
                          tooltip->GetDisplayName());
    }

    if (!lua_isnumber(L, 3)) {
        return luaL_error(L, "Invalid inventory slot in SetInventoryItem");
    }

    auto item = Script_GetInventoryItem(L, 2, 3);
    auto info = item ? ItemCacheGet(item->GetEntryID()) : nullptr;

    // An item whose record has not arrived yet answers "no item" and leaves the tooltip alone. The
    // cache has asked for it by now, so the next hover fills in -- which is why FrameXML re-runs
    // these on every OnEnter rather than caching the answer.
    if (!info) {
        return 0;
    }

    // Durability is per-item, not per-record: an identical sword at half health shows a different
    // line to a fresh one, so it comes off the object rather than the cache.
    auto data = item->Item();

    TooltipSetItemInfo(tooltip, info,
                       data ? data->durability : 0,
                       data ? data->maxDurability : 0);

    lua_pushboolean(L, 1);
    lua_pushboolean(L, 0); // hasCooldown: item cooldowns are not tracked yet
    lua_pushnumber(L, 0.0); // repairCost: needs the merchant frame

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

// ref: FUN_0062f420
//
// SetBagItem(bag, slot) -> hasItem, hasCooldown, repairCost, same three as SetInventoryItem.
//
// The bag is 0-based and the slot 1-based, which is how FrameXML passes them and what the
// container lookup expects.
int32_t CGTooltip_SetBagItem(lua_State* L) {
    auto tooltip = TooltipThis(L);

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetBagItem(bag, slot)", tooltip->GetDisplayName());
    }

    auto item = Script_GetContainerItem(L, 2, 3);
    auto info = item ? ItemCacheGet(item->GetEntryID()) : nullptr;

    if (!info) {
        return 0;
    }

    auto data = item->Item();

    TooltipSetItemInfo(tooltip, info,
                       data ? data->durability : 0,
                       data ? data->maxDurability : 0);

    lua_pushboolean(L, 1);
    lua_pushboolean(L, 0); // hasCooldown: item cooldowns are not tracked yet
    lua_pushnumber(L, 0.0); // repairCost: needs the merchant frame

    return 3;
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
    TooltipClear(tooltip);

    tooltip->m_unitGUID = guid;

    int32_t line = 1;
    TooltipSetLine(tooltip, line, false, TooltipUnitName(unit));

    // The title sits between the name and the level line -- "Innkeeper" under "Amy Davenport".
    auto subName = unit->GetSubName();

    if (subName && *subName) {
        TooltipSetLine(tooltip, ++line, false, subName);
    }

    TooltipUnitLevelLine(tooltip, unit, ++line);

    tooltip->m_lineCount = line;

    // UnitFrame_UpdateTooltip colours TextLeft1 by reaction right after this, and FrameXML's
    // OnTooltipSetUnit handlers add their own lines, so the layout is taken afterwards.
    tooltip->RunOnTooltipSetUnitScript();
    TooltipShow(tooltip);

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
