#include "util/Lua.hpp"
#include "ui/game/CGTooltip.hpp"
#include "ui/game/CGTooltipScript.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "object/client/ItemCache.hpp"
#include "ui/FrameScript.hpp"
#include <storm/String.hpp>
#include <cstring>

int32_t CGTooltip::s_metatable;
int32_t CGTooltip::s_objectType;

CSimpleFrame* CGTooltip::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator

    return STORM_NEW(CGTooltip)(parent);
}

void CGTooltip::CreateScriptMetaTable() {
    auto L = FrameScript_GetContext();
    CGTooltip::s_metatable = FrameScript_Object::CreateScriptMetaTable(L, &CGTooltip::RegisterScriptMethods);
}

// ref: FUN_00514410
int32_t CGTooltip::GetObjectType() {
    if (!CGTooltip::s_objectType) {
        CGTooltip::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CGTooltip::s_objectType;
}

void CGTooltip::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, CGTooltipMethods, NUM_CG_TOOLTIP_SCRIPT_METHODS);
}

CGTooltip::CGTooltip(CSimpleFrame* parent) : CSimpleFrame(parent) {
    // TODO
}

int32_t CGTooltip::GetScriptMetaTable() {
    return CGTooltip::s_metatable;
}

bool CGTooltip::IsA(int32_t type) {
    return type == CGTooltip::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

FrameScript_Object::ScriptIx* CGTooltip::GetScriptByName(const char* name, ScriptData& data) {
    if (!SStrCmpI(name, "OnTooltipSetDefaultAnchor")) {
        return &this->m_onTooltipSetDefaultAnchor;
    }

    if (!SStrCmpI(name, "OnTooltipAddMoney")) {
        data.wrapper = "return function(self, money) %s end";
        return &this->m_onTooltipAddMoney;
    }

    if (!SStrCmpI(name, "OnTooltipCleared")) {
        return &this->m_onTooltipCleared;
    }

    if (!SStrCmpI(name, "OnTooltipSetUnit")) {
        return &this->m_onTooltipSetUnit;
    }

    if (!SStrCmpI(name, "OnTooltipSetItem")) {
        return &this->m_onTooltipSetItem;
    }

    if (!SStrCmpI(name, "OnTooltipSetSpell")) {
        return &this->m_onTooltipSetSpell;
    }

    if (!SStrCmpI(name, "OnTooltipSetQuest")) {
        return &this->m_onTooltipSetQuest;
    }

    if (!SStrCmpI(name, "OnTooltipSetAchievement")) {
        return &this->m_onTooltipSetAchievement;
    }

    return CSimpleFrame::GetScriptByName(name, data);
}

// Take a pair of font strings made elsewhere -- FrameXML and addons create them as
// $parentTextLeft<n> / $parentTextRight<n> -- as a line past the ones the template declares.
//
// The reference's own store for this (the callee of the AddFontStrings binding, FUN_0061fe30) has
// not been decompiled, so only the registration is ported here: the pairs are kept in the order
// they arrive and the line lookup in CGTooltipScript.cpp reads them after the template's lines.
void CGTooltip::AddFontStrings(CSimpleFontString* left, CSimpleFontString* right) {
    auto line = this->m_extraLines.New();

    line->left = left;
    line->right = right;
}

void CGTooltip::RunOnTooltipSetDefaultAnchorScript() {
    if (this->m_onTooltipSetDefaultAnchor.luaRef) {
        this->RunScript(this->m_onTooltipSetDefaultAnchor, 0, nullptr);
    }
}

void CGTooltip::RunOnTooltipAddMoneyScript(int32_t money) {
    if (this->m_onTooltipAddMoney.luaRef) {
        auto L = FrameScript_GetContext();
        lua_pushnumber(L, money);

        this->RunScript(this->m_onTooltipAddMoney, 1, nullptr);
    }
}

void CGTooltip::RunOnTooltipClearedScript() {
    if (this->m_onTooltipCleared.luaRef) {
        this->RunScript(this->m_onTooltipCleared, 0, nullptr);
    }
}

void CGTooltip::RunOnTooltipSetUnitScript() {
    if (this->m_onTooltipSetUnit.luaRef) {
        this->RunScript(this->m_onTooltipSetUnit, 0, nullptr);
    }
}

void CGTooltip::RunOnTooltipSetItemScript() {
    if (this->m_onTooltipSetItem.luaRef) {
        this->RunScript(this->m_onTooltipSetItem, 0, nullptr);
    }
}

// ref: FUN_0061b5b0
void ItemStatTotals::Clear() {
    memset(this->stats, 0, sizeof(this->stats));
    this->dps = 0.0f;
    this->flags = 0;
}

// ref: FUN_0061b8e0
void ItemStatTotals::AddArmorAndResistances(const ItemInfo* info) {
    this->flags |= 0x8;

    this->stats[0] += info->armor;

    for (int32_t i = 0; i < 6; i++) {
        this->stats[1 + i] += info->resistance[i];
    }
}

// ref: FUN_0061b930
void ItemStatTotals::AddSockets(const ItemInfo* info) {
    this->flags |= 0x10;

    for (int32_t i = 0; i < ItemInfo::MAX_SOCKETS; i++) {
        auto color = info->socketColor[i];

        if (color & 0x1) {
            this->stats[69]++;
        }

        if (color & 0x2) {
            this->stats[70]++;
        }

        if (color & 0x4) {
            this->stats[71]++;
        }

        if (color & 0x8) {
            this->stats[72]++;
        }
    }
}

// Stat type 38 (attack power) also counts toward both attack power totals, and 39 (ranged attack
// power) toward the ranged one.
// ref: FUN_0061b990
void ItemStatTotals::AddItemStats(const ItemInfo* info) {
    this->flags |= 0x20;

    for (int32_t i = 0; i < info->statsCount; i++) {
        auto type = info->statType[i];
        auto value = info->statValue[i];

        this->stats[11 + type] += value;

        if (type == 38) {
            this->stats[8] += value;
            this->stats[9] += value;
        } else if (type == 39) {
            this->stats[9] += value;
        }
    }
}

// Adds stats[index] into each of the count stats named by into. When collapse is set and they all
// come out equal, that common value moves back to stats[index] and the others are cleared;
// otherwise stats[index] is cleared.
// ref: FUN_0061b9e0
void ItemStatTotals::FoldStats(int32_t index, const int32_t* into, uint32_t count, int32_t collapse) {
    auto value = this->stats[index];
    int32_t previous = 0;

    for (uint32_t i = 0; i < count; i++) {
        this->stats[into[i]] += value;

        collapse = collapse && (i == 0 || this->stats[into[i]] == previous) ? 1 : 0;

        previous = this->stats[into[i]];
    }

    if (!collapse) {
        this->stats[index] = 0;
        return;
    }

    this->stats[index] = this->stats[into[0]];

    for (uint32_t i = 0; i < count; i++) {
        this->stats[into[i]] = 0;
    }
}

// Formats a time as "<n> days/hours/min/sec" through the <prefix>_DAYS, _HOURS, _MIN or _SEC
// global string, choosing the largest unit the time reaches. time is milliseconds, or seconds when
// inSeconds is set; roundUp rounds the count up instead of down, and a count that rounds up to a
// whole next unit is shown as 1 of that unit. A non-zero displayValue is printed in place of the
// count.
// ref: FUN_0061a9e0
void FormatTimeInterval(char* dest, uint32_t destSize, uint64_t time, const char* prefix, int32_t displayValue, int32_t roundUp, bool inSeconds) {
    if (!dest || !prefix) {
        return;
    }

    uint32_t minute = inSeconds ? 60 : 60000;
    uint32_t hour = minute * 60;
    *dest = '\0';
    uint32_t day = minute * 1440;

    auto value = static_cast<uint32_t>(time);
    const char* format;

    if ((time >> 32) == 0 && value < day) {
        if (value < hour) {
            if (value < minute) {
                if (!inSeconds) {
                    value /= 1000;
                }

                format = "%s_SEC";
            } else {
                auto count = roundUp
                    ? (static_cast<uint64_t>(value) - 1) / minute + 1
                    : static_cast<uint64_t>(value) / minute;

                value = static_cast<uint32_t>(count);

                if (count == 60) {
                    value = 1;
                    format = "%s_HOURS";
                } else {
                    format = "%s_MIN";
                }
            }
        } else {
            auto count = roundUp
                ? (static_cast<uint64_t>(value) - 1) / hour + 1
                : static_cast<uint64_t>(value) / hour;

            value = static_cast<uint32_t>(count);

            if (count == 24) {
                format = "%s_DAYS";
                value = 1;
            } else {
                format = "%s_HOURS";
            }
        }
    } else {
        if (!roundUp) {
            value = static_cast<uint32_t>(time / day);
        } else {
            value = static_cast<uint32_t>((time - 1) / day) + 1;
        }

        format = "%s_DAYS";
    }

    char key[256];
    SStrPrintf(key, sizeof(key), format, prefix);

    if (displayValue) {
        SStrPrintf(dest, destSize, FrameScript_GetText(key, -1, GENDER_NOT_APPLICABLE), displayValue);
        return;
    }

    SStrPrintf(dest, destSize, FrameScript_GetText(key, -1, GENDER_NOT_APPLICABLE), value);
}
