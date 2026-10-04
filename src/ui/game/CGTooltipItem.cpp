#include "ui/game/CGTooltip.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "object/Types.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ItemLink.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/SpellBook.hpp"
#include "object/client/SpellHistory.hpp"
#include "object/client/Spell_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/Util.hpp"
#include "ui/game/CGArenaTeamInfo.hpp"
#include "ui/game/ContainerFrameScript.hpp"
#include "ui/game/Cursor.hpp"
#include "ui/game/EquipmentManager.hpp"
#include "ui/game/ItemSocketInfo.hpp"
#include "ui/game/LootFrame.hpp"
#include "ui/game/MerchantFrame.hpp"
#include "ui/game/QuestTextParser.hpp"
#include "ui/game/ReputationInfo.hpp"
#include "ui/game/TooltipColors.hpp"
#include <storm/String.hpp>
#include <tempest/Rect.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The item tooltip (Tooltip.cpp, FUN_006277f0) and the helpers only it reads.

namespace {

// The colour codes the enchantment lines are wrapped in.
const char* const TOOLTIP_CODE_WHITE = "|cffffffff";                              // ref: PTR_s__cffffffff_00ad2a54
const char* const TOOLTIP_CODE_GRAY = "|cff808080";                               // ref: PTR_s__cff808080_00ad2a58
const char* const TOOLTIP_CODE_RED = "|cffff2020";                                // ref: PTR_s__cffff2020_00ad2a5c

// What colorblind mode puts before a coloured line: none, then easy to impossible.
const char* const s_colorblindPrefixes[5] = { "", "[+] ", "[++] ", "[+++] ", "[-] " };   // ref: PTR_DAT_00ad2ac8

// The global string naming each inventory type.
const char* const s_inventoryTypeTokens[29] = {                                   // ref: PTR_DAT_00ac7fd8
    "", "INVTYPE_HEAD", "INVTYPE_NECK", "INVTYPE_SHOULDER", "INVTYPE_BODY", "INVTYPE_CHEST",
    "INVTYPE_WAIST", "INVTYPE_LEGS", "INVTYPE_FEET", "INVTYPE_WRIST", "INVTYPE_HAND",
    "INVTYPE_FINGER", "INVTYPE_TRINKET", "INVTYPE_WEAPON", "INVTYPE_SHIELD", "INVTYPE_RANGED",
    "INVTYPE_CLOAK", "INVTYPE_2HWEAPON", "INVTYPE_BAG", "INVTYPE_TABARD", "INVTYPE_ROBE",
    "INVTYPE_WEAPONMAINHAND", "INVTYPE_WEAPONOFFHAND", "INVTYPE_HOLDABLE", "INVTYPE_AMMO",
    "INVTYPE_THROWN", "INVTYPE_RANGEDRIGHT", "INVTYPE_QUIVER", "INVTYPE_RELIC",
};

// The global string naming each item stat type.
const char* const s_itemModTokens[49] = {                                         // ref: PTR_s_ITEM_MOD_MANA_00ad6640
    "ITEM_MOD_MANA", "ITEM_MOD_HEALTH", "", "ITEM_MOD_AGILITY", "ITEM_MOD_STRENGTH",
    "ITEM_MOD_INTELLECT", "ITEM_MOD_SPIRIT", "ITEM_MOD_STAMINA", "", "", "", "",
    "ITEM_MOD_DEFENSE_SKILL_RATING", "ITEM_MOD_DODGE_RATING", "ITEM_MOD_PARRY_RATING",
    "ITEM_MOD_BLOCK_RATING", "ITEM_MOD_HIT_MELEE_RATING", "ITEM_MOD_HIT_RANGED_RATING",
    "ITEM_MOD_HIT_SPELL_RATING", "ITEM_MOD_CRIT_MELEE_RATING", "ITEM_MOD_CRIT_RANGED_RATING",
    "ITEM_MOD_CRIT_SPELL_RATING", "ITEM_MOD_HIT_TAKEN_MELEE_RATING",
    "ITEM_MOD_HIT_TAKEN_RANGED_RATING", "ITEM_MOD_HIT_TAKEN_SPELL_RATING",
    "ITEM_MOD_CRIT_TAKEN_MELEE_RATING", "ITEM_MOD_CRIT_TAKEN_RANGED_RATING",
    "ITEM_MOD_CRIT_TAKEN_SPELL_RATING", "ITEM_MOD_HASTE_MELEE_RATING",
    "ITEM_MOD_HASTE_RANGED_RATING", "ITEM_MOD_HASTE_SPELL_RATING", "ITEM_MOD_HIT_RATING",
    "ITEM_MOD_CRIT_RATING", "ITEM_MOD_HIT_TAKEN_RATING", "ITEM_MOD_CRIT_TAKEN_RATING",
    "ITEM_MOD_RESILIENCE_RATING", "ITEM_MOD_HASTE_RATING", "ITEM_MOD_EXPERTISE_RATING",
    "ITEM_MOD_ATTACK_POWER", "ITEM_MOD_RANGED_ATTACK_POWER", "", "ITEM_MOD_SPELL_HEALING_DONE",
    "ITEM_MOD_SPELL_DAMAGE_DONE", "ITEM_MOD_MANA_REGENERATION",
    "ITEM_MOD_ARMOR_PENETRATION_RATING", "ITEM_MOD_SPELL_POWER", "ITEM_MOD_HEALTH_REGEN",
    "ITEM_MOD_SPELL_PENETRATION", "ITEM_MOD_BLOCK_VALUE",
};

// The order the primary stats are listed in, and the stats listed as "Equip:" lines after them.
const int32_t s_primaryStatOrder[11] = { 4, 3, 7, 5, 6, 1, 0, 8, 9, 2, 10 };      // ref: DAT_00a262f0
const int32_t s_equipStatOrder[37] = {                                             // ref: DAT_00a25f78
    12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
    35, 36, 37, 38, 39, 40, 42, 41, 43, 44, 45, 46, 47, 48,
};

// The socket colours, by colour bit.
const char* const s_socketNames[4] = { "Meta", "Red", "Yellow", "Blue" };           // ref: PTR_DAT_00ad6704

// The gem colour a meta gem condition counts, by operand type, and its name.
const int32_t s_gemColorIndex[5] = { -1, 0, 1, 2, 3 };                            // ref: DAT_00a246e0
const char* const s_gemColorTokens[5] = { nullptr, "META_GEM", "RED_GEM", "YELLOW_GEM", "BLUE_GEM" };   // ref: PTR_DAT_00ad2ab4

// The reputation each standing begins at, hated to exalted.
const int32_t s_standingThresholds[8] = { -42000, -6000, -3000, 0, 3000, 9000, 21000, 42000 };   // ref: DAT_00a2d2fc

// The equipment slots each inventory type goes in.
const uint32_t s_inventoryTypeSlots[29] = {                                        // ref: DAT_00a2d288
    0x00000000, 0x00000001, 0x00000002, 0x00000004, 0x00000008, 0x00000010, 0x00000020,
    0x00000040, 0x00000080, 0x00000100, 0x00000200, 0x00000c00, 0x00003000, 0x00018000,
    0x00010000, 0x00020000, 0x00004000, 0x00018000, 0x00780000, 0x00040000, 0x00000010,
    0x00018000, 0x00018000, 0x00010000, 0x00000000, 0x00020000, 0x00020000, 0x00000000,
    0x00020000,
};

// The set the bonus sort reads (0x00c5cf48).
const ItemSetRec* s_sortSet;

// What the item set match counts against: the tooltip, the set, each piece's name record, the
// pieces matched, and the equipment slots already used. The reference passes it in EAX.
struct ITEMSETMATCH {
    CGTooltip* tooltip;
    const ItemSetRec* set;
    const ItemNameRec* names[17];
    int32_t* matched;
    uint8_t slotUsed[19];
};

const CRect TOOLTIP_TEXCOORDS = { 0.0f, 0.0f, 1.0f, 1.0f };

CImVector TooltipWhite() {
    CImVector color;
    color.value = 0xFFFFFFFF;

    return color;
}

bool TooltipColorblind() {
    auto var = CVar::Lookup("colorblindMode");

    return var && var->GetInt() != 0;
}

// ref: FUN_0061a1c0
const char* TooltipColorblindPrefix(int32_t index) {
    if (TooltipColorblind()) {
        return s_colorblindPrefixes[index];
    }

    return "";
}

// ref: FUN_0061a1f0
// The colour of a skill against what it needs: trivial 100 over, easy 50, medium 25, optimal at
// it, red below. `prefix` gets the matching colorblind mark. The reference passes the skill in EAX,
// the requirement in ECX and prefix in EBX.
const CImVector& TooltipSkillColor(int32_t skill, int32_t required, const char** prefix) {
    int32_t index = 4;

    if (skill >= required + 100) {
        index = 0;
    } else if (skill >= required + 50) {
        index = 1;
    } else if (skill >= required + 25) {
        index = 2;
    } else if (required <= skill) {
        index = 3;
    }

    *prefix = TooltipColorblind() ? s_colorblindPrefixes[index] : "";

    return TOOLTIP_COLOR_SKILL[index];
}

// ref: FUN_0061a650
int32_t ScalingStatValuesGetBudget(const ScalingStatValuesRec* values, uint32_t mask) {
    if (!values) {
        return 0;
    }

    switch (mask & 0x4001F) {
        case 0x1: return values->m_shoulderBudget;
        case 0x2: return values->m_trinketBudget;
        case 0x4: return values->m_weaponBudget1H;
        case 0x8: return values->m_primaryBudget;
        case 0x10: return values->m_rangedBudget;
        case 0x40000: return values->m_tertiaryBudget;
        default: return 0;
    }
}

// ref: FUN_0061a6b0
int32_t ScalingStatValuesGetArmor(const ScalingStatValuesRec* values, uint32_t mask) {
    if (!values) {
        return 0;
    }

    switch (mask & 0xF801E0) {
        case 0x20: return values->m_clothShoulderArmor;
        case 0x40: return values->m_leatherShoulderArmor;
        case 0x80: return values->m_mailShoulderArmor;
        case 0x100: return values->m_plateShoulderArmor;
        case 0x80000: return values->m_clothCloakArmor;
        case 0x100000: return values->m_clothChestArmor;
        case 0x200000: return values->m_leatherChestArmor;
        case 0x400000: return values->m_mailChestArmor;
        case 0x800000: return values->m_plateChestArmor;
        default: return 0;
    }
}

// ref: FUN_0061a830
// A heirloom weapon's damage range: its DPS budget, spread 30% each way (20% for two-handers).
void ScalingStatValuesGetDamage(const ScalingStatValuesRec* values, uint32_t mask, float* minDamage, float* maxDamage) {
    if (minDamage) {
        *minDamage = 0.0f;
    }

    if (maxDamage) {
        *maxDamage = 0.0f;
    }

    if (!minDamage || !maxDamage || !values) {
        return;
    }

    float dps = 0.0f;
    float spread = 0.3f;

    switch (mask & 0x7E00) {
        case 0x200: dps = static_cast<float>(values->m_weaponDPS1H); break;
        case 0x400: dps = static_cast<float>(values->m_weaponDPS2H); spread = 0.2f; break;
        case 0x800: dps = static_cast<float>(values->m_spellcasterDPS1H); break;
        case 0x1000: dps = static_cast<float>(values->m_spellcasterDPS2H); spread = 0.2f; break;
        case 0x2000: dps = static_cast<float>(values->m_rangedDPS); break;
        case 0x4000: dps = static_cast<float>(values->m_wandDPS); break;
        default: break;
    }

    *minDamage = dps - dps * spread;
    *maxDamage = (spread + 1.0f) * dps;
}

// ref: FUN_0061a8f0
// The feral attack power a druid gets from a main hand weapon's DPS above 54.8.
int32_t ItemGetFeralAttackPower(const ItemStats_C* info, float averageDamage) {
    if (info->itemClass == 2 && (s_inventoryTypeSlots[info->inventoryType] & 0x8000)) {
        auto dps = averageDamage / (static_cast<float>(info->delay) * 0.001f);

        if (dps > 54.81f) {
            return static_cast<int32_t>(std::floor(static_cast<double>((dps - 54.81f) * 14.0f + 0.5f)));
        }
    }

    return 0;
}

// ref: FUN_0061abf0
// A time in milliseconds as at most two abbreviated units ("1 Hr 5 Min"), through `formatName`.
void TooltipFormatCooldown(char* dest, uint32_t destSize, uint64_t time, const char* formatName) {
    if (!dest || !formatName) {
        return;
    }

    *dest = '\0';

    char unit[256] = {};
    char joined[1024] = {};
    int32_t units = 0;

    if (time >= 86400000) {
        SStrPrintf(unit, sizeof(unit), FrameScript_GetText("DAYS_ABBR", -1, GENDER_NOT_APPLICABLE), static_cast<uint32_t>(time / 86400000));
        SStrPack(joined, unit, sizeof(joined));
        time -= (time / 86400000) * 86400000;
        units = 1;
    }

    if (time >= 3600000) {
        if (joined[0]) {
            SStrPack(joined, FrameScript_GetText("TIME_UNIT_DELIMITER", -1, GENDER_NOT_APPLICABLE), sizeof(joined));
        }

        SStrPrintf(unit, sizeof(unit), FrameScript_GetText("HOURS_ABBR", -1, GENDER_NOT_APPLICABLE), static_cast<uint32_t>(time / 3600000));
        SStrPack(joined, unit, sizeof(joined));
        time -= (time / 3600000) * 3600000;
        units++;
    }

    if (units < 2) {
        if (time >= 60000) {
            if (joined[0]) {
                SStrPack(joined, FrameScript_GetText("TIME_UNIT_DELIMITER", -1, GENDER_NOT_APPLICABLE), sizeof(joined));
            }

            SStrPrintf(unit, sizeof(unit), FrameScript_GetText("MINUTES_ABBR", -1, GENDER_NOT_APPLICABLE), static_cast<uint32_t>(time / 60000));
            SStrPack(joined, unit, sizeof(joined));
            time -= (time / 60000) * 60000;
            units++;
        }

        if (units < 2 && time != 0) {
            if (joined[0]) {
                SStrPack(joined, FrameScript_GetText("TIME_UNIT_DELIMITER", -1, GENDER_NOT_APPLICABLE), sizeof(joined));
            }

            SStrPrintf(unit, sizeof(unit), FrameScript_GetText("SECONDS_ABBR", -1, GENDER_NOT_APPLICABLE), static_cast<uint32_t>(time / 1000));
            SStrPack(joined, unit, sizeof(joined));
        }
    }

    SStrPrintf(dest, destSize, FrameScript_GetText(formatName, -1, GENDER_NOT_APPLICABLE), joined);
}

// ref: FUN_0061b4f0
// A tooltip line as the item dump writes it: language-processed, with &, < and > escaped.
void TooltipXmlEscape(const char* text, char* dest) {
    auto length = static_cast<int32_t>(strlen(text));
    auto processed = LanguageProcess(text);
    int32_t out = 0;

    for (int32_t i = 0; i < length; i++) {
        auto c = processed[i];

        if (c == '<') {
            memcpy(dest + out, "&lt;", 4);
            out += 4;
        } else if (c == '>') {
            memcpy(dest + out, "&gt;", 4);
            out += 4;
        } else if (c == '&') {
            memcpy(dest + out, "&amp;", 5);
            out += 5;
        } else {
            dest[out++] = c;
        }
    }

    dest[out] = '\0';
}

// ref: FUN_00577260
// An enchantment's name with each "$i" replaced by its points; any other "$" token shows as "?".
// False when there was such a token.
bool SpellItemEnchantmentGetText(const SpellItemEnchantmentRec* enchant, char* dest, uint32_t destSize, int32_t points) {
    *dest = '\0';

    auto text = enchant->m_name;
    bool unknown = false;

    for (auto token = SStrChr(text, '$'); token && *token; token = SStrChr(text, '$')) {
        if (token - text != 0) {
            auto length = SStrLen(dest);
            SStrPack(dest, text, destSize);

            auto end = static_cast<uint32_t>(token - text) + static_cast<uint32_t>(length);

            if (end < destSize) {
                dest[end] = '\0';
            }
        }

        text = token + 1;

        if (*text == 'i') {
            char number[32];
            SStrPrintf(number, sizeof(number), "%d", points);
            SStrPack(dest, number, destSize);

            if (*text) {
                text = token + 2;
            }
        } else {
            SStrPack(dest, "?", destSize);
            unknown = true;
        }
    }

    SStrPack(dest, text, destSize);

    return !unknown;
}

// ref: FUN_0061c0b0
// A meta gem's condition lines, each coloured white or gray by whether the unit's equipped gems
// meet it, appended to dest after a newline (always one first when `newlineFirst`). True when all
// the conditions are met -- and when there are none to show.
bool EnchantGetConditionText(CGPlayer_C* unit, const SpellItemEnchantmentRec* enchant, char* dest, uint32_t destSize, bool newlineFirst) {
    *dest = '\0';

    if (!enchant || !enchant->m_srcItemID || !enchant->m_conditionID || !unit) {
        return true;
    }

    auto condition = g_spellItemEnchantmentConditionDB.GetRecord(enchant->m_conditionID);

    if (!condition) {
        return true;
    }

    auto count = [unit](uint32_t type) -> int32_t {
        auto index = static_cast<uint32_t>(s_gemColorIndex[type]);

        return index < 4 ? unit->m_gemColorCounts[index] : 0;
    };

    auto name = [](char* buffer, uint32_t size, uint32_t type) {
        if (type < 5) {
            TooltipCopyText(buffer, size, s_gemColorTokens[type] ? s_gemColorTokens[type] : "", -1, GENDER_NOT_APPLICABLE);
        } else {
            SStrCopy(buffer, FrameScript_GetText("UNKNOWN", -1, GENDER_NOT_APPLICABLE), size);
        }
    };

    bool result = true;

    for (int32_t i = 0; i < 5; i++) {
        bool met = true;

        if (newlineFirst || *dest) {
            SStrPack(dest, "\n", destSize);
        }

        char left[1024];
        char right[1024];
        char text[1024] = {};

        if (condition->m_rtOperandType[i] == 0) {
            auto ltType = condition->m_ltOperandType[i];
            auto value = condition->m_rtOperand[i];
            auto have = count(ltType);
            name(left, sizeof(left), ltType);

            switch (condition->m_operator[i]) {
                case 0:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_EQUAL_VALUE", -1, GENDER_NOT_APPLICABLE), value, left);
                    met = have == value;
                    break;

                case 1:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_NOT_EQUAL_VALUE", -1, GENDER_NOT_APPLICABLE), value, left);
                    met = have != value;
                    break;

                case 3:
                    value++;
                    // Fall through: "more than" is "at least one more".

                case 5:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_MORE_VALUE", -1, GENDER_NOT_APPLICABLE), value, left);
                    met = value <= have;
                    break;

                case 4:
                    value++;
                    // Fall through: "at most" is "less than one more".

                case 2:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_LESS_VALUE", -1, GENDER_NOT_APPLICABLE), value, left);
                    met = have < value;
                    break;

                default:
                    break;
            }
        } else {
            auto ltType = condition->m_ltOperandType[i];
            auto rtType = condition->m_rtOperandType[i];
            auto lt = count(ltType);
            auto rt = count(rtType);
            name(left, sizeof(left), ltType);
            name(right, sizeof(right), rtType);

            switch (condition->m_operator[i]) {
                case 0:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_EQUAL_COMPARE", -1, GENDER_NOT_APPLICABLE), left, right);
                    met = lt == rt;
                    break;

                case 1:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_NOT_EQUAL_COMPARE", -1, GENDER_NOT_APPLICABLE), left, right);
                    met = lt != rt;
                    break;

                case 2:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_MORE_COMPARE", -1, GENDER_NOT_APPLICABLE), right, left);
                    met = rt > lt;
                    break;

                case 3:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_MORE_COMPARE", -1, GENDER_NOT_APPLICABLE), left, right);
                    met = lt > rt;
                    break;

                case 4:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_MORE_EQUAL_COMPARE", -1, GENDER_NOT_APPLICABLE), right, left);
                    met = lt <= rt;
                    break;

                case 5:
                    SStrPrintf(text, sizeof(text), FrameScript_GetText("ENCHANT_CONDITION_MORE_EQUAL_COMPARE", -1, GENDER_NOT_APPLICABLE), left, right);
                    met = rt <= lt;
                    break;

                default:
                    break;
            }
        }

        char line[1024];
        SStrPrintf(line, sizeof(line), "  %s%s%s%s", met ? TOOLTIP_CODE_WHITE : TOOLTIP_CODE_GRAY, FrameScript_GetText("ENCHANT_CONDITION_REQUIRES", -1, GENDER_NOT_APPLICABLE), text, "|r");
        SStrPack(dest, line, destSize);

        result = met && result;

        if (condition->m_logic[i] == 0) {
            return result;
        }
    }

    return result;
}

// ref: FUN_0061e740
// The level a heirloom is shown at: the link's level, else the requester's (unless it is the
// vendor), else the player's; 1 when asked for; clamped between the item's required level and
// its distribution's maximum.
int32_t ItemGetScalingLevel(const ItemStats_C* info, const ITEMLINKINFO* linkInfo, WOWGUID requester, int32_t levelOne) {
    auto distribution = g_scalingStatDistributionDB.GetRecord(info->scalingStatDistribution);
    int32_t level = 0;

    if (!levelOne) {
        if ((!linkInfo || (level = linkInfo->level) == 0) && requester != MerchantGetGUID()) {
            auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(requester, TYPE_UNIT, ".\\Tooltip.cpp", 0xb7f));

            if (unit) {
                level = static_cast<int32_t>(unit->Unit()->level);
            }
        }
    } else {
        level = 1;
    }

    auto player = CGPlayer_C::GetActivePtr();

    if (player && level == 0) {
        level = static_cast<int32_t>(player->Unit()->level);
    }

    auto result = level;

    if (distribution) {
        auto required = info->requiredLevel;
        auto lowest = required > level ? required : level;
        result = distribution->m_maxLevel;

        if (lowest < result) {
            result = level < required ? required : level;
        }
    }

    return result > 1 ? result : 1;
}

// ref: FUN_006276e0
// Whether an equipped item counts as a piece of the set. The first pass takes exact pieces; the
// second takes, in a slot the first left free, an item of the same inventory type as a piece not
// yet matched (a robe standing in for a chest). The second pass counts an item even when no piece
// is left for it, as the reference does.
bool ItemSetMatch(ITEMSETMATCH* match, int32_t pass, int32_t itemID, int32_t slot) {
    WOWGUID none = 0;
    auto info = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(itemID)), &none, &CGTooltip::OnItemArrived, match->tooltip, true);

    if (!info || info->itemSet != match->set->m_ID) {
        return false;
    }

    if (pass == 0) {
        for (int32_t piece = 0; piece < 17; piece++) {
            if (match->matched[piece] == 0 && itemID == match->set->m_itemID[piece]) {
                match->matched[piece] = itemID;
                match->slotUsed[slot] = 1;

                return true;
            }
        }

        return false;
    }

    if (match->slotUsed[slot]) {
        return false;
    }

    for (int32_t piece = 0; piece < 17; piece++) {
        if (match->matched[piece] != 0 || !match->names[piece]) {
            continue;
        }

        auto type = info->inventoryType;
        auto pieceType = static_cast<int32_t>(match->names[piece]->m_inventoryType);

        if (type == pieceType || (type == 5 && pieceType == 20) || (type == 20 && pieceType == 5)) {
            match->matched[piece] = itemID;

            return true;
        }
    }

    return true;
}

// ref: FUN_0061a600
int ItemSetBonusCompare(const void* a, const void* b) {
    auto left = *static_cast<const uint8_t*>(a);
    auto right = *static_cast<const uint8_t*>(b);
    auto leftThreshold = s_sortSet->m_setThreshold[left];
    auto rightThreshold = s_sortSet->m_setThreshold[right];

    if (rightThreshold < leftThreshold) {
        return 1;
    }

    if (leftThreshold < rightThreshold) {
        return -1;
    }

    return left < right ? -1 : 1;
}

// ref: FUN_006dc550
bool ItemSetMeetsSkill(const ItemSetRec* set) {
    if (set->m_requiredSkill == 0) {
        return true;
    }

    auto player = CGPlayer_C::GetActivePtr();
    auto index = player ? player->GetSkillIndex(set->m_requiredSkill) : -1;

    return index >= 0 && set->m_requiredSkillRank <= static_cast<int32_t>(player->GetSkillRank(index));
}

// ref: FUN_005b9430
const char* HolidayGetName(int32_t holidayID) {
    auto holiday = g_holidaysDB.GetRecord(holidayID);

    if (holiday) {
        auto name = g_holidayNamesDB.GetRecord(holiday->m_holidayNameID);

        if (name) {
            return name->m_name;
        }
    }

    return nullptr;
}

// ref: FUN_00634910
const char* StringLookupsGetString(int32_t id) {
    auto rec = g_stringLookupsDB.GetRecord(id);

    return rec ? rec->m_string : nullptr;
}

// ref: FUN_00514030
// An ItemSubClass row by its position; the table has no id column.
const ItemSubClassRec* ItemSubClassGetByIndex(int32_t index) {
    return g_itemSubClassDB.GetRecordByIndex(index);
}

// The row for a class and subclass, the way the reference walks it.
const ItemSubClassRec* ItemSubClassFind(int32_t itemClass, int32_t subClass) {
    for (int32_t i = 0; i < g_itemSubClassDB.GetNumRecords(); i++) {
        auto row = g_itemSubClassDB.GetRecordByIndex(i);

        if (row && row->m_classID == itemClass && row->m_subClassID == subClass) {
            return row;
        }
    }

    return nullptr;
}

// A skill requirement line: "Requires <skill>" or "Requires <skill> (<rank>)".
void TooltipFormatSkillRequirement(char* dest, uint32_t destSize, const char* reqToken, const char* minToken, int32_t skillLine, int32_t rank) {
    auto skill = g_skillLineDB.GetRecord(skillLine);
    auto skillName = skill ? skill->m_displayName : "UNKNOWN";
    char format[1024];

    SStrCopy(format, FrameScript_GetText(rank == 0 ? reqToken : minToken, -1, GENDER_NOT_APPLICABLE), sizeof(format));

    if (rank == 0) {
        SStrPrintf(dest, destSize, format, skillName);
    } else {
        SStrPrintf(dest, destSize, format, skillName, rank);
    }
}

// The time left of a refund or a trade window: an hour and some minutes as two units, else the
// largest unit that fits.
void TooltipFormatWindowTime(char* dest, uint32_t destSize, int32_t left) {
    auto seconds = left + 7200;

    if (static_cast<uint32_t>(left + 3540) < 3540) {
        auto minutes = (left + 3600) / 60;
        char hours[4096];
        char mins[256];

        SStrPrintf(hours, sizeof(hours), FrameScript_GetText("INT_SPELL_DURATION_HOURS", 1, GENDER_NOT_APPLICABLE), 1);
        SStrPrintf(mins, sizeof(mins), FrameScript_GetText("INT_SPELL_DURATION_MIN", minutes, GENDER_NOT_APPLICABLE), minutes);
        SStrPrintf(dest, destSize, "%s%s%s", hours, FrameScript_GetText("TIME_UNIT_DELIMITER", -1, GENDER_NOT_APPLICABLE), mins);
    } else {
        FormatTimeInterval(dest, destSize, static_cast<uint64_t>(static_cast<int64_t>(seconds)), "INT_SPELL_DURATION", nullptr, 0, true);
    }
}

} // namespace

// ref: FUN_00623760
int32_t CGTooltip::SetItemRetrieving() {
    this->AddLine(FrameScript_GetText("RETRIEVING_ITEM_INFO", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);

    if (this->m_onTooltipSetItem.luaRef) {
        this->RunScript(this->m_onTooltipSetItem, 0, nullptr);
    }

    this->Show();
    this->CalculateSize();

    return 0;
}

// ref: FUN_00623590
// The reagents a spell takes, as "<format>: a, b (2), c" with each one the player lacks enough of
// in red. Their item records are asked for, refreshing the tooltip when they come.
void CGTooltip::AddSpellReagents(const SpellRec* spell, const char* format, DBCACHECALLBACKFN callback) {
    char list[4096];
    bool first = true;

    for (int32_t i = 0; i < 8; i++) {
        if (spell->m_reagent[i] <= 0) {
            continue;
        }

        auto requester = TooltipSpellRequester(spell->m_ID);
        auto info = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(spell->m_reagent[i])), &requester, callback, this, true);

        if (!info) {
            continue;
        }

        if (!first) {
            SStrPack(list, ", ", sizeof(list));
        } else {
            first = false;
            list[0] = '\0';
        }

        char name[1024];

        if (spell->m_reagentCount[i] < 2) {
            SStrCopy(name, info->name.c_str(), sizeof(name));
        } else {
            SStrPrintf(name, sizeof(name), "%s (%d)", info->name.c_str(), spell->m_reagentCount[i]);
        }

        auto player = CGPlayer_C::GetActivePtr();

        if (!player || player->m_bag.CountItem(spell->m_reagent[i], 0) < spell->m_reagentCount[i]) {
            SStrPack(list, TOOLTIP_CODE_RED, sizeof(list));
            SStrPack(list, name, sizeof(list));
            SStrPack(list, "|r", sizeof(list));
        } else {
            SStrPack(list, name, sizeof(list));
        }
    }

    if (!first) {
        char line[4096];
        SStrPrintf(line, sizeof(line), FrameScript_GetText(format, -1, GENDER_NOT_APPLICABLE), list);
        this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 1);
    }
}

// ref: FUN_00626650
// The record a tooltip was waiting on arrived: fill it again from what it was asked for. A refill
// passes the stored "no price" flag where the reference's call puts it -- in the "no lock" slot --
// so the refill shows the price again; kept as the reference has it.
void CGTooltip::OnItemArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    if (!found) {
        return;
    }

    auto tooltip = static_cast<CGTooltip*>(param);

    if (tooltip->m_spellID) {
        tooltip->SetSpell(tooltip->m_spellID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0);
        tooltip->CalculateSize();

        return;
    }

    if (tooltip->m_itemID) {
        WOWGUID requester = 0;
        auto item = tooltip->m_itemGUID;

        tooltip->SetItem(tooltip->m_itemID, &requester, &item, tooltip->m_itemNameOnly, tooltip->m_itemCompareSlot, tooltip->m_useLinkInfo, 0, tooltip->m_itemOwner, tooltip->m_itemSocketPreview, nullptr, tooltip->m_noCharges, nullptr, tooltip->m_levelOne, tooltip->m_noPrice, 0);
        tooltip->CalculateSize();
    }
}

// ref: FUN_006277f0
// An item's tooltip: the item object `itemGUID` names, or the stored link info when `useLinkInfo`,
// else the bare entry. Returns whether the tooltip shows something that changes with time (an
// expiry, a cooldown, an enchantment running out), so its owner refreshes it.
int32_t CGTooltip::SetItem(int32_t itemID, const WOWGUID* requester, const WOWGUID* itemGUID, int32_t nameOnly, int32_t compareSlot, int32_t useLinkInfo, int32_t append, WOWGUID owner, int32_t socketPreview, FILE* dump, int32_t noCharges, const ITEMLINKINFO* linkInfo, int32_t levelOne, int32_t noLock, int32_t noPrice) {
    if (!append) {
        this->ClearTooltip();
    }

    auto player = CGPlayer_C::GetActivePtr();

    if (!player) {
        this->Hide();

        return 0;
    }

    CGPlayer_C* ownerPlayer = nullptr;

    if (owner) {
        ownerPlayer = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(owner, TYPE_PLAYER, ".\\Tooltip.cpp", 0xbaf));
    }

    if (!append) {
        this->m_itemGUID = *itemGUID;
        this->m_itemID = itemID;
        this->m_useLinkInfo = useLinkInfo;
        this->m_itemNameOnly = nameOnly;
        this->m_itemSocketPreview = socketPreview;
        this->m_itemCompareSlot = compareSlot;
        this->m_itemOwner = owner;
        this->m_noCharges = noCharges;
        this->m_levelOne = levelOne;
        this->m_noPrice = noPrice;
    }

    auto info = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(itemID)), requester, &CGTooltip::OnItemArrived, this, true);

    if (!info) {
        return this->SetItemRetrieving();
    }

    auto playerData = player->Unit();
    auto playerLevel = static_cast<int32_t>(playerData->level);
    auto playerRace = static_cast<uint32_t>(playerData->bytes0 & 0xFF);
    auto playerClass = static_cast<uint32_t>((playerData->bytes0 >> 8) & 0xFF);

    bool isBag = info->inventoryType == 18;
    int32_t result = 0;
    auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(*itemGUID, TYPE_ITEM, ".\\Tooltip.cpp", 0xbc6));
    auto itemData = item ? item->Item() : nullptr;
    bool wrapped = (item && ((itemData->flags >> 3) & 1)) || (this->m_useLinkInfo && this->m_linkInfo.wrapped);

    // The random property or suffix, and for a link the enchantments it brings.
    int32_t suffix = 0;
    int32_t useEnchantSpell = 0;
    int32_t useEnchantCharges = 0;
    const SpellItemEnchantmentRec* useEnchant = nullptr;

    if (!wrapped) {
        if (!this->m_useLinkInfo || (suffix = this->m_linkInfo.randomPropertyID) == 0) {
            if (item) {
                suffix = itemData->randomPropertiesID;
            }
        } else if (suffix < 1) {
            if (auto randomSuffix = g_itemRandomSuffixDB.GetRecord(-suffix)) {
                for (int32_t i = 0; i < 5; i++) {
                    this->m_linkInfo.enchant[7 + i] = randomSuffix->m_enchantment[i];
                }
            }
        } else if (auto property = g_itemRandomPropertiesDB.GetRecord(suffix)) {
            for (int32_t i = 0; i < 5; i++) {
                this->m_linkInfo.enchant[7 + i] = property->m_enchantment[i];
            }
        }
    }

    // The comparison header.
    bool destroyGem = false;

    if (compareSlot) {
        if (info->gemProperties == 0) {
            this->AddLine(FrameScript_GetText("CURRENTLY_EQUIPPED", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_GRAY, TOOLTIP_COLOR_GRAY, 0);
        } else {
            this->AddLine(FrameScript_GetText("DESTROY_GEM", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);
            destroyGem = true;
        }
    }

    // The name.
    char name[1024];
    char text[1024];
    char line[4096];
    char right[1024];
    ItemNameFromEntry(name, sizeof(name), itemID, suffix);

    if (append) {
        SStrCopy(text, name, sizeof(text));
        SStrPrintf(name, sizeof(name), "\n%s", text);
    }

    if (!nameOnly) {
        auto& color = destroyGem ? TOOLTIP_COLOR_GRAY : TOOLTIP_COLOR_QUALITY[info->quality];
        this->AddLine(name, nullptr, color, color, 0);

        if (TooltipColorblind()) {
            const char* quality;

            if (!(info->flags & 0x8)) {
                SStrPrintf(text, sizeof(text), "ITEM_QUALITY%d_DESC", destroyGem ? 0 : info->quality);
                quality = FrameScript_GetText(text, -1, GENDER_NOT_APPLICABLE);
            } else {
                quality = FrameScript_GetText("ITEM_HEROIC_EPIC", -1, GENDER_NOT_APPLICABLE);
            }

            this->AddLine(quality, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        } else if (info->flags & 0x8) {
            this->AddLine(FrameScript_GetText("ITEM_HEROIC", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 0);
        }
    } else {
        this->AddLine(name, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
    }

    // A charter: its petition's title, creator and signatures.
    if (item && item->IsCharter()) {
        auto petitionID = item->IsCharter() ? itemData->enchantments[0].id : 0;
        auto petition = g_petitionCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(petitionID)), itemGUID, &CGTooltip::OnItemArrived, this, true);

        if (!petition) {
            return 0;
        }

        SStrPrintf(line, sizeof(line), FrameScript_GetText(petition->m_type == 0 ? "GUILD_CHARTER_TITLE" : "PETITION_TITLE", -1, GENDER_NOT_APPLICABLE), petition->m_title);
        this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 1);

        auto creator = g_nameCache.GetRecord(DBCACHEKEY64(petition->m_creator), &petition->m_creator, &CGTooltip::OnItemArrived, this, true);

        if (creator) {
            SStrPrintf(line, sizeof(line), FrameScript_GetText(petition->m_type == 0 ? "GUILD_CHARTER_CREATOR" : "PETITION_CREATOR", -1, GENDER_NOT_APPLICABLE), creator->m_name);
            this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        }

        // FUN_0061dc90: the signature count rides in the first enchantment's duration.
        auto signatures = item->IsCharter() ? itemData->enchantments[0].expiration : 0;

        if (signatures) {
            SStrPrintf(line, sizeof(line), FrameScript_GetText("PETITION_NUM_SIGNATURES", signatures, GENDER_NOT_APPLICABLE), signatures);
            this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        }
    }

    if (info->flags & 0x2000) {
        this->AddLine(FrameScript_GetText("ITEM_SIGNABLE", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 0);
    }

    if (!nameOnly && !socketPreview) {
        // Where it may be used.
        if (info->area) {
            if (auto area = g_areaTableDB.GetRecord(info->area)) {
                this->AddLine(area->m_areaName, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
            }
        }

        if (info->map) {
            if (auto map = g_mapDB.GetRecord(info->map)) {
                this->AddLine(map->m_mapName, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
            }
        }

        if (info->flags & 0x2) {
            TooltipCopyText(text, sizeof(text), "ITEM_CONJURED", -1, GENDER_NOT_APPLICABLE);
            this->AddLine(text, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        }

        // How it binds.
        line[0] = '\0';

        if (item && item->IsSoulbound()) {
            if (info->bonding == 4) {
                TooltipCopyText(line, sizeof(line), "ITEM_BIND_QUEST", -1, GENDER_NOT_APPLICABLE);
            } else if (info->itemClass != 10) {
                TooltipCopyText(line, sizeof(line), (info->flags & 0x8000000) ? "ITEM_ACCOUNTBOUND" : "ITEM_SOULBOUND", -1, GENDER_NOT_APPLICABLE);
            }
        }

        bool showBind = true;

        if (info->bonding != 0 && line[0] == '\0') {
            if (info->itemClass != 10 || *requester == LootGetGUID()) {
                if (info->flags & 0x8000000) {
                    TooltipCopyText(line, sizeof(line), "ITEM_BIND_TO_ACCOUNT", -1, GENDER_NOT_APPLICABLE);
                } else {
                    static const char* const bindings[4] = { "ITEM_BIND_ON_PICKUP", "ITEM_BIND_ON_EQUIP", "ITEM_BIND_ON_USE", "ITEM_BIND_QUEST" };

                    if (static_cast<uint32_t>(info->bonding - 1) <= 3) {
                        TooltipCopyText(line, sizeof(line), bindings[info->bonding - 1], -1, GENDER_NOT_APPLICABLE);
                    } else {
                        showBind = false;
                    }
                }
            } else {
                showBind = false;
            }
        }

        if (showBind && line[0]) {
            this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        }

        // How many may be carried.
        if (info->itemClass != 10) {
            if (info->flags & 0x80000) {
                TooltipCopyText(line, sizeof(line), "ITEM_UNIQUE_EQUIPPABLE", -1, GENDER_NOT_APPLICABLE);
                this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
            } else if (info->maxCount > 0) {
                if (info->maxCount == 1) {
                    TooltipCopyText(line, sizeof(line), "ITEM_UNIQUE", -1, GENDER_NOT_APPLICABLE);
                } else {
                    TooltipCopyText(text, sizeof(text), "ITEM_UNIQUE_MULTIPLE", -1, GENDER_NOT_APPLICABLE);
                    SStrPrintf(line, sizeof(line), text, info->maxCount);
                }

                this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
            } else if (info->itemLimitCategory) {
                // The reference reads the row without checking it exists.
                if (auto category = g_itemLimitCategoryDB.GetRecord(info->itemLimitCategory)) {
                    TooltipCopyText(text, sizeof(text), (category->m_flags & 1) ? "ITEM_LIMIT_CATEGORY_MULTIPLE" : "ITEM_LIMIT_CATEGORY", -1, GENDER_NOT_APPLICABLE);
                    SStrPrintf(line, sizeof(line), text, category->m_name, category->m_quantity);
                    this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
                }
            }
        }

        if (info->startQuest) {
            TooltipCopyText(line, sizeof(line), "ITEM_STARTS_QUEST", -1, GENDER_NOT_APPLICABLE);
            this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        }

        // The lock, and what opens it.
        const LockRec* lock = nullptr;

        if (!noLock) {
            lock = g_lockDB.GetRecord(info->lockID);

            if (!lock && this->m_useLinkInfo && this->m_linkInfo.lockID) {
                lock = g_lockDB.GetRecord(this->m_linkInfo.lockID);
            }

            if (lock && item && ((itemData->flags >> 2) & 1)) {
                lock = nullptr;
            }
        }

        if (lock) {
            int32_t spellID = 0;
            int32_t skill = 0;
            int32_t required = 0;
            int32_t lockType = 0;
            const char* prefix = "";
            CImVector color;

            CGItem_C::CheckLock(lock, info->itemLevel, &spellID, &skill, &required, &lockType, nullptr, nullptr);

            if (spellID == 0) {
                color = TOOLTIP_COLOR_SKILL[1];
                prefix = TooltipColorblindPrefix(1);
            } else {
                color = TooltipSkillColor(skill, required, &prefix);
            }

            if (lockType == 0) {
                for (int32_t i = 0; i < 8; i++) {
                    if (lock->m_index[i] == 20) {
                        lockType = 20;
                        break;
                    }
                }
            }

            SStrPrintf(line, sizeof(line), "%s%s", prefix, FrameScript_GetText(lockType == 20 ? "ENCRYPTED" : "LOCKED", -1, GENDER_NOT_APPLICABLE));
            this->AddLine(line, nullptr, color, color, 0);

            for (int32_t i = 0; i < 8; i++) {
                auto type = lock->m_type[i];

                if (type == 2) {
                    int32_t lockSpell = 0;
                    int32_t lockSkill = 0;
                    int32_t lockRequired = 0;
                    CGItem_C::CheckLock(lock, info->itemLevel, &lockSpell, &lockSkill, &lockRequired, nullptr, nullptr, nullptr);

                    if (lockSpell) {
                        const char* skillPrefix;
                        auto skillColor = TooltipSkillColor(lockSkill, lockRequired, &skillPrefix);
                        char format[128];
                        SStrCopy(format, FrameScript_GetText("ITEM_MIN_SKILL", -1, GENDER_NOT_APPLICABLE), sizeof(format));
                        auto lockTypeRec = g_lockTypeDB.GetRecord(lock->m_index[i]);
                        SStrPrintf(line, sizeof(line), format, lockTypeRec ? lockTypeRec->m_name : "UNKNOWN", lock->m_skill[i]);
                        this->AddLine(line, nullptr, skillColor, skillColor, 0);
                    }

                    break;
                }

                if (type == 3) {
                    if (auto spell = g_spellDB.GetRecord(lock->m_index[i])) {
                        this->AddSpellReagents(spell, "LOCKED_WITH_SPELL", &CGTooltip::OnItemArrived);
                    }

                    break;
                }

                if (type == 1) {
                    WOWGUID keyRequester = item ? item->GetGUID() : 0;
                    auto key = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(lock->m_index[i])), &keyRequester, &CGTooltip::OnItemArrived, this, true);

                    if (key) {
                        // FUN_004fd200: the first of the record's names.
                        SStrPrintf(line, sizeof(line), FrameScript_GetText("LOCKED_WITH_ITEM", -1, GENDER_NOT_APPLICABLE), key->names[0].c_str());
                        this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
                    }

                    break;
                }
            }

            result = 1;
        }
    }

    // A heirloom's scaling.
    const ScalingStatValuesRec* scalingValues = nullptr;
    const ScalingStatDistributionRec* distribution = nullptr;
    int32_t scalingLevel = 0;

    if (info->scalingStatValue != 0) {
        distribution = g_scalingStatDistributionDB.GetRecord(info->scalingStatDistribution);
        scalingLevel = ItemGetScalingLevel(info, linkInfo, *requester, levelOne);
        this->m_scalingLevel = scalingLevel;
        scalingValues = g_scalingStatValuesDB.GetRecordByIndex(scalingLevel - 1);
    }

    // The slot and subclass.
    auto subclass = ItemSubClassFind(info->itemClass, info->subClass);
    right[0] = '\0';

    if (!isBag) {
        bool glyph = false;

        if (info->itemClass == 16) {
            glyph = true;

            for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
                auto spell = g_spellDB.GetRecord(info->spellID[i]);

                if (!spell) {
                    continue;
                }

                for (int32_t e = 0; e < 3; e++) {
                    if (spell->m_effect[e] != 74) {
                        continue;
                    }

                    if (auto glyphRec = g_glyphPropertiesDB.GetRecord(spell->m_effectMiscValue[e])) {
                        auto token = glyphRec->m_glyphSlotFlags == 0 ? "MAJOR_GLYPH" : "MINOR_GLYPH";
                        this->AddLine(FrameScript_GetText(token, -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_GLYPH, TOOLTIP_COLOR_GLYPH, 0);
                    }
                }
            }
        }

        if (!glyph) {
            if (subclass && info->inventoryType != 16 && !(subclass->m_displayFlags & 1) && subclass->m_displayName && subclass->m_displayName[0]) {
                SStrCopy(right, subclass->m_displayName, sizeof(right));
            }

            if (info->itemClass == 6) {
                auto itemClassRec = g_itemClassDB.GetRecord(6);

                if (itemClassRec && itemClassRec->m_name && itemClassRec->m_name[0]) {
                    SStrCopy(name, itemClassRec->m_name, sizeof(name));
                } else {
                    name[0] = '\0';
                }
            } else {
                auto slot = FrameScript_GetText(s_inventoryTypeTokens[info->inventoryType], -1, GENDER_NOT_APPLICABLE);
                SStrCopy(name, slot, sizeof(name));

                if (*slot == '\0') {
                    name[0] = '\0';
                }
            }

            bool subclassRed = false;
            bool slotRed = false;
            auto proficiency = CGPlayer_C::GetItemProficiency(static_cast<uint8_t>(info->itemClass));

            if (proficiency && !(proficiency & (1u << (info->subClass & 0x1F)))) {
                if (info->itemClass == 2) {
                    if (subclass->m_prerequisiteProficiency == -1) {
                        if (subclass->m_postrequisiteProficiency == -1 || !(proficiency & (1u << (subclass->m_postrequisiteProficiency & 0x1F)))) {
                            subclassRed = true;
                        } else {
                            slotRed = true;
                        }
                    } else if (!(proficiency & (1u << (subclass->m_prerequisiteProficiency & 0x1F)))) {
                        subclassRed = true;
                    } else {
                        slotRed = true;
                    }
                } else {
                    const ChrClassesRec* classRec = nullptr;

                    if (scalingValues && distribution && info->itemClass == 4 && (info->subClass == 4 || info->subClass == 3)) {
                        classRec = g_chrClassesDB.GetRecord(playerClass);
                    }

                    int32_t shownSubClass = -1;

                    if (classRec) {
                        if (info->subClass == 4 && (classRec->m_flags & 0x20)) {
                            shownSubClass = static_cast<int32_t>(((proficiency & 8) | 0x10) >> 3);
                        } else if (info->subClass == 3 && (classRec->m_flags & 0x10) && (proficiency & 4)) {
                            shownSubClass = 2;
                        }
                    }

                    if (shownSubClass < 0) {
                        subclassRed = true;
                    } else {
                        for (int32_t i = 0; i < g_itemSubClassDB.GetNumRecords(); i++) {
                            auto row = ItemSubClassGetByIndex(i);

                            if (row && row->m_classID == 4 && row->m_subClassID == shownSubClass) {
                                if (row->m_displayName && row->m_displayName[0]) {
                                    SStrCopy(right, row->m_displayName, sizeof(right));
                                }

                                break;
                            }
                        }
                    }
                }
            }

            if (info->inventoryType == 22) {
                slotRed = !SpellBookCanDualWield() ? true : slotRed;
            }

            if (name[0] == '\0') {
                if (right[0]) {
                    auto& color = subclassRed ? TOOLTIP_COLOR_RED : TOOLTIP_COLOR_HIGHLIGHT;
                    this->AddLine(right, nullptr, color, color, 0);
                }
            } else {
                auto& rightColor = subclassRed ? TOOLTIP_COLOR_RED : TOOLTIP_COLOR_HIGHLIGHT;
                auto& leftColor = slotRed ? TOOLTIP_COLOR_RED : TOOLTIP_COLOR_HIGHLIGHT;
                this->AddLine(name, right[0] ? right : nullptr, leftColor, rightColor, 0);
            }
        }
    } else if (subclass && subclass->m_displayName && subclass->m_displayName[0]) {
        SStrCopy(text, FrameScript_GetText("CONTAINER_SLOTS", -1, GENDER_NOT_APPLICABLE), sizeof(text));
        SStrPrintf(name, sizeof(name), text, info->containerSlots, subclass->m_displayName);
        this->AddLine(name, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
    }

    float averageDamage = 0.0f;
    bool allGemsFit = true;

    if (!nameOnly) {
        // Damage, speed and DPS.
        bool showDps = false;

        if (!(info->scalingStatValue & 0x7E00) || !scalingValues) {
            bool first = true;

            for (int32_t d = 0; d < ItemStats_C::MAX_DAMAGES; d++) {
                double minDamage = info->damageMin[d];
                double maxDamage = info->damageMax[d];
                auto low = static_cast<int32_t>(minDamage > 0.0 ? minDamage : minDamage - 0.99995f);
                auto high = static_cast<int32_t>(maxDamage > 0.0 ? maxDamage + 0.99995f : maxDamage);

                if (low == 0 && high == 0) {
                    continue;
                }

                char format[1024];

                if (info->damageType[d] == 0) {
                    if (info->itemClass == 6) {
                        auto average = static_cast<float>(high + low) * 0.5f;
                        SStrCopy(format, FrameScript_GetText(first ? "AMMO_DAMAGE_TEMPLATE" : "PLUS_AMMO_DAMAGE_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(format));
                        SStrPrintf(name, sizeof(name), format, static_cast<double>(average));
                    } else if (low == high) {
                        SStrCopy(format, FrameScript_GetText(first ? "SINGLE_DAMAGE_TEMPLATE" : "PLUS_SINGLE_DAMAGE_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(format));
                        SStrPrintf(name, sizeof(name), format, low);
                    } else {
                        SStrCopy(format, FrameScript_GetText(first ? "DAMAGE_TEMPLATE" : "PLUS_DAMAGE_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(format));
                        SStrPrintf(name, sizeof(name), format, low, high);
                    }
                } else {
                    char school[4096];
                    SStrPrintf(format, sizeof(format), "SPELL_SCHOOL%d_CAP", info->damageType[d]);
                    SStrCopy(school, FrameScript_GetText(format, -1, GENDER_NOT_APPLICABLE), sizeof(school));

                    if (info->itemClass == 6) {
                        auto average = static_cast<float>(high + low) * 0.5f;
                        SStrCopy(format, FrameScript_GetText(first ? "AMMO_SCHOOL_DAMAGE_TEMPLATE" : "PLUS_AMMO_SCHOOL_DAMAGE_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(format));
                        SStrPrintf(name, sizeof(name), format, static_cast<double>(average), school);
                    } else if (low == high) {
                        SStrCopy(format, FrameScript_GetText(first ? "SINGLE_DAMAGE_TEMPLATE_WITH_SCHOOL" : "PLUS_SINGLE_DAMAGE_TEMPLATE_WITH_SCHOOL", -1, GENDER_NOT_APPLICABLE), sizeof(format));
                        SStrPrintf(name, sizeof(name), format, low, school);
                    } else {
                        SStrCopy(format, FrameScript_GetText(first ? "DAMAGE_TEMPLATE_WITH_SCHOOL" : "PLUS_DAMAGE_TEMPLATE_WITH_SCHOOL", -1, GENDER_NOT_APPLICABLE), sizeof(format));
                        SStrPrintf(name, sizeof(name), format, low, high, school);
                    }
                }

                if (first && info->itemClass == 2) {
                    SStrCopy(format, FrameScript_GetText("SPEED", -1, GENDER_NOT_APPLICABLE), sizeof(format));
                    SStrPrintf(right, sizeof(right), "%s %.2f", format, static_cast<double>(static_cast<float>(info->delay) * 0.001f));
                } else {
                    right[0] = '\0';
                }

                this->AddLine(name, right, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);

                first = false;
                averageDamage = (info->damageMin[d] + info->damageMax[d]) * 0.5f + averageDamage;
            }

            showDps = !first;
        } else {
            float minDamage;
            float maxDamage;
            ScalingStatValuesGetDamage(scalingValues, info->scalingStatValue, &minDamage, &maxDamage);

            if (info->delay != 0) {
                minDamage = minDamage * static_cast<float>(info->delay) * 0.001f;
                maxDamage = static_cast<float>(info->delay) * maxDamage * 0.001f;
            }

            averageDamage = (minDamage + maxDamage) * 0.5f;

            char format[1024];
            SStrCopy(format, FrameScript_GetText("DAMAGE_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(format));
            SStrPrintf(name, sizeof(name), format, static_cast<int32_t>(minDamage), static_cast<int32_t>(maxDamage));
            SStrCopy(format, FrameScript_GetText("SPEED", -1, GENDER_NOT_APPLICABLE), sizeof(format));
            SStrPrintf(right, sizeof(right), "%s %.2f", format, static_cast<double>(static_cast<float>(info->delay) * 0.001f));
            this->AddLine(name, right, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);

            showDps = true;
        }

        if (showDps && info->itemClass == 2) {
            char format[1024];
            SStrCopy(format, FrameScript_GetText("DPS_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(format));
            SStrPrintf(name, sizeof(name), format, static_cast<double>(averageDamage / (static_cast<float>(info->delay) * 0.001f)));
            this->AddLine(name, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        }

        // Armor and block.
        if (!(info->scalingStatValue & 0xF801E0)) {
            if (info->armor > 0) {
                SStrCopy(text, FrameScript_GetText("ARMOR_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(text));
                SStrPrintf(line, sizeof(line), text, info->armor);
                auto& color = info->armorDamageModifier > 0.0f ? TOOLTIP_COLOR_GREEN : TOOLTIP_COLOR_HIGHLIGHT;
                this->AddLine(line, nullptr, color, color, 0);
            }
        } else {
            auto armor = ScalingStatValuesGetArmor(scalingValues, info->scalingStatValue);
            SStrCopy(text, FrameScript_GetText("ARMOR_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(text));
            SStrPrintf(line, sizeof(line), text, armor);
            this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        }

        if (info->block > 0) {
            SStrCopy(text, FrameScript_GetText("SHIELD_BLOCK_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(text));
            SStrPrintf(line, sizeof(line), text, info->block);
            this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        }

        // The primary stats, in the reference's order.
        auto addStat = [&](int32_t type, int32_t value) {
            text[0] = '\0';
            SStrCopy(text, FrameScript_GetText(s_itemModTokens[type], -1, GENDER_NOT_APPLICABLE), sizeof(text));

            if (text[0]) {
                SStrPrintf(line, sizeof(line), text, value < 1 ? '-' : '+', std::abs(value));
                this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 1);
            }
        };

        if (info->scalingStatValue == 0) {
            for (auto type : s_primaryStatOrder) {
                for (int32_t i = 0; i < ItemStats_C::MAX_STATS; i++) {
                    if (info->statValue[i] != 0 && info->statType[i] != -1 && info->statType[i] == type) {
                        addStat(type, info->statValue[i]);
                        break;
                    }
                }
            }
        } else if (distribution) {
            auto budget = scalingValues ? ScalingStatValuesGetBudget(scalingValues, info->scalingStatValue) : 0;

            for (auto type : s_primaryStatOrder) {
                for (int32_t i = 0; i < 10; i++) {
                    auto value = distribution->m_bonus[i] * budget / 10000;

                    if (distribution->m_bonus[i] != 0 && value != 0 && distribution->m_statID[i] == type) {
                        addStat(type, value);
                        break;
                    }
                }
            }
        }

        // Resistances: one "all" line when the five schools match holy, else each school's.
        bool allEqual = true;

        for (int32_t school = 2; school <= 6; school++) {
            if (info->resistance[school - 1] != info->resistance[0]) {
                allEqual = false;
                break;
            }
        }

        if (allEqual) {
            if (info->resistance[0] != 0) {
                SStrCopy(text, FrameScript_GetText("ITEM_RESIST_ALL", -1, GENDER_NOT_APPLICABLE), sizeof(text));
                auto value = info->resistance[0];
                SStrPrintf(line, sizeof(line), text, value < 1 ? '-' : '+', std::abs(value));
                this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
            }
        } else {
            for (int32_t school = 2; school <= 6; school++) {
                auto value = info->resistance[school - 1];

                if (!value) {
                    continue;
                }

                char schoolName[32];
                SStrPrintf(text, sizeof(text), "SPELL_SCHOOL%d_CAP", school);
                SStrCopy(schoolName, FrameScript_GetText(text, -1, GENDER_NOT_APPLICABLE), sizeof(schoolName));
                SStrCopy(text, FrameScript_GetText("ITEM_RESIST_SINGLE", -1, GENDER_NOT_APPLICABLE), sizeof(text));
                SStrPrintf(line, sizeof(line), text, value < 1 ? '-' : '+', std::abs(value), schoolName);
                this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
            }
        }

        // Sockets and enchantments.
        int32_t socketCount = 0;

        if (!item) {
            for (int32_t i = 0; i < ItemStats_C::MAX_SOCKETS; i++) {
                if (info->socketColor[i] != 0) {
                    socketCount++;
                }
            }

            if (this->m_useLinkInfo && this->m_linkInfo.enchant[6]) {
                if (auto prismatic = g_spellItemEnchantmentDB.GetRecord(this->m_linkInfo.enchant[6])) {
                    for (int32_t e = 0; e < 3; e++) {
                        if (prismatic->m_effect[e] == 8) {
                            socketCount += prismatic->m_effectPointsMin[e];
                            break;
                        }
                    }
                }
            }
        } else {
            socketCount = item->GetSocketCount();
        }

        auto addEmptySocket = [&](uint32_t color, const CImVector& lineColor) {
            for (int32_t bit = 0; bit < 4; bit++) {
                if (!(color & (1u << bit))) {
                    continue;
                }

                char upper[32];
                char key[1024];
                SStrCopy(upper, s_socketNames[bit], sizeof(upper));
                SStrUpper(upper);
                SStrPrintf(key, sizeof(key), "%s%s", "EMPTY_SOCKET_", upper);
                SStrPrintf(line, sizeof(line), FrameScript_GetText(key, -1, GENDER_NOT_APPLICABLE));
                this->AddLine(line, nullptr, lineColor, lineColor, 0);
                SStrPrintf(line, sizeof(line), "Interface\\ItemSocketingFrame\\UI-EmptySocket-%s.blp", s_socketNames[bit]);
                this->AddTexture(line, TOOLTIP_TEXCOORDS, TooltipWhite());

                return;
            }
        };

        if (!wrapped) {
            if (!item && !this->m_useLinkInfo) {
                for (int32_t socket = 0; socket < ItemStats_C::MAX_SOCKETS; socket++) {
                    if (info->socketColor[socket] != 0) {
                        addEmptySocket(static_cast<uint32_t>(info->socketColor[socket]), TOOLTIP_COLOR_GRAY);
                    }
                }

                if (info->socketBonus) {
                    if (auto bonus = g_spellItemEnchantmentDB.GetRecord(std::abs(info->socketBonus))) {
                        SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_SOCKET_BONUS", -1, GENDER_NOT_APPLICABLE), bonus->m_name);
                        this->AddLine(line, nullptr, TOOLTIP_COLOR_GRAY, TOOLTIP_COLOR_GRAY, 0);
                    }
                }

                if (info->randomProperty || info->randomSuffix) {
                    this->AddLine(FrameScript_GetText("ITEM_RANDOM_ENCHANT", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 0);
                }
            } else {
                for (int32_t slot = 0; slot < 12; slot++) {
                    int32_t enchantID;

                    if (!item) {
                        enchantID = this->m_linkInfo.enchant[slot];
                    } else {
                        enchantID = item->IsCharter() ? 0 : itemData->enchantments[slot].id;
                    }

                    auto socket = static_cast<uint32_t>(slot - 2);
                    bool hasSocket = socket <= 2 && static_cast<int32_t>(socket) < socketCount;

                    // Socketing preview: the gem placed in the socketing frame stands in.
                    if (socket < 3 && socketPreview) {
                        auto gemItem = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(s_socketGems[socket], TYPE_ITEM, ".\\Tooltip.cpp", 0xe64));
                        auto current = g_spellItemEnchantmentDB.GetRecord(std::abs(enchantID));

                        if (!gemItem) {
                            if (!current && info->socketColor[socket] != 0) {
                                allGemsFit = false;
                            }
                        } else {
                            WOWGUID none = 0;
                            auto gemInfo = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(gemItem->GetEntryID())), &none, &CGTooltip::OnItemArrived, this, true);

                            if (gemInfo) {
                                if (auto properties = g_gemPropertiesDB.GetRecord(gemInfo->gemProperties)) {
                                    enchantID = properties->m_enchantID;
                                }

                                if (!ItemSocketInfoGemFits(info, gemInfo, static_cast<int32_t>(socket))) {
                                    allGemsFit = false;
                                }
                            }
                        }
                    }

                    if (enchantID == 0) {
                        if (socket >= 3 || !hasSocket || socketPreview) {
                            continue;
                        }

                        auto color = static_cast<uint32_t>(info->socketColor[socket]);

                        if (color != 0) {
                            allGemsFit = false;
                            addEmptySocket(color, TOOLTIP_COLOR_GRAY);

                            continue;
                        }

                        // A prismatic socket: it may need a skill and a level.
                        int32_t prismaticID;

                        if (!item) {
                            prismaticID = this->m_useLinkInfo ? this->m_linkInfo.enchant[6] : 0;
                        } else {
                            prismaticID = item->IsCharter() ? 0 : itemData->enchantments[6].id;
                        }

                        auto prismatic = g_spellItemEnchantmentDB.GetRecord(prismaticID);

                        if (!prismaticID || !prismatic || !prismatic->m_requiredSkillID) {
                            this->AddLine(FrameScript_GetText("EMPTY_SOCKET_NO_COLOR", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_GRAY, TOOLTIP_COLOR_GRAY, 0);
                            this->AddTexture("Interface\\ItemSocketingFrame\\UI-EmptySocket.blp", TOOLTIP_TEXCOORDS, TooltipWhite());

                            continue;
                        }

                        auto index = player->GetSkillIndex(prismatic->m_requiredSkillID);
                        bool skillOK = index >= 0 && prismatic->m_requiredSkillRank <= static_cast<int32_t>(player->GetSkillRank(index));
                        bool levelOK = prismatic->m_minLevel <= playerLevel;
                        auto& color2 = skillOK && levelOK ? TOOLTIP_COLOR_GRAY : TOOLTIP_COLOR_RED;

                        this->AddLine(FrameScript_GetText("EMPTY_SOCKET_NO_COLOR", -1, GENDER_NOT_APPLICABLE), nullptr, color2, color2, 0);
                        this->AddTexture("Interface\\ItemSocketingFrame\\UI-EmptySocket.blp", TOOLTIP_TEXCOORDS, TooltipWhite());

                        if (!skillOK) {
                            TooltipFormatSkillRequirement(line, sizeof(line), "SOCKET_ITEM_REQ_SKILL", "SOCKET_ITEM_MIN_SKILL", prismatic->m_requiredSkillID, prismatic->m_requiredSkillRank);
                            this->AddLine(line, nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);
                        }

                        if (!levelOK) {
                            // The reference formats the level requirement here and never adds it.
                            SStrCopy(text, FrameScript_GetText("SOCKET_ITEM_REQ_LEVEL", -1, GENDER_NOT_APPLICABLE), sizeof(text));
                            SStrPrintf(line, sizeof(line), text, prismatic->m_minLevel);
                        }

                        continue;
                    }

                    if (slot == 6 || (info->socketBonus != 0 && info->socketBonus == enchantID)) {
                        continue;
                    }

                    auto enchant = g_spellItemEnchantmentDB.GetRecord(std::abs(enchantID));

                    if (!enchant) {
                        continue;
                    }

                    if (enchant->m_effect[0] == 7) {
                        // An "on use" enchantment: its spell is listed with the item's own.
                        useEnchantSpell = enchant->m_effectArg[0];
                        useEnchant = enchant;

                        if (!item) {
                            useEnchantCharges = this->m_linkInfo.enchantCharges[slot];
                        } else {
                            useEnchantCharges = item->IsCharter() ? 0 : static_cast<int16_t>(itemData->enchantments[slot].chargesRemaining);
                        }

                        continue;
                    }

                    if (enchant->m_effect[0] == 3 && ClntObjMgrGetActivePlayer() == owner) {
                        auto spell = g_spellDB.GetRecord(enchant->m_effectArg[0]);

                        if (spell && ownerPlayer && !SpellIsUsableInArena(spell)) {
                            continue;
                        }
                    }

                    CImVector lineColor = TOOLTIP_COLOR_HIGHLIGHT;

                    if (socket > 2 && slot < 7) {
                        lineColor = enchantID < 1 ? TOOLTIP_COLOR_PURE_RED : TOOLTIP_COLOR_GREEN;
                    }

                    auto points = !item
                        ? CGItem_C::GetRandomSuffixPoints(slot, this->m_linkInfo.randomPropertyID, static_cast<int16_t>(this->m_linkInfo.suffixFactor))
                        : item->GetEnchantPoints(slot);

                    SpellItemEnchantmentGetText(enchant, text, sizeof(text), points);

                    auto timeLeft = item ? item->GetEnchantTimeLeft(slot) : 0;

                    if (!item || timeLeft == 0) {
                        SStrCopy(line, text, sizeof(line));
                    } else {
                        FormatTimeInterval(line, sizeof(line), static_cast<uint64_t>(static_cast<int64_t>(timeLeft)), "ITEM_ENCHANT_TIME_LEFT", text, 1, false);
                        result = 1;
                    }

                    int32_t charges = 0;
                    bool hasCharges;

                    if (!item) {
                        charges = this->m_linkInfo.enchantCharges[slot];
                        hasCharges = true;
                    } else if (!item->IsCharter()) {
                        charges = static_cast<int16_t>(itemData->enchantments[slot].chargesRemaining);
                        hasCharges = true;
                    } else {
                        hasCharges = false;
                    }

                    if (hasCharges && charges != 0) {
                        if (!item || !((reinterpret_cast<const uint8_t*>(&itemData->enchantments[slot].chargesRemaining)[2]) & 0xF)) {
                            char count[64];
                            SStrPrintf(count, sizeof(count), FrameScript_GetText("ITEM_SPELL_CHARGES", -1, GENDER_NOT_APPLICABLE), charges);
                            SStrPrintf(text, sizeof(text), " (%s)", count);
                            SStrPack(line, text, sizeof(line));
                        } else {
                            lineColor = TOOLTIP_COLOR_GRAY;
                        }
                    }

                    char condition[1024];
                    condition[0] = '\0';
                    bool skillOK = true;
                    bool levelOK = true;

                    if (enchant->m_requiredSkillID) {
                        auto index = player->GetSkillIndex(enchant->m_requiredSkillID);
                        skillOK = index >= 0 && enchant->m_requiredSkillRank <= static_cast<int32_t>(player->GetSkillRank(index));
                    }

                    if (playerLevel < enchant->m_minLevel) {
                        levelOK = false;
                    }

                    int32_t gemEnchantID = 0;
                    bool gemOK = true;
                    bool noPrismatic;

                    if (!item) {
                        noPrismatic = this->m_linkInfo.enchant[6] == 0;
                    } else {
                        noPrismatic = item->IsCharter() || itemData->enchantments[6].id == 0;
                    }

                    bool conditionLine = false;

                    if (slot < 2 || slot > 4) {
                        if (slot < 7) {
                            auto& color = skillOK && levelOK ? lineColor : TOOLTIP_COLOR_RED;
                            this->AddLine(line, nullptr, color, color, 0);
                        } else {
                            conditionLine = true;
                        }
                    } else {
                        if (info->socketColor[socket] == 0 || !noPrismatic) {
                            if (!item) {
                                gemEnchantID = this->m_useLinkInfo ? this->m_linkInfo.enchant[slot] : 0;
                            } else {
                                gemEnchantID = item->IsCharter() ? 0 : itemData->enchantments[slot].id;
                            }

                            auto gem = gemEnchantID ? g_spellItemEnchantmentDB.GetRecord(gemEnchantID) : nullptr;

                            if (gem && gem->m_requiredSkillID) {
                                auto index = player->GetSkillIndex(gem->m_requiredSkillID);
                                gemOK = index >= 0 && gem->m_requiredSkillRank <= static_cast<int32_t>(player->GetSkillRank(index)) && enchant->m_minLevel <= playerLevel;
                            }
                        }

                        conditionLine = true;
                    }

                    if (conditionLine) {
                        auto target = player;

                        if (auto ownerUnit = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(owner, TYPE_PLAYER, ".\\Tooltip.cpp", 0xed3))) {
                            ownerUnit->UpdateGemColorCounts();
                            target = ownerUnit;
                        }

                        auto met = EnchantGetConditionText(target, enchant, condition, sizeof(condition), true);
                        auto code = TOOLTIP_CODE_RED;

                        if (gemOK && skillOK && levelOK) {
                            code = met ? TOOLTIP_CODE_WHITE : TOOLTIP_CODE_GRAY;
                        }

                        SStrPrintf(text, sizeof(text), "%s%s%s", code, line, "|r");
                        SStrPack(text, condition, sizeof(text));
                        this->AddLine(text, nullptr, TOOLTIP_COLOR_NORMAL, TOOLTIP_COLOR_NORMAL, 0);
                    }

                    if (enchant->m_srcItemID) {
                        WOWGUID none = 0;
                        auto gemInfo = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(enchant->m_srcItemID)), &none, &CGTooltip::OnItemArrived, this, true);

                        if (gemInfo) {
                            if (!ItemSocketInfoGemFits(info, gemInfo, static_cast<int32_t>(socket))) {
                                allGemsFit = false;
                            }

                            auto directory = StringLookupsGetString(3);
                            char icon[260];
                            SStrPrintf(icon, sizeof(icon), "%s%s%s", directory ? directory : "", directory && directory[0] ? "\\" : "", ItemDisplayGetIcon(gemInfo->displayInfoID));
                            this->AddTexture(icon, TOOLTIP_TEXCOORDS, TooltipWhite());
                        }
                    }

                    if (!skillOK && enchant->m_requiredSkillID) {
                        TooltipFormatSkillRequirement(line, sizeof(line), "ENCHANT_ITEM_REQ_SKILL", "ENCHANT_ITEM_MIN_SKILL", enchant->m_requiredSkillID, enchant->m_requiredSkillRank);
                        this->AddLine(line, nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);
                    }

                    if (!levelOK) {
                        SStrCopy(text, FrameScript_GetText("ENCHANT_ITEM_REQ_LEVEL", -1, GENDER_NOT_APPLICABLE), sizeof(text));
                        SStrPrintf(line, sizeof(line), text, enchant->m_minLevel);
                        this->AddLine(line, nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);
                    }

                    if (!gemOK && gemEnchantID) {
                        auto gem = g_spellItemEnchantmentDB.GetRecord(gemEnchantID);

                        if (gem && gem->m_requiredSkillID) {
                            TooltipFormatSkillRequirement(line, sizeof(line), "SOCKET_ITEM_REQ_SKILL", "SOCKET_ITEM_MIN_SKILL", gem->m_requiredSkillID, gem->m_requiredSkillRank);
                            this->AddLine(line, nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);
                        }
                    }
                }

                if (this->m_useLinkInfo && this->m_linkInfo.proposedEnchant) {
                    if (auto spell = g_spellDB.GetRecord(this->m_linkInfo.proposedEnchant)) {
                        SStrCopy(text, FrameScript_GetText("ITEM_PROPOSED_ENCHANT", -1, GENDER_NOT_APPLICABLE), sizeof(text));
                        SStrPrintf(line, sizeof(line), text, spell->m_name);
                        this->AddLine(line, nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 0);
                        this->AddLine(FrameScript_GetText("ITEM_ENCHANT_DISCLAIMER", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_PURE_RED, TOOLTIP_COLOR_PURE_RED, 0);
                    }
                }

                if (info->socketBonus) {
                    if (auto bonus = g_spellItemEnchantmentDB.GetRecord(std::abs(info->socketBonus))) {
                        SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_SOCKET_BONUS", -1, GENDER_NOT_APPLICABLE), bonus->m_name);
                        auto& color = allGemsFit ? TOOLTIP_COLOR_GREEN : TOOLTIP_COLOR_GRAY;
                        this->AddLine(line, nullptr, color, color, 0);
                    }
                }
            }
        }

        // A gem: its enchantment and the conditions it carries.
        if (info->gemProperties && !socketPreview) {
            auto properties = g_gemPropertiesDB.GetRecord(info->gemProperties);
            auto gemEnchant = properties ? g_spellItemEnchantmentDB.GetRecord(std::abs(properties->m_enchantID)) : nullptr;

            if (gemEnchant) {
                bool skillOK = true;

                if (info->requiredSkill) {
                    auto index = player->GetSkillIndex(info->requiredSkill);
                    skillOK = index >= 0 && static_cast<int32_t>(player->GetSkillRank(index)) >= info->requiredSkillRank;
                }

                auto& color = destroyGem ? TOOLTIP_COLOR_GRAY : (skillOK ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED);
                this->AddLine(gemEnchant->m_name, nullptr, color, color, 0);

                EnchantGetConditionText(player, gemEnchant, line, sizeof(line), false);

                if (line[0]) {
                    this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 1);
                }
            }
        }
    }

    // Durability.
    if (!wrapped && !socketPreview) {
        if (this->m_useLinkInfo && this->m_linkInfo.maxDurability == -1) {
            this->m_linkInfo.maxDurability = info->maxDurability;
        }

        bool itemDurability = item && !((itemData->flags >> 3) & 1) && itemData->maxDurability != 0;

        if (itemDurability || (this->m_useLinkInfo && this->m_linkInfo.maxDurability != 0)) {
            SStrCopy(text, FrameScript_GetText("DURABILITY_TEMPLATE", -1, GENDER_NOT_APPLICABLE), sizeof(text));

            uint32_t current;
            uint32_t maximum;

            if (!itemDurability) {
                bool fromLink = this->m_useLinkInfo && this->m_linkInfo.maxDurability != 0;
                current = static_cast<uint32_t>(fromLink ? this->m_linkInfo.durability : info->maxDurability);
                maximum = static_cast<uint32_t>(fromLink ? this->m_linkInfo.maxDurability : info->maxDurability);
            } else {
                current = static_cast<uint32_t>(itemData->durability);
                maximum = static_cast<uint32_t>(itemData->maxDurability);
            }

            if (maximum <= current) {
                current = maximum;
            }

            SStrPrintf(line, sizeof(line), text, current, maximum);
            auto& color = current == 0 ? TOOLTIP_COLOR_RED : TOOLTIP_COLOR_HIGHLIGHT;
            this->AddLine(line, nullptr, color, color, 0);
        }
    }

    // How long it lasts.
    auto duration = info->duration;

    if (duration > 0) {
        if (item) {
            duration = item->GetExpirationTimeLeft();
        }

        if (duration >= 0 && !socketPreview) {
            FormatTimeInterval(line, sizeof(line), static_cast<uint64_t>(static_cast<int64_t>(duration)), "ITEM_DURATION", nullptr, 1, true);
            this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
            result = 1;
        }
    }

    if (info->holidayID) {
        SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_REQ_SKILL", -1, GENDER_NOT_APPLICABLE), HolidayGetName(info->holidayID));
        this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
    }

    // Who may use it.
    if (!socketPreview) {
        auto races = static_cast<uint32_t>(info->allowableRace);
        auto classes = static_cast<uint32_t>(info->allowableClass);

        if ((races & (races - 1)) == 0 && (classes & (classes - 1)) == 0) {
            line[0] = '\0';
            bool usable = true;

            for (int32_t i = 0; i < g_chrRacesDB.GetNumRecords(); i++) {
                auto race = g_chrRacesDB.GetRecordByIndex(i);

                if (race && !(race->m_flags & 1) && race->m_name && (races & (1u << ((race->m_ID - 1) & 0x1F)))) {
                    SStrPrintf(line, sizeof(line), "%s ", race->m_name);

                    if (playerRace != static_cast<uint32_t>(race->m_ID)) {
                        usable = false;
                    }

                    break;
                }
            }

            for (int32_t i = 0; i < g_chrClassesDB.GetNumRecords(); i++) {
                auto classRec = g_chrClassesDB.GetRecordByIndex(i);

                if (classRec && classRec->m_name && (classes & (1u << ((classRec->m_ID - 1) & 0x1F)))) {
                    SStrPack(line, classRec->m_name, sizeof(line));

                    if (playerClass != static_cast<uint32_t>(classRec->m_ID)) {
                        usable = false;
                    }

                    break;
                }
            }

            if (line[0]) {
                char only[128];
                SStrCopy(text, FrameScript_GetText("RACE_CLASS_ONLY", -1, GENDER_NOT_APPLICABLE), sizeof(text));
                SStrPrintf(only, sizeof(only), text, line);
                auto& color = usable ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
                this->AddLine(only, nullptr, color, color, 0);
            }
        } else {
            bool allRaces = true;
            bool allClasses = true;

            for (int32_t i = 0; i < g_chrRacesDB.GetNumRecords(); i++) {
                auto race = g_chrRacesDB.GetRecordByIndex(i);

                if (race && !(race->m_flags & 1) && !(races & (1u << ((race->m_ID - 1) & 0x1F)))) {
                    allRaces = false;
                    break;
                }
            }

            for (int32_t i = 0; i < g_chrClassesDB.GetNumRecords(); i++) {
                auto classRec = g_chrClassesDB.GetRecordByIndex(i);

                if (classRec && !(classes & (1u << ((classRec->m_ID - 1) & 0x1F)))) {
                    allClasses = false;
                    break;
                }
            }

            char list[512];
            char allowed[512];

            if (!allRaces) {
                list[0] = '\0';
                bool first = true;
                bool usable = false;

                for (int32_t i = 0; i < g_chrRacesDB.GetNumRecords(); i++) {
                    auto race = g_chrRacesDB.GetRecordByIndex(i);

                    if (race && !(race->m_flags & 1) && race->m_name && (races & (1u << ((race->m_ID - 1) & 0x1F)))) {
                        if (!first) {
                            SStrPack(list, ", ", sizeof(list));
                        }

                        SStrPack(list, race->m_name, sizeof(list));
                        first = false;

                        if (playerRace == static_cast<uint32_t>(race->m_ID)) {
                            usable = true;
                        }
                    }
                }

                if (list[0]) {
                    SStrCopy(text, FrameScript_GetText("ITEM_RACES_ALLOWED", -1, GENDER_NOT_APPLICABLE), sizeof(text));
                    SStrPrintf(allowed, sizeof(allowed), text, list);
                    auto& color = usable ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
                    this->AddLine(allowed, nullptr, color, color, 0);
                }
            }

            if (!allClasses) {
                list[0] = '\0';
                bool first = true;
                bool usable = false;

                for (int32_t i = 0; i < g_chrClassesDB.GetNumRecords(); i++) {
                    auto classRec = g_chrClassesDB.GetRecordByIndex(i);

                    if (classRec && classRec->m_name && (classes & (1u << ((classRec->m_ID - 1) & 0x1F)))) {
                        if (!first) {
                            SStrPack(list, ", ", sizeof(list));
                        }

                        SStrPack(list, classRec->m_name, sizeof(list));
                        first = false;

                        if (playerClass == static_cast<uint32_t>(classRec->m_ID)) {
                            usable = true;
                        }
                    }
                }

                if (list[0]) {
                    SStrCopy(text, FrameScript_GetText("ITEM_CLASSES_ALLOWED", -1, GENDER_NOT_APPLICABLE), sizeof(text));
                    SStrPrintf(allowed, sizeof(allowed), text, list);
                    auto& color = usable ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
                    this->AddLine(allowed, nullptr, color, color, 0);
                }
            }
        }
    }

    // The level it needs, and its item level.
    auto requiredLevel = info->requiredLevel;

    if (!distribution) {
        if (requiredLevel > 1) {
            SStrCopy(text, FrameScript_GetText("ITEM_MIN_LEVEL", -1, GENDER_NOT_APPLICABLE), sizeof(text));
            SStrPrintf(line, sizeof(line), text, requiredLevel);
            auto& color = requiredLevel <= playerLevel ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
            this->AddLine(line, nullptr, color, color, 0);
        }
    } else {
        SStrCopy(text, FrameScript_GetText("ITEM_LEVEL_RANGE_CURRENT", -1, GENDER_NOT_APPLICABLE), sizeof(text));
        SStrPrintf(line, sizeof(line), text, requiredLevel < 1 ? 1 : requiredLevel, distribution->m_maxLevel, scalingLevel);
        auto& color = (playerLevel < requiredLevel || distribution->m_maxLevel < playerLevel) ? TOOLTIP_COLOR_RED : TOOLTIP_COLOR_HIGHLIGHT;
        this->AddLine(line, nullptr, color, color, 0);
    }

    auto showItemLevel = CVar::Lookup("showItemLevel");

    if (showItemLevel && showItemLevel->GetInt() && (info->itemClass == 2 || info->itemClass == 4 || info->itemClass == 5 || info->itemClass == 6)) {
        SStrCopy(text, FrameScript_GetText("ITEM_LEVEL", -1, GENDER_NOT_APPLICABLE), sizeof(text));
        SStrPrintf(line, sizeof(line), text, info->itemLevel);
        this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
    }

    if (!socketPreview) {
        // The skill it needs, or the profession it feeds.
        if (info->requiredSkill) {
            auto index = player->GetSkillIndex(info->requiredSkill);
            bool skillOK = index >= 0 && static_cast<int32_t>(player->GetSkillRank(index)) >= info->requiredSkillRank;
            auto& color = skillOK ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;

            if (info->flags & 0x40000) {
                if (index >= 0 && player->GetSkillRank(index) >= 1) {
                    this->AddLine(FrameScript_GetText("ITEM_PROSPECTABLE", -1, GENDER_NOT_APPLICABLE), nullptr, color, color, 0);
                }
            } else if (info->flags & 0x20000000) {
                if (index >= 0 && player->GetSkillRank(index) >= 1) {
                    this->AddLine(FrameScript_GetText("ITEM_MILLABLE", -1, GENDER_NOT_APPLICABLE), nullptr, color, color, 0);
                }
            } else {
                TooltipFormatSkillRequirement(line, sizeof(line), "ITEM_REQ_SKILL", "ITEM_MIN_SKILL", info->requiredSkill, info->requiredSkillRank);
                this->AddLine(line, nullptr, color, color, 0);
            }
        }

        // A recipe or book whose spell is already known.
        auto teach = g_spellDB.GetRecord(info->spellID[0]);

        if (teach && teach->m_effect[0] == 36) {
            auto learned = teach->m_effectTriggerSpell[0];

            if (!learned) {
                for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
                    if (info->spellTrigger[i] == 6) {
                        learned = info->spellID[i];
                        break;
                    }
                }
            }

            CGUnit_C* learner = player;

            if (teach->m_effectImplicitTargetA[0] == 5 || teach->m_effectImplicitTargetB[0] == 5) {
                auto pet = playerData->charm ? playerData->charm : playerData->summon;
                learner = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(pet, TYPE_UNIT, ".\\Tooltip.cpp", 0x1083));
            }

            if (learner && (learner->KnowsSpell(static_cast<uint32_t>(learned)) || learner->KnowsHigherRank(learned))) {
                this->AddLine(FrameScript_GetText("ITEM_SPELL_KNOWN", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);
            }
        }

        if (info->requiredSpell) {
            if (auto spell = g_spellDB.GetRecord(info->requiredSpell)) {
                bool known = player->KnowsSpell(static_cast<uint32_t>(info->requiredSpell)) || player->KnowsHigherRank(info->requiredSpell);
                SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_REQ_SKILL", -1, GENDER_NOT_APPLICABLE), spell->m_name);
                auto& color = known ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
                this->AddLine(line, nullptr, color, color, 0);
            }
        }

        if (info->requiredHonorRank) {
            // The player's highest honor rank, PLAYER_FIELD_BYTES' top byte.
            auto rank = static_cast<int32_t>(player->Player()->field_bytes_4);
            SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_REQ_SKILL", -1, GENDER_NOT_APPLICABLE), player->GetPvpRankName(info->requiredHonorRank));
            auto& color = rank < info->requiredHonorRank ? TOOLTIP_COLOR_RED : TOOLTIP_COLOR_HIGHLIGHT;
            this->AddLine(line, nullptr, color, color, 0);
        }

        // What the open vendor asks beyond money.
        if (*requester == MerchantGetGUID()) {
            auto merchantItem = MerchantFindItem(itemID);
            auto cost = merchantItem ? g_itemExtendedCostDB.GetRecord(merchantItem->m_extendedCostID) : nullptr;

            if (cost) {
                if (cost->m_requiredArenaRating > 0) {
                    auto bracket = cost->m_arenaBracket;

                    if (bracket < 0) {
                        bracket = 0;
                    } else if (bracket > 3) {
                        bracket = 3;
                    }

                    bool rated = CGArenaTeamInfo::HasRating(static_cast<uint32_t>(cost->m_requiredArenaRating), bracket);
                    auto token = bracket == 2 ? "ITEM_REQ_ARENA_RATING_5V5" : bracket == 1 ? "ITEM_REQ_ARENA_RATING_3V3" : "ITEM_REQ_ARENA_RATING";
                    SStrPrintf(line, sizeof(line), FrameScript_GetText(token, -1, GENDER_NOT_APPLICABLE), cost->m_requiredArenaRating);
                    auto& color = rated ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
                    this->AddLine(line, nullptr, color, color, 0);
                }

                auto group = cost->m_itemPurchaseGroup ? g_itemPurchaseGroupDB.GetRecord(cost->m_itemPurchaseGroup) : nullptr;

                if (group) {
                    bool owned = false;

                    for (int32_t i = 0; i < 8; i++) {
                        if (group->m_itemID[i] && player->m_bag.FindItemByID(group->m_itemID[i], 8)) {
                            owned = true;
                            break;
                        }
                    }

                    SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_REQ_PURCHASE_GROUP", -1, GENDER_NOT_APPLICABLE), group->m_name);
                    auto& color = owned ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
                    this->AddLine(line, nullptr, color, color, 0);
                }
            }
        }

        if (info->requiredCityRank) {
            auto medals = player->Player()->pvpMedals;
            SStrPrintf(text, sizeof(text), "PVP_MEDAL%d", info->requiredCityRank);
            SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_REQ_SKILL", -1, GENDER_NOT_APPLICABLE), FrameScript_GetText(text, -1, GENDER_NOT_APPLICABLE));
            auto& color = (medals & (1u << ((info->requiredCityRank - 1) & 0x1F))) ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
            this->AddLine(line, nullptr, color, color, 0);
        }

        if (info->requiredReputationFaction) {
            auto rank = info->requiredReputationRank;
            auto standing = ReputationGetStanding(info->requiredReputationFaction);
            bool met = s_standingThresholds[rank] <= standing;
            auto faction = g_factionDB.GetRecord(info->requiredReputationFaction);

            SStrPrintf(text, sizeof(text), "FACTION_STANDING_LABEL%d", rank + 1);
            auto label = player->GetGenderedText(text, -1);
            SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_REQ_REPUTATION", -1, GENDER_NOT_APPLICABLE), faction ? faction->m_name : "UNKNOWN", label);
            auto& color = met ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
            this->AddLine(line, nullptr, color, color, 0);
        }
    }

    // The item set: which pieces the player (or the owner) wears.
    const ItemSetRec* set = nullptr;
    int32_t setEquipped = 0;
    int32_t setUnique = 0;
    bool setSkillMet = false;
    int32_t matched[17] = {};

    if (info->itemSet && (set = g_itemSetDB.GetRecord(info->itemSet)) != nullptr && !socketPreview) {
        ITEMSETMATCH match = {};
        match.tooltip = this;
        match.set = set;
        match.matched = matched;

        for (int32_t piece = 0; piece < 17; piece++) {
            if (set->m_itemID[piece] == 0) {
                match.names[piece] = nullptr;
                continue;
            }

            WOWGUID none = 0;
            auto pieceName = g_itemNameCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(set->m_itemID[piece])), &none, &CGTooltip::OnItemArrived, this, true);
            match.names[piece] = pieceName;

            if (!pieceName) {
                continue;
            }

            int32_t other = 0;

            for (; other < piece; other++) {
                if (!match.names[other]) {
                    break;
                }

                auto type = pieceName->m_inventoryType;
                auto otherType = match.names[other]->m_inventoryType;

                if (type == otherType && type != 11 && type != 12 && type != 13) {
                    break;
                }

                if ((type == 5 && otherType == 20) || (type == 20 && otherType == 5)) {
                    break;
                }
            }

            if (other == piece) {
                setUnique++;
            }
        }

        for (int32_t pass = 0; pass < 2; pass++) {
            for (int32_t slot = 0; slot < 19; slot++) {
                if (!ownerPlayer) {
                    auto worn = player->m_bag.GetItem(slot);

                    if (worn && ItemSetMatch(&match, pass, worn->GetEntryID(), slot)) {
                        setEquipped++;
                    }
                } else {
                    auto visible = ownerPlayer->GetVisibleItem(slot);
                    auto entry = visible ? std::abs(visible->entryID) : 0;

                    if (entry && ItemSetMatch(&match, pass, entry, slot)) {
                        setEquipped++;
                    }
                }
            }
        }

        setSkillMet = ItemSetMeetsSkill(set);
    } else if (socketPreview) {
        set = info->itemSet ? g_itemSetDB.GetRecord(info->itemSet) : nullptr;
    }

    if (playerClass == 11) {
        auto feral = ItemGetFeralAttackPower(info, averageDamage);

        if (feral > 0) {
            SStrCopy(text, FrameScript_GetText("FERAL_DRUID_ITEM_AP", -1, GENDER_NOT_APPLICABLE), sizeof(text));
            SStrPrintf(name, sizeof(name), text, feral);
            this->AddLine(name, nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 1);
        }
    }

    // The ratings and other stats, as "Equip:" lines.
    auto addEquipStat = [&](int32_t type, int32_t value, int32_t wrap) {
        text[0] = '\0';
        SStrCopy(text, FrameScript_GetText(s_itemModTokens[type], -1, GENDER_NOT_APPLICABLE), sizeof(text));

        if (!text[0]) {
            return;
        }

        if (SStrStr(text, "%c")) {
            SStrPrintf(name, sizeof(name), text, value < 1 ? '-' : '+', std::abs(value));
        } else {
            SStrPrintf(name, sizeof(name), text, value);
        }

        SStrPrintf(line, sizeof(line), "%s %s", FrameScript_GetText("ITEM_SPELL_TRIGGER_ONEQUIP", -1, GENDER_NOT_APPLICABLE), name);
        this->AddLine(line, nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, wrap);
    };

    if (info->scalingStatValue == 0) {
        for (int32_t i = 0; i < ItemStats_C::MAX_STATS; i++) {
            if (info->statValue[i] == 0 || info->statType[i] == -1) {
                continue;
            }

            for (auto type : s_equipStatOrder) {
                if (info->statType[i] == type) {
                    addEquipStat(type, info->statValue[i], 0);
                    break;
                }
            }
        }
    } else {
        auto budget = ScalingStatValuesGetBudget(scalingValues, info->scalingStatValue);

        if (distribution && budget) {
            for (int32_t i = 0; i < 10; i++) {
                auto value = budget * distribution->m_bonus[i] / 10000;

                if (distribution->m_bonus[i] == 0 || value == 0) {
                    continue;
                }

                for (auto type : s_equipStatOrder) {
                    if (distribution->m_statID[i] == type) {
                        addEquipStat(type, value, 1);
                        break;
                    }
                }
            }
        }

        if ((info->scalingStatValue & 0x8000) && scalingValues && scalingValues->m_spellPower) {
            text[0] = '\0';
            SStrCopy(text, FrameScript_GetText(s_itemModTokens[45], -1, GENDER_NOT_APPLICABLE), sizeof(text));

            if (text[0]) {
                SStrPrintf(name, sizeof(name), text, scalingValues->m_spellPower);
                SStrPrintf(line, sizeof(line), "%s %s", FrameScript_GetText("ITEM_SPELL_TRIGGER_ONEQUIP", -1, GENDER_NOT_APPLICABLE), name);
                this->AddLine(line, nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 1);
            }
        }
    }

    // The item's spells.
    bool learnShown = false;
    char dumpLine[512];
    char escaped[2048];

    for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
        if (dump) {
            sprintf(dumpLine, "\t\t\t<spelldesc_%d>", i);
            fputs(dumpLine, dump);
        }

        auto closeDump = [&]() {
            if (dump) {
                sprintf(dumpLine, "</spelldesc_%d>\n", i);
                fputs(dumpLine, dump);
            }
        };

        if (info->spellID[i] < 1) {
            closeDump();
            continue;
        }

        auto spell = g_spellDB.GetRecord(info->spellID[i]);

        if (!spell) {
            closeDump();
            continue;
        }

        SpellParseDescription(spell, text, sizeof(text), 0, 0, 0, 0, 1, 0);

        if (text[0] == '\0' && info->spellTrigger[i] != 6) {
            closeDump();
            continue;
        }

        switch (info->spellTrigger[i]) {
            case 0: {
                auto cooldown = info->spellCooldown[i];
                bool negative = cooldown < 0;

                if (negative && info->spellCategoryCooldown[i] < 0) {
                    auto recovery = spell->m_recoveryTime;
                    auto categoryRecovery = spell->m_categoryRecoveryTime;
                    SpellApplyModifier(spell, &recovery, 0xb);

                    if (!(spell->m_attributesEx6 & 0x80000000)) {
                        SpellApplyModifier(spell, &categoryRecovery, 0xb);
                    }

                    cooldown = recovery <= categoryRecovery ? categoryRecovery : recovery;
                    negative = cooldown < 0;
                }

                if (!negative && cooldown != 0) {
                    char cooldownText[1024];
                    TooltipFormatCooldown(cooldownText, sizeof(cooldownText), static_cast<uint64_t>(static_cast<int64_t>(cooldown)), "ITEM_COOLDOWN_TOTAL");
                    SStrPrintf(line, sizeof(line), "%s %s %s", FrameScript_GetText("ITEM_SPELL_TRIGGER_ONUSE", -1, GENDER_NOT_APPLICABLE), text, cooldownText);
                } else {
                    SStrPrintf(line, sizeof(line), "%s %s", FrameScript_GetText("ITEM_SPELL_TRIGGER_ONUSE", -1, GENDER_NOT_APPLICABLE), text);
                }

                break;
            }

            case 1:
                SStrPrintf(line, sizeof(line), "%s %s", FrameScript_GetText("ITEM_SPELL_TRIGGER_ONEQUIP", -1, GENDER_NOT_APPLICABLE), text);
                break;

            case 2:
                SStrPrintf(line, sizeof(line), "%s %s", FrameScript_GetText("ITEM_SPELL_TRIGGER_ONPROC", -1, GENDER_NOT_APPLICABLE), text);
                break;

            case 6:
                SStrPrintf(line, sizeof(line), "%s %s", FrameScript_GetText("ITEM_SPELL_TRIGGER_ONUSE", -1, GENDER_NOT_APPLICABLE), info->description.c_str());
                learnShown = true;
                break;

            default:
                SStrCopy(line, text, sizeof(line));
                break;
        }

        // A recipe: the spell it teaches creates an item.
        const SpellRec* learned = nullptr;
        bool recipe = false;

        if (info->spellTrigger[i] == 6) {
            learned = spell;
        } else if (spell->m_effect[0] == 36) {
            learned = g_spellDB.GetRecord(spell->m_effectTriggerSpell[0]);
        }

        if (learned) {
            auto effect = learned->m_effect[0];

            if ((effect == 24 || effect == 157 || effect == 59) && learned->m_effectItemType[0] != 0) {
                recipe = true;
            }
        }

        if (dump) {
            TooltipXmlEscape(text, escaped);
            fputs(escaped, dump);
        }

        auto& color = recipe ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_GREEN;
        this->AddLine(line, nullptr, color, color, 1);

        if (!noCharges) {
            int32_t charges;

            if (this->m_useLinkInfo) {
                charges = this->m_linkInfo.charges;
            } else if (!item) {
                charges = info->spellCharges[i];
            } else {
                charges = itemData->spellCharges[i];
            }

            if (info->spellCharges[i] != 0 && info->spellCharges[i] != -1) {
                auto count = std::abs(charges);

                if (count == 0) {
                    SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_SPELL_CHARGES_NONE", -1, GENDER_NOT_APPLICABLE));
                } else {
                    SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_SPELL_CHARGES", -1, GENDER_NOT_APPLICABLE), count);
                }

                if (dump) {
                    TooltipXmlEscape(line, escaped);
                    fputs(escaped, dump);
                }

                this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
            }
        }

        if (learned && recipe) {
            // What the recipe makes, as its own tooltip appended here, then its reagents.
            WOWGUID none = 0;
            WOWGUID noItem = 0;
            this->SetItem(learned->m_effectItemType[0], &none, &noItem, 0, 0, 0, 1, 0, 0, nullptr, noCharges, nullptr, 0, 0, 1);

            char reagents[4096];
            reagents[0] = '\0';
            bool first = true;
            bool missing = false;

            for (int32_t r = 0; r < 8; r++) {
                if (learned->m_reagent[r] <= 0) {
                    continue;
                }

                auto reagentRequester = TooltipSpellRequester(learned->m_ID);
                auto reagent = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(learned->m_reagent[r])), &reagentRequester, &CGTooltip::OnItemArrived, this, true);

                if (!reagent) {
                    missing = true;
                    continue;
                }

                if (!first) {
                    SStrPack(reagents, ", ", sizeof(reagents));
                }

                first = false;

                if (learned->m_reagentCount[r] < 2) {
                    SStrCopy(text, reagent->name.c_str(), sizeof(text));
                } else {
                    SStrPrintf(text, sizeof(text), "%s (%d)", reagent->name.c_str(), learned->m_reagentCount[r]);
                }

                SStrPack(reagents, text, sizeof(reagents));
            }

            if (!first && !missing) {
                SStrPrintf(text, sizeof(text), FrameScript_GetText("ITEM_REQ_SKILL", -1, GENDER_NOT_APPLICABLE), reagents);
                SStrPrintf(name, sizeof(name), "\n%s", text);

                if (dump) {
                    TooltipXmlEscape(name, escaped);
                    fputs(escaped, dump);
                }

                this->AddLine(name, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 1);
            }
        }

        closeDump();
    }

    // A use enchantment's spell.
    if (useEnchantSpell) {
        if (auto spell = g_spellDB.GetRecord(useEnchantSpell)) {
            SpellParseDescription(spell, text, sizeof(text), 0, 0, 0, 0, 1, 0);

            bool skillOK = true;
            bool levelOK = true;

            if (useEnchant->m_requiredSkillID) {
                auto index = player->GetSkillIndex(useEnchant->m_requiredSkillID);

                if (index < 0 || static_cast<int32_t>(player->GetSkillRank(index)) < useEnchant->m_requiredSkillRank) {
                    skillOK = false;
                }
            }

            if (playerLevel < useEnchant->m_minLevel) {
                levelOK = false;
            }

            auto recovery = spell->m_recoveryTime;
            auto categoryRecovery = spell->m_categoryRecoveryTime;
            SpellApplyModifier(spell, &recovery, 0xb);

            if (!(spell->m_attributesEx6 & 0x80000000)) {
                SpellApplyModifier(spell, &categoryRecovery, 0xb);
            }

            auto cooldown = recovery <= categoryRecovery ? categoryRecovery : recovery;

            if (cooldown < 1) {
                SStrPrintf(line, sizeof(line), "%s %s", FrameScript_GetText("ITEM_SPELL_TRIGGER_ONUSE", -1, GENDER_NOT_APPLICABLE), text);
            } else {
                char cooldownText[1024];
                TooltipFormatCooldown(cooldownText, sizeof(cooldownText), static_cast<uint64_t>(static_cast<int64_t>(cooldown)), "ITEM_COOLDOWN_TOTAL");
                SStrPrintf(line, sizeof(line), "%s %s %s", FrameScript_GetText("ITEM_SPELL_TRIGGER_ONUSE", -1, GENDER_NOT_APPLICABLE), text, cooldownText);
            }

            if (useEnchantCharges != 0 && useEnchantCharges != -1) {
                auto count = std::abs(useEnchantCharges);
                char chargesText[64];
                SStrPrintf(chargesText, sizeof(chargesText), FrameScript_GetText("ITEM_SPELL_CHARGES", count, GENDER_NOT_APPLICABLE), count);
                SStrPrintf(text, sizeof(text), " (%s)", chargesText);
                SStrPack(line, text, sizeof(line));
            }

            auto& color = skillOK && levelOK ? TOOLTIP_COLOR_GREEN : TOOLTIP_COLOR_RED;
            this->AddLine(line, nullptr, color, color, 1);

            if (!skillOK && useEnchant->m_requiredSkillID) {
                TooltipFormatSkillRequirement(line, sizeof(line), "ENCHANT_ITEM_REQ_SKILL", "ENCHANT_ITEM_MIN_SKILL", useEnchant->m_requiredSkillID, useEnchant->m_requiredSkillRank);
                this->AddLine(line, nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);
            }

            if (!levelOK) {
                char format[256];
                SStrCopy(format, FrameScript_GetText("ENCHANT_ITEM_REQ_LEVEL", -1, GENDER_NOT_APPLICABLE), sizeof(format));
                SStrPrintf(line, sizeof(line), format, useEnchant->m_minLevel);
                this->AddLine(line, nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);
            }
        }
    }

    // The set's pieces and bonuses.
    if (!set || socketPreview) {
        if (dump) {
            for (int32_t i = 0; i < 8; i++) {
                sprintf(dumpLine, "\t\t\t<setspelldesc_%d></setspelldesc_%d>\n", i, i);
                fputs(dumpLine, dump);
            }
        }
    } else {
        this->AddLine(" ", nullptr, TOOLTIP_COLOR_NORMAL, TOOLTIP_COLOR_NORMAL, 1);

        SStrCopy(text, FrameScript_GetText("ITEM_SET_NAME", -1, GENDER_NOT_APPLICABLE), sizeof(text));
        SStrPrintf(line, sizeof(line), text, set->m_name, setEquipped, setUnique);
        this->AddLine(line, nullptr, TOOLTIP_COLOR_NORMAL, TOOLTIP_COLOR_NORMAL, 0);

        if (set->m_requiredSkill) {
            TooltipFormatSkillRequirement(line, sizeof(line), "ITEM_REQ_SKILL", "ITEM_MIN_SKILL", set->m_requiredSkill, set->m_requiredSkillRank);
            auto index = player->GetSkillIndex(set->m_requiredSkill);
            bool met = index >= 0 && static_cast<int32_t>(player->GetSkillRank(index)) >= set->m_requiredSkillRank;
            auto& color = met ? TOOLTIP_COLOR_HIGHLIGHT : TOOLTIP_COLOR_RED;
            this->AddLine(line, nullptr, color, color, 0);
        }

        for (int32_t piece = 0; piece < 17; piece++) {
            if (!set->m_itemID[piece]) {
                continue;
            }

            const char* pieceName = nullptr;

            if (matched[piece] == 0) {
                auto record = g_itemNameCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(set->m_itemID[piece])));
                pieceName = record ? record->m_name.c_str() : nullptr;
            } else {
                auto record = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(matched[piece])));
                pieceName = record ? record->name.c_str() : nullptr;
            }

            if (pieceName) {
                SStrPrintf(line, sizeof(line), "  %s", pieceName);
                auto& color = matched[piece] == 0 ? TOOLTIP_COLOR_GRAY : TOOLTIP_COLOR_LIGHT_YELLOW;
                this->AddLine(line, nullptr, color, color, 0);
            }
        }

        this->AddLine(" ", nullptr, TOOLTIP_COLOR_NORMAL, TOOLTIP_COLOR_NORMAL, 1);

        uint8_t order[8];

        for (uint8_t i = 0; i < 8; i++) {
            order[i] = i;
        }

        s_sortSet = set;
        qsort(order, 8, 1, &ItemSetBonusCompare);

        for (int32_t i = 0; i < 8; i++) {
            auto bonus = order[i];

            if (dump) {
                sprintf(dumpLine, "\t\t\t<setspelldesc_%d>", i);
                fputs(dumpLine, dump);
            }

            if (set->m_setSpellID[bonus]) {
                if (auto spell = g_spellDB.GetRecord(set->m_setSpellID[bonus])) {
                    SpellParseDescription(spell, text, sizeof(text), 0, 0, 0, 0, 1, 0);

                    if (!setSkillMet || setEquipped < set->m_setThreshold[bonus]) {
                        SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_SET_BONUS_GRAY", -1, GENDER_NOT_APPLICABLE), set->m_setThreshold[bonus], text);
                        this->AddLine(line, nullptr, TOOLTIP_COLOR_GRAY, TOOLTIP_COLOR_GRAY, 1);
                    } else {
                        SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_SET_BONUS", -1, GENDER_NOT_APPLICABLE), text);
                        this->AddLine(line, nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 1);
                    }

                    if (dump) {
                        TooltipXmlEscape(text, escaped);
                        fputs(escaped, dump);
                    }
                }
            }

            if (dump) {
                sprintf(dumpLine, "</setspelldesc_%d>\n", i);
                fputs(dumpLine, dump);
            }
        }
    }

    // The cooldown.
    if (!this->m_useLinkInfo || !this->m_linkInfo.cooldown || socketPreview) {
        int32_t cooldownDuration = 0;
        uint32_t enabled = 0;
        ItemGetCooldown(item, &cooldownDuration, nullptr, &enabled);

        if (cooldownDuration != 0 && enabled == 0) {
            this->AddLine(FrameScript_GetText("COOLDOWN_ON_LEAVE_COMBAT", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
            result = 1;
        }
    } else {
        TooltipFormatCooldown(line, sizeof(line), static_cast<uint32_t>(this->m_linkInfo.cooldown), "ITEM_COOLDOWN_TIME");
        this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
        result = 1;
    }

    if (!nameOnly) {
        if (!learnShown && !info->description.empty()) {
            SStrPrintf(line, sizeof(line), "\"%s\"", info->description.c_str());
            this->AddLine(line, nullptr, TOOLTIP_COLOR_NORMAL, TOOLTIP_COLOR_NORMAL, 1);
        }

        if (!socketPreview) {
            // Who made it, wrapped it, and what can be done with it.
            bool describe = true;
            WOWGUID creator = 0;

            if (!wrapped) {
                if (item) {
                    creator = itemData->creator;
                } else if (this->m_useLinkInfo) {
                    creator = this->m_linkInfo.creator;
                } else {
                    describe = false;
                }

                if (describe && creator) {
                    if (auto creatorName = g_nameCache.GetRecord(DBCACHEKEY64(creator), &creator, &CGTooltip::OnItemArrived, this, true)) {
                        auto token = (item && ((itemData->flags >> 9) & 1)) ? "ITEM_WRITTEN_BY" : "ITEM_CREATED_BY";
                        SStrPrintf(line, sizeof(line), FrameScript_GetText(token, -1, GENDER_NOT_APPLICABLE), creatorName->m_name);
                        this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
                    }
                }
            } else {
                WOWGUID gift = 0;

                if (item) {
                    gift = itemData->giftCreator;
                } else if (this->m_useLinkInfo) {
                    gift = this->m_linkInfo.giftCreator;
                } else {
                    describe = false;
                }

                if (describe && gift) {
                    if (auto giftName = g_nameCache.GetRecord(DBCACHEKEY64(gift), &gift, &CGTooltip::OnItemArrived, this, true)) {
                        SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_WRAPPED_BY", -1, GENDER_NOT_APPLICABLE), giftName->m_name);
                        this->AddLine(line, nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
                    }
                }
            }

            if (describe && item) {
                bool openable = ((info->flags & 0x4) && (info->lockID == 0 || ((itemData->flags >> 2) & 1)))
                    || ((info->flags & 0x200) && ((itemData->flags >> 3) & 1));

                if (!this->m_useLinkInfo && openable) {
                    this->AddLine(FrameScript_GetText("ITEM_OPENABLE", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 0);
                } else if (item->GetPageText() != 0 || ((itemData->flags >> 9) & 1)) {
                    this->AddLine(FrameScript_GetText("ITEM_READABLE", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 0);
                }

                if (item->GetSocketCount() != 0) {
                    this->AddLine(FrameScript_GetText("ITEM_SOCKETABLE", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_GREEN, TOOLTIP_COLOR_GREEN, 0);
                }
            }

            // With a disenchant waiting for its target: the skill it needs.
            if (!append && Spell_C_IsTargeting()) {
                auto ability = SkillLineAbilityFindForRaceClass(static_cast<uint8_t>(playerRace), static_cast<uint8_t>(playerClass), Spell_C_GetTargetingSpellID());
                auto skill = g_skillLineDB.GetRecord(ability ? ability->m_skillLine : -1);
                auto targeting = g_spellDB.GetRecord(Spell_C_GetTargetingSpellID());

                if (skill && ability && targeting && targeting->m_effect[0] == 99) {
                    auto value = player->GetSkillValue(ability->m_skillLine);
                    auto needed = info->requiredDisenchantSkill;

                    if (needed < 1) {
                        if (needed != 0) {
                            SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_DISENCHANT_NOT_DISENCHANTABLE", -1, GENDER_NOT_APPLICABLE));
                            this->AddLine(line, nullptr, TOOLTIP_COLOR_RED, TOOLTIP_COLOR_RED, 0);
                        } else {
                            SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_DISENCHANT_ANY_SKILL", -1, GENDER_NOT_APPLICABLE));
                            this->AddLine(line, nullptr, TOOLTIP_COLOR_LIGHT_BLUE, TOOLTIP_COLOR_LIGHT_BLUE, 0);
                        }
                    } else {
                        SStrPrintf(line, sizeof(line), FrameScript_GetText("ITEM_DISENCHANT_MIN_SKILL", -1, GENDER_NOT_APPLICABLE), skill->m_displayName, needed);
                        auto& color = value < needed ? TOOLTIP_COLOR_RED : TOOLTIP_COLOR_LIGHT_BLUE;
                        this->AddLine(line, nullptr, color, color, 0);
                    }
                }
            }
        }

        // The equipment sets it belongs to.
        if (*itemGUID != 0) {
            auto manager = CVar::Lookup("equipmentManager");
            auto hide = CVar::Lookup("dontShowEquipmentSetsOnItems");

            if (manager && manager->GetInt() && (!hide || !hide->GetInt())) {
                auto format = FrameScript_GetText("EQUIPMENT_SETS", -1, GENDER_NOT_APPLICABLE);

                if (format && EquipmentManagerGetSetNames(text, sizeof(text), *itemGUID)) {
                    SStrPrintf(line, sizeof(line), format, text);
                    this->AddLine(line, nullptr, TOOLTIP_COLOR_NORMAL, TOOLTIP_COLOR_NORMAL, 1);
                }
            }
        }

        // A refund window, or a window to trade a soulbound loot.
        bool shownWindow = false;

        if (item) {
            if (!item->m_refundInfo) {
                if ((info->flags & 0x1000) && !((item->m_itemFlags >> 2) & 1)) {
                    item->RequestRefundInfo();
                    shownWindow = true;
                }
            } else {
                auto left = static_cast<int32_t>(item->m_refundInfo->refundTimeLeft) - player->GetPlayedTime();

                if (left + 7200 > 0 && !item->HasEnchantments()) {
                    TooltipFormatWindowTime(text, sizeof(text), left);
                    SStrPrintf(line, sizeof(line), FrameScript_GetText("REFUND_TIME_REMAINING", -1, GENDER_NOT_APPLICABLE), text);
                    this->AddLine(" ", nullptr, TOOLTIP_COLOR_NORMAL, TOOLTIP_COLOR_NORMAL, 0);
                    this->AddLine(line, nullptr, TOOLTIP_COLOR_REFUND, TOOLTIP_COLOR_REFUND, 1);
                    shownWindow = true;
                }
            }

            if (item->IsSoulbound() && !item->IsTradeWindowExpired()) {
                auto left = itemData->createPlayedTime - player->GetPlayedTime();

                if (left + 7200 > 0) {
                    TooltipFormatWindowTime(text, sizeof(text), left);
                    SStrPrintf(line, sizeof(line), FrameScript_GetText("BIND_TRADE_TIME_REMAINING", -1, GENDER_NOT_APPLICABLE), text);
                    this->AddLine(" ", nullptr, TOOLTIP_COLOR_NORMAL, TOOLTIP_COLOR_NORMAL, 0);
                    this->AddLine(line, nullptr, TOOLTIP_COLOR_BIND_TRADE, TOOLTIP_COLOR_BIND_TRADE, 1);
                }
            }
        }

        // The price, unless the cursor is picking an item to sell.
        if (GetCursorMode() != 0x11 && !shownWindow && !this->m_noPrice && !noPrice) {
            auto sell = info->sellPrice;
            int32_t maxPrice = -1;

            if (sell == 0) {
                if (MerchantGetGUID() != 0 && item) {
                    bool unsellable = true;

                    if (itemData->containedIn == player->GetGUID()) {
                        unsellable = false;

                        for (uint32_t slot = 0; slot < player->m_bag.m_numSlots; slot++) {
                            if (player->m_bag.m_slots[slot] == item->GetGUID()) {
                                unsellable = slot > 22;
                                break;
                            }
                        }
                    }

                    if (unsellable) {
                        this->AddLine(FrameScript_GetText("ITEM_UNSELLABLE", -1, GENDER_NOT_APPLICABLE), nullptr, TOOLTIP_COLOR_HIGHLIGHT, TOOLTIP_COLOR_HIGHLIGHT, 0);
                    }
                }
            } else {
                int32_t price = sell;

                if (!item) {
                    if (this->m_useLinkInfo) {
                        price = sell * this->m_linkInfo.stackCount;

                        if (this->m_linkInfo.stackCount < this->m_linkInfo.maxStackCount) {
                            maxPrice = this->m_linkInfo.maxStackCount * sell;
                        }
                    }

                    this->RunOnTooltipAddMoneyScript(price, maxPrice);
                } else {
                    if (item->GetUseSpell(0) && !item->HasUseEnchantment() && item->GetMaxCharges() < -1) {
                        price = item->GetCharges() * price / item->GetMaxCharges();
                    }

                    auto repair = item->GetRepairCost();

                    if (repair < price) {
                        if (repair != 0) {
                            price -= repair;
                        }
                    } else {
                        price = 1;
                    }

                    if (static_cast<int32_t>(itemData->stackCount) > 1) {
                        this->RunOnTooltipAddMoneyScript(price * static_cast<int32_t>(itemData->stackCount), maxPrice);
                    } else {
                        this->RunOnTooltipAddMoneyScript(price, maxPrice);
                    }
                }
            }
        }
    }

    if (this->m_onTooltipSetItem.luaRef) {
        this->RunScript(this->m_onTooltipSetItem, 0, nullptr);
    }

    this->Show();
    this->CalculateSize();

    return result;
}
