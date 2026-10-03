#include "object/client/Spell_C.hpp"
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "net/Types.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/Types.hpp"
#include <common/DataStore.hpp>
#include <storm/String.hpp>
#include "object/client/CGItem_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "ui/game/CGPetInfo.hpp"
#include "ui/game/CharacterInfoScript.hpp"
#include "object/client/CGUnit_C.hpp"
#include <cmath>

// Written by the cast and combat code, none of which is ported yet; the reference
// zero-initialises all of it.

// The spell repeating on its own, the one CMSG_CANCEL_AUTO_REPEAT_SPELL cancels.
static int32_t s_autoRepeatSpell;               // ref: DAT_00d397d0
// A second repeat spell; START/STOP_AUTOREPEAT_SPELL fire for it only while the one above is
// unset. The player's attack paths clear it.
static int32_t s_pendingAutoRepeatSpell;        // ref: DAT_00d397cc
// The last cast failure reported for s_pendingAutoRepeatSpell, so the same one is not repeated.
static int32_t s_pendingAutoRepeatError;        // ref: DAT_00d397c8

static STORM_LIST(SpellCastNode) s_spellCasts;          // ref: DAT_00af524c
// Where SpellCastRetire moves a node out of s_spellCasts.
static STORM_LIST(SpellCastNode) s_retiredSpellCasts;   // ref: DAT_00af5258

// ref: FUN_007fde20
bool SpellHasEffect(const SpellRec* spell, int32_t effect) {
    for (uint32_t i = 0; i < 3; i++) {
        if (spell->m_effect[i] == effect) {
            return true;
        }
    }

    return false;
}

// ref: FUN_007fde50
bool SpellHasAura(const SpellRec* spell, int32_t aura) {
    for (uint32_t i = 0; i < 3; i++) {
        if (spell->m_effectAura[i] == aura) {
            return true;
        }
    }

    return false;
}

// ref: FUN_007fde80
const uint32_t* SpellGetEffectClassMask(const SpellRec* spell, int32_t effectIndex) {
    if (effectIndex == 0) {
        return spell->m_effectSpellClassMask[0];
    }

    if (effectIndex != 1) {
        if (effectIndex != 2) {
            return nullptr;
        }

        return spell->m_effectSpellClassMask[2];
    }

    return spell->m_effectSpellClassMask[1];
}

// ref: FUN_007fdfa0
// Whether an aura spell's immunity effects cover one effect of another spell. The reference takes
// the aura in EAX and the effect index in EDI.
bool SpellAuraGrantsImmunity(const SpellRec* aura, const SpellRec* spell, int32_t effectIndex) {
    if (!aura || !(aura->m_attributesEx & 0x8000) || (spell->m_attributes & 0x20000000)) {
        return false;
    }

    for (uint32_t i = 0; i < 3; i++) {
        if (aura->m_effect[i] != 6) {
            continue;
        }

        uint32_t misc = static_cast<uint32_t>(aura->m_effectMiscValue[i]);

        switch (aura->m_effectAura[i]) {
            case 0x26:
                if (misc == static_cast<uint32_t>(spell->m_effectAura[effectIndex])) {
                    return true;
                }

                break;

            case 0x27:
            case 0x10b:
                if (!(spell->m_attributesEx2 & 0x4000000) && (misc & spell->m_schoolMask)) {
                    return true;
                }

                break;

            case 0x29:
                if (misc == static_cast<uint32_t>(spell->m_dispel)) {
                    return true;
                }

                break;

            case 0x4d:
                if (misc == static_cast<uint32_t>(spell->m_mechanic)) {
                    return true;
                }

                if (misc == static_cast<uint32_t>(spell->m_effectMechanic[effectIndex])) {
                    return true;
                }

                break;

            default:
                break;
        }
    }

    return false;
}

// ref: FUN_007fe130
int32_t SpellGetAutoRepeatSpell() {
    return s_autoRepeatSpell;
}

// ref: FUN_007fe140
void SpellSetPendingAutoRepeatSpell(int32_t spellID) {
    if (s_pendingAutoRepeatSpell == spellID) {
        return;
    }

    s_pendingAutoRepeatSpell = spellID;

    if (s_autoRepeatSpell != 0) {
        return;
    }

    if (spellID) {
        FrameScript_SignalEvent(SCRIPT_START_AUTOREPEAT_SPELL, nullptr);
        return;
    }

    FrameScript_SignalEvent(SCRIPT_STOP_AUTOREPEAT_SPELL, nullptr);
}

// ref: FUN_007fe180
int32_t SpellGetPendingAutoRepeatSpell() {
    return s_pendingAutoRepeatSpell;
}

// ref: FUN_007fe190
void SpellResetPendingAutoRepeatError() {
    s_pendingAutoRepeatError = 0xbb;
}

// ref: FUN_007fe1b0
// 2 when the spell reaches an enemy target, 1 when it reaches a friendly or party one, 0 when
// neither, read from its target flags and implicit targets.
int32_t SpellTargetDisposition(const SpellRec* spell) {
    if (spell->m_targets & 0x100) {
        return 1;
    }

    if (static_cast<int8_t>(spell->m_targets) < 0) {
        return 2;
    }

    for (uint32_t i = 0; i < 3; i++) {
        switch (spell->m_effectImplicitTargetA[i]) {
            case 2:
            case 6:
            case 0xf:
            case 0x10:
            case 0x18:
            case 0x1c:
            case 0x35:
            case 0x36:
            case 0x5d:
                return 2;
        }

        switch (spell->m_effectImplicitTargetB[i]) {
            case 2:
            case 6:
            case 0xf:
            case 0x10:
            case 0x18:
            case 0x1c:
            case 0x35:
            case 0x36:
            case 0x5d:
                return 2;
        }
    }

    for (uint32_t i = 0; i < 3; i++) {
        switch (spell->m_effectImplicitTargetA[i]) {
            case 1:
                if (spell->m_effectAura[i] != 4) {
                    return 1;
                }

                break;

            case 3:
            case 4:
            case 5:
            case 0x14:
            case 0x15:
            case 0x1b:
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x21:
            case 0x22:
            case 0x23:
            case 0x2d:
            case 0x38:
            case 0x39:
            case 0x3a:
            case 0x3b:
            case 0x3d:
            case 0x3e:
                return 1;
        }

        switch (spell->m_effectImplicitTargetB[i]) {
            case 1:
                if (spell->m_effectAura[i] != 4) {
                    return 1;
                }

                break;

            case 3:
            case 4:
            case 5:
            case 0x14:
            case 0x15:
            case 0x1b:
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x21:
            case 0x22:
            case 0x23:
            case 0x2d:
            case 0x38:
            case 0x39:
            case 0x3a:
            case 0x3b:
            case 0x3d:
            case 0x3e:
                return 1;
        }
    }

    return 0;
}

// ref: FUN_007fe4b0
// DIVERGED: the reference tests powerType > 7 against its seven tokens, so 7 reads one past the
// end of its stack array. Frozen answers "" for 7 as it does for anything larger.
const char* SpellPowerTypeToken(uint32_t powerType) {
    const char* tokens[7] = {
        "MANA",
        "RAGE",
        "FOCUS",
        "ENERGY",
        "HAPPINESS",
        "RUNES",
        "RUNIC_POWER",
    };

    if (powerType > 6) {
        return "";
    }

    return tokens[powerType];
}

// ref: FUN_007fe820
// The ten ids after the record's first field.
bool RecordListContains(int32_t value, const int32_t* record) {
    for (uint32_t i = 0; i < 10; i++) {
        record++;

        if (*record == value) {
            return true;
        }
    }

    return false;
}

// ref: FUN_007fe850
bool SpellRequiresForm(const SpellRec* spell, int32_t formIndex) {
    if (formIndex < 0) {
        return false;
    }

    return (spell->m_stances[formIndex / 32] & (1u << (formIndex & 0x1f))) != 0;
}

// ref: FUN_007fe890
bool SpellExcludesForm(const SpellRec* spell, int32_t formIndex) {
    if (formIndex < 0) {
        return false;
    }

    return (spell->m_stancesNot[formIndex / 32] & (1u << (formIndex & 0x1f))) != 0;
}

// ref: FUN_007fe8d0
// Cast by the unit the player controls: its charm, or its summon when it charms nothing.
bool SpellCastIsByPlayerPet(const SpellCastNode* node) {
    if (ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__)) {
        auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
        auto data = player->Unit();

        const WOWGUID* pet = &data->charm;

        if (*pet == 0) {
            pet = &data->summon;
        }

        if (*pet == node->m_casterGUID) {
            return true;
        }
    }

    return false;
}

// ref: FUN_007fea30
// ItemSubClass.dbc has no id column, so this scans for the class with a subclass in the mask.
const ItemSubClassRec* ItemSubClassFindByMask(int32_t itemClass, uint32_t subClassMask) {
    for (int32_t i = 0; i < g_itemSubClassDB.GetNumRecords(); i++) {
        auto rec = g_itemSubClassDB.GetRecordByIndex(i);

        if (rec->m_classID == itemClass && (subClassMask & (1u << (rec->m_subClassID & 0x1f)))) {
            return rec;
        }
    }

    return nullptr;
}

// ref: FUN_007fef10
void SpellDisplayCustomError(char* buffer, uint32_t bufferSize, int32_t error) {
    char token[64];
    SStrPrintf(token, sizeof(token), "%s_%d", "SPELL_FAILED_CUSTOM_ERROR", error);

    auto text = FrameScript_GetText(token, -1, GENDER_NOT_APPLICABLE);
    SStrCopy(buffer, text, bufferSize);

    CGGameUI::DisplayError(0x30, buffer);
}

// ref: FUN_00800390
void SpellDisplayPetTameError(uint8_t result) {
    const char* token;

    switch (result) {
        case 1:
            token = "PETTAME_INVALIDCREATURE";
            break;
        case 2:
            token = "PETTAME_TOOMANY";
            break;
        case 3:
            token = "PETTAME_CREATUREALREADYOWNED";
            break;
        case 4:
            token = "PETTAME_NOTTAMEABLE";
            break;
        case 5:
            token = "PETTAME_ANOTHERSUMMONACTIVE";
            break;
        case 6:
            token = "PETTAME_UNITSCANTTAME";
            break;
        case 7:
            token = "PETTAME_NOPETAVAILABLE";
            break;
        case 8:
            token = "PETTAME_INTERNALERROR";
            break;
        case 9:
            token = "PETTAME_TOOHIGHLEVEL";
            break;
        case 10:
            token = "PETTAME_DEAD";
            break;
        case 11:
            token = "PETTAME_NOTDEAD";
            break;
        case 12:
            token = "PETTAME_CANTCONTROLEXOTIC";
            break;
        default:
            token = "PETTAME_UNKNOWNERROR";
            break;
    }

    auto text = FrameScript_GetText(token, -1, GENDER_NOT_APPLICABLE);

    char buffer[1024];
    SStrCopy(buffer, text, sizeof(buffer));

    CGGameUI::DisplayError(0xfd, buffer);
}

// ref: FUN_008008d0
void SpellSendCancelChannelling(int32_t spellID) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_CANCEL_CHANNELLING));
    msg.Put(static_cast<uint32_t>(spellID));
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_00800950
// Whether any effect is a shapeshift (aura 0x24) into a form that is missing or lacks flag 1.
bool SpellChangesShapeshiftForm(const SpellRec* spell) {
    for (uint32_t i = 0; i < 3; i++) {
        if (spell->m_effectAura[i] != 0x24) {
            continue;
        }

        auto form = g_spellShapeshiftFormDB.GetRecord(spell->m_effectMiscValue[i]);

        if (!form || !(form->m_flags & 1)) {
            return true;
        }
    }

    return false;
}

// ref: FUN_00800d00
// The caster's or the target's impact kit, falling back to the plain impact kit when that one
// does not resolve.
const SpellVisualKitRec* SpellVisualGetImpactKit(const SpellVisualRec* visual, int32_t caster) {
    int32_t kitID = caster ? visual->m_casterImpactKit : visual->m_targetImpactKit;

    auto kit = g_spellVisualKitDB.GetRecord(kitID);

    if (!kit) {
        kit = g_spellVisualKitDB.GetRecord(visual->m_impactKit);
    }

    return kit;
}

// ref: FUN_00805100
// The reference takes the node in EDI.
void SpellCastRetire(SpellCastNode* node) {
    for (auto cast = s_spellCasts.Head(); cast; cast = s_spellCasts.Next(cast)) {
        if (cast == node) {
            s_retiredSpellCasts.LinkToTail(node);
            return;
        }
    }
}

// ref: FUN_00805180
SpellCastNode* SpellCastFind(uint8_t castID, int32_t spellID, const WOWGUID& caster) {
    if (spellID == 0) {
        return nullptr;
    }

    for (auto cast = s_spellCasts.Head(); cast; cast = s_spellCasts.Next(cast)) {
        if (cast->m_castID == castID && cast->m_spellID == spellID && cast->m_casterGUID == caster) {
            return cast;
        }
    }

    return nullptr;
}

// ref: FUN_00805f60
// A zero source matches any.
bool SpellCastIsPending(const WOWGUID& source, int32_t spellID, const SpellCastNode* exclude) {
    for (auto cast = s_spellCasts.Head(); cast; cast = s_spellCasts.Next(cast)) {
        if (cast == exclude) {
            continue;
        }

        if ((source == 0 || cast->m_sourceGUID == source) && cast->m_spellID == spellID) {
            return true;
        }
    }

    return false;
}

// ref: FUN_00805fc0
bool SpellCastIsPendingFromItem(int32_t itemEntry) {
    for (auto cast = s_spellCasts.Head(); cast; cast = s_spellCasts.Next(cast)) {
        if (cast->m_sourceGUID == cast->m_casterGUID) {
            continue;
        }

        auto item = ClntObjMgrObjectPtr(cast->m_sourceGUID, TYPE_ITEM, __FILE__, __LINE__);

        if (item && item->GetEntryID() == itemEntry) {
            return true;
        }
    }

    return false;
}

// ref: FUN_00810320
// An empty mask admits every race or class; the invert flags turn a mask into an exclusion.
bool RaceClassMaskMatches(uint8_t race, uint8_t classID, uint32_t raceMask, uint32_t classMask, int32_t invertRace, int32_t invertClass) {
    if (invertRace) {
        raceMask = ~raceMask;
    }

    if (invertClass) {
        classMask = ~classMask;
    }

    return (raceMask == 0 || (raceMask & (1u << ((race - 1u) & 0x1f))))
        && (classMask == 0 || (classMask & (1u << ((classID - 1u) & 0x1f))));
}

// ref: FUN_00810410
const SkillLineAbilityRec* SkillLineAbilityFind(int32_t skillLine, int32_t spell) {
    for (int32_t i = 0; i < g_skillLineAbilityDB.GetNumRecords(); i++) {
        auto rec = g_skillLineAbilityDB.GetRecordByIndex(i);

        if (rec->m_skillLine == skillLine && rec->m_spell == spell) {
            return rec;
        }
    }

    return nullptr;
}

// ------------------------------------------------------------------------------------------------
// The spell modifiers the server sets (talents and the like), and the spell figures they change:
// power cost, cast time, range and cooldown. Spell_C.cpp 0x007fd970 .. 0x008012f0.
// ------------------------------------------------------------------------------------------------

// The flat and percent modifier tables: one entry per spell family flag bit (96) and modifier op
// (31), set by SMSG_SET_FLAT_SPELL_MODIFIER and SMSG_SET_PCT_SPELL_MODIFIER (DAT_00d3c658,
// DAT_00d397d8).
static const uint32_t SPELL_MOD_BITS = 96;
static const uint32_t SPELL_MOD_OPS = 31;
static int32_t s_spellModFlat[SPELL_MOD_BITS * SPELL_MOD_OPS];
static int32_t s_spellModPct[SPELL_MOD_BITS * SPELL_MOD_OPS];

// The spell family the player's class casts from; only its spells take the modifiers
// (DAT_00d397b4).
static int32_t s_spellClassSet;

// How many of each power a displayed point stands for: rage and runic power are kept in tenths,
// happiness in thousandths (DAT_00af5220).
static const int32_t s_powerDivisor[] = { 1, 10, 1, 1, 1000, 1, 10 };

// ref: FUN_007fdc60
// The value for one family bit and op. The two opcodes share the handler and differ in table.
//
// DIVERGED: a bit or op past the tables is dropped. The reference indexes without a check.
int32_t ReceiveSpellModifier(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t bit;
    uint8_t op;
    uint32_t value;

    msg->Get(bit);
    msg->Get(op);
    msg->Get(value);

    auto index = bit * SPELL_MOD_OPS + op;

    if (bit >= SPELL_MOD_BITS || op >= SPELL_MOD_OPS) {
        return 1;
    }

    if (msgId == SMSG_SET_PCT_SPELL_MODIFIER) {
        s_spellModPct[index] = static_cast<int32_t>(value);
    } else {
        s_spellModFlat[index] = static_cast<int32_t>(value);
    }

    return 1;
}

// ref: FUN_008007a0
void SpellSetClassSet(int32_t classID) {
    auto classRec = g_chrClassesDB.GetRecord(classID);

    if (classRec) {
        s_spellClassSet = classRec->m_spellClassSet;
    }
}

// ref: FUN_007fd970
// The flat and percent modifiers for op on a spell of the player's family, summed over every family
// bit the spell carries; the percent comes back as a multiplier in hundredths. Answers whether
// there are any. A spell of another family, or one with AttributesEx3 0x20000000, takes none.
bool SpellGetModifier(const SpellRec* spell, int32_t op, int32_t* flat, int32_t* pct) {
    if (!spell->m_spellClassSet || spell->m_spellClassSet != s_spellClassSet) {
        *flat = 0;
        *pct = 100;

        return false;
    }

    if (spell->m_attributesEx3 & 0x20000000) {
        *flat = 0;
        *pct = 100;

        return false;
    }

    *pct = 0;
    *flat = 0;

    for (uint32_t bit = 0; bit < SPELL_MOD_BITS; bit++) {
        if (spell->m_spellClassMask[bit >> 5] & (1u << (bit & 31))) {
            *flat += s_spellModFlat[bit * SPELL_MOD_OPS + op];
            *pct += s_spellModPct[bit * SPELL_MOD_OPS + op];
        }
    }

    if (*flat == 0 && *pct == 0) {
        return false;
    }

    *pct += 100;

    if (*pct < 0) {
        *pct = 0;
    }

    return true;
}

// ref: FUN_007fdb50
void SpellApplyModifier(const SpellRec* spell, int32_t* value, int32_t op) {
    int32_t flat;
    int32_t pct;

    if (SpellGetModifier(spell, op, &flat, &pct)) {
        *value = (*value + flat) * pct / 100;
    }
}

// ref: FUN_007fdba0
void SpellApplyModifier(const SpellRec* spell, float* value, int32_t op) {
    int32_t flat;
    int32_t pct;

    if (SpellGetModifier(spell, op, &flat, &pct)) {
        *value = (static_cast<float>(flat) + *value) * static_cast<float>(pct) * 0.01f;
    }
}

// ref: FUN_00800770
bool SpellHasModifier(const SpellRec* spell, int32_t op) {
    int32_t flat;
    int32_t pct;

    return SpellGetModifier(spell, op, &flat, &pct);
}

// ref: FUN_007fde00
int32_t SpellPowerDivisor(int32_t powerType) {
    if (powerType < 0) {
        return 1;
    }

    return s_powerDivisor[powerType];
}

// ref: FUN_007ff070
// The level the spell scales with: the caster's skill in it, a level being five points. The caster
// is the player being inspected, the pet, or the player.
int32_t SpellGetCasterLevel(const SpellRec* spell, int32_t pet, int32_t inspect) {
    CGUnit_C* caster;

    if (inspect) {
        caster = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(s_inspectGUID, TYPE_UNIT, ".\\Spell_C.cpp", 0x7af));
    } else if (pet) {
        caster = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(CGPetInfo::GetPet(0), TYPE_UNIT, ".\\Spell_C.cpp", 0x7b1));
    } else {
        caster = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
    }

    if (!caster) {
        return 0;
    }

    return static_cast<uint32_t>(caster->GetSpellSkill(spell)) / 5;
}

// ref: FUN_007ff100
// The power a channelled or toggled spell spends each second at the caster's level.
int32_t SpellGetPowerCostPerTime(const SpellRec* spell, CGUnit_C* caster) {
    if (!caster) {
        return 0;
    }

    auto level = static_cast<uint32_t>(caster->GetSpellSkill(spell)) / 5;
    int32_t cost = (static_cast<int32_t>(level) - spell->m_baseLevel) * spell->m_manaPerSecondPerLevel + spell->m_manaPerSecond;

    if (cost > 0) {
        int32_t flat;
        int32_t pct;

        if (SpellGetModifier(spell, 14, &flat, &pct)) {
            cost = pct * cost / 100;
        }
    }

    return cost;
}

// ref: FUN_007ff180
// Milliseconds: the cast time row at the caster's level, at least its minimum, through the cast
// time modifiers, scaled by the caster's cast speed (not for an ability or a spell with
// AttributesEx3 0x20000000). A ranged spell takes half a second more and, for the player, scales
// by the ranged weapon's speed against its own. Never below zero unless allowNegative.
int32_t SpellGetCastTime(const SpellRec* spell, int32_t pet, int32_t inspect, int32_t allowNegative) {
    if (!spell) {
        return 0;
    }

    auto castTimes = g_spellCastTimesDB.GetRecord(spell->m_castingTimeIndex);

    if (!castTimes) {
        return 0;
    }

    auto level = SpellGetCasterLevel(spell, pet, inspect);
    int32_t castTime = (level - spell->m_baseLevel) * castTimes->m_perLevel + castTimes->m_base;

    if (castTime < castTimes->m_minimum) {
        castTime = castTimes->m_minimum;
    }

    SpellApplyModifier(spell, &castTime, 10);

    auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, ".\\Spell_C.cpp", 0x82a));

    if (!(spell->m_attributes & 0x30) && !(spell->m_attributesEx3 & 0x20000000)) {
        CGUnit_C* caster = player;

        if (pet) {
            if (!player) {
                goto ranged;
            }

            auto petGUID = player->Unit()->charm ? player->Unit()->charm : player->Unit()->summon;
            caster = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(petGUID, TYPE_UNIT, ".\\Spell_C.cpp", 0x82f));
        }

        if (caster && castTime > 0) {
            auto speed = caster->Unit()->modCastingSpeed;

            if (speed != 1.0f) {
                castTime = static_cast<int32_t>(static_cast<float>(castTime) * speed);
            }
        }
    }

ranged:
    if (spell->m_attributes & 0x2) {
        castTime += 500;

        if (!pet) {
            auto active = CGPlayer_C::GetActivePtr();

            if (active) {
                auto weapon = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(active->Player()->invSlots[17], TYPE_ITEM, ".\\Spell_C.cpp", 0x83b));

                if (weapon) {
                    auto stats = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(weapon->GetEntryID())), nullptr, nullptr, nullptr, false);

                    if (stats && stats->delay) {
                        auto attackTime = active->Unit()->rangedAttackTime;

                        if (attackTime) {
                            castTime = static_cast<int32_t>(std::lround(static_cast<double>(attackTime) / stats->delay * castTime));
                        }
                    }
                }
            }
        }
    }

    if (!allowNegative && castTime < 1) {
        castTime = 0;
    }

    return castTime;
}

// ref: FUN_007ff380
// The range is melee range.
bool SpellRangeIsMelee(const SpellRec* spell) {
    auto range = g_spellRangeDB.GetRecord(spell->m_rangeIndex);

    return range && (range->m_flags & 1);
}

// ref: FUN_007ff3c0
// The range is counted from the edges of the two units' combat reach.
bool SpellRangeUsesCombatReach(const SpellRec* spell) {
    auto range = g_spellRangeDB.GetRecord(spell->m_rangeIndex);

    return range && (range->m_flags & 2);
}

// ref: FUN_007ff400
// The minimum range shown: none for melee, five yards past the record's for a combat reach range.
void SpellGetDisplayMinRange(const SpellRec* spell, float* minRange) {
    auto range = g_spellRangeDB.GetRecord(spell->m_rangeIndex);

    if (!range) {
        return;
    }

    auto friendly = SpellTargetDisposition(spell) == 1 ? 1 : 0;

    if (range->m_flags & 1) {
        *minRange = 0.0f;
        return;
    }

    if (range->m_flags & 0x7fffffff) {
        *minRange = 5.0f + range->m_rangeMin[friendly];
        return;
    }

    *minRange = range->m_rangeMin[friendly];
}

// ref: FUN_007ff480
// The range from caster to target: the record's hostile or friendly band, widened by the two
// units' combat reach (melee is all reach, at least five yards), a little more when both are
// running, a ranged spell scaled by the player's ranged weapon, then the range modifiers. A spell
// with Attributes 0x404 reaches a hundred yards.
void SpellGetRange(CGUnit_C* caster, const SpellRec* spell, float* minRange, float* maxRange, int32_t friendly, CGObject_C* target) {
    auto unitTarget = target && target->IsA(TYPE_UNIT) ? static_cast<CGUnit_C*>(target) : nullptr;

    if (spell->m_attributes & 0x404) {
        *maxRange = 100.0f;
        return;
    }

    float reach = 0.0f;
    auto range = g_spellRangeDB.GetRecord(spell->m_rangeIndex);

    if (range) {
        if (!(range->m_flags & 1)) {
            float min;

            if (!(range->m_flags & 2)) {
                min = range->m_rangeMin[friendly];
            } else {
                auto other = unitTarget ? unitTarget : caster;
                float edge = other->Unit()->combatReach + caster->Unit()->combatReach + 1.3333334f;

                *minRange = edge;

                if (edge <= 5.0f) {
                    *minRange = 5.0f;
                    min = 5.0f + range->m_rangeMin[friendly];
                } else {
                    *minRange = edge;
                    min = edge + range->m_rangeMin[friendly];
                }
            }

            *minRange = min;
            *maxRange = range->m_rangeMax[friendly];

            if (target && target->IsA(static_cast<OBJECT_TYPE>(TYPE_UNIT | 0x80))) {
                auto other = unitTarget ? unitTarget : caster;
                reach = caster->Unit()->combatReach + other->Unit()->combatReach;

                if (*minRange != 0.0f && !(range->m_flags & 2)) {
                    *minRange = reach + *minRange;
                }
            }
        } else {
            *minRange = 0.0f;

            auto other = unitTarget;

            if (!other) {
                other = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(caster->m_attackTarget, TYPE_UNIT, ".\\Spell_C.cpp", 0x89d));
            }

            float otherReach = other ? other->Unit()->combatReach : caster->Unit()->combatReach;
            reach = otherReach + caster->Unit()->combatReach + 1.3333334f;

            if (reach <= 5.0f) {
                reach = 5.0f;
            }
        }

        if (unitTarget
            && (caster->m_localMove.GetMoveFlags() & 0x100d)
            && (unitTarget->m_localMove.GetMoveFlags() & 0x100d)
            && !caster->IsMovingAtWalkPace()
            && !unitTarget->IsMovingAtWalkPace()
            && ((range->m_flags & 1) || unitTarget->IsA(TYPE_PLAYER))) {
            reach += 2.6666667f;
        }
    }

    if ((spell->m_attributes & 0x2) && caster->IsA(TYPE_PLAYER)) {
        auto player = static_cast<CGPlayer_C*>(caster);
        auto weapon = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(player->Player()->invSlots[17], TYPE_ITEM, ".\\Spell_C.cpp", 0x8c6));

        if (weapon) {
            auto stats = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(weapon->GetEntryID())), nullptr, nullptr, nullptr, false);

            if (stats) {
                *maxRange = stats->rangedModRange * 0.01f * *maxRange;
            }
        }
    }

    SpellApplyModifier(spell, maxRange, 5);

    *maxRange = *maxRange + reach;
}

// ref: FUN_007fef60
// A cooldown with the player's weapon in it: a category flagged for it scales by the main hand's
// speed, a ranged spell adds the ranged attack time (not with AttributesEx2 0x20000), and then
// the cooldown modifiers unless AttributesEx6 0x80000000 forbids them.
void SpellApplyCooldownModifiers(int32_t* cooldown, const SpellRec* spell, int32_t categoryOnly) {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (!player) {
        return;
    }

    if (!categoryOnly) {
        auto category = g_spellCategoryDB.GetRecord(spell->m_category);

        if (category && (category->m_flags & 1)) {
            auto weapon = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(player->Player()->invSlots[15], TYPE_ITEM, __FILE__, __LINE__));

            if (weapon) {
                auto stats = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(weapon->GetEntryID())), nullptr, nullptr, nullptr, false);

                if (stats) {
                    *cooldown = stats->delay * *cooldown / 1000;
                }
            }
        }

        if ((spell->m_attributes & 0x2) && !(spell->m_attributesEx2 & 0x20000)) {
            *cooldown += static_cast<int32_t>(player->Unit()->rangedAttackTime);
        }
    }

    if (!(spell->m_attributesEx6 & 0x80000000)) {
        SpellApplyModifier(spell, cooldown, 11);
    }
}

// ref: FUN_00800d60
// The player casts this without reagents: a player flagged for it and a spell that allows it, or a
// spell of the player's family that the no-reagent-cost mask names.
bool SpellIgnoresReagents(CGPlayer_C* player, const SpellRec* spell) {
    if ((player->Unit()->flags & 0x20) && (spell->m_attributesEx5 & 0x2)) {
        return true;
    }

    if (spell->m_spellClassSet && spell->m_spellClassSet == s_spellClassSet) {
        for (int32_t i = 0; i < 3; i++) {
            if (player->Player()->noReagentCost[i] & spell->m_spellClassMask[i]) {
                return true;
            }
        }
    }

    return false;
}

// ref: FUN_006d71a0
// The item can be used: an item flagged for it, one with no durability, or one not broken -- and
// not one flagged as unusable.
static bool ItemIsUsable(CGItem_C* item) {
    auto data = item->Item();

    return ((data->flags & 0x8) || data->maxDurability == 0 || data->durability != 0) && !(data->flags & 0x10);
}

// ref: FUN_008012f0
// The power a cast spends: the base cost at the caster's level, plus its percentage of the base
// power, plus for an ammo-using player spell the weapon's share, through the caster's school cost
// modifier and multiplier and the cost modifiers. Never below zero; -1 with no caster.
//
// PARTIAL: an NPC's level scaling through gtNPCManaCostScaler (FUN_007f6990, table 6) and the
// totem bar's summed cost (effect 0x61, the action slots from 0x84 through FUN_005a8c30) wait on
// the game tables and the action bar's slot spells.
int32_t SpellGetPowerCost(const SpellRec* spell, CGUnit_C* caster) {
    if (!caster) {
        return -1;
    }

    auto level = static_cast<uint32_t>(caster->GetSpellSkill(spell)) / 5;
    int32_t cost = (static_cast<int32_t>(level) - spell->m_baseLevel) * spell->m_manaCostPerLevel + spell->m_manaCost;

    if (spell->m_manaCostPct) {
        cost += caster->GetBasePower(spell->m_powerType) * spell->m_manaCostPct / 100;
    }

    if ((spell->m_attributesEx4 & 0x400) && caster->GetGUID() == ClntObjMgrGetActivePlayer()) {
        int32_t share = 0;

        auto form = g_spellShapeshiftFormDB.GetRecord(caster->GetShapeshiftForm());

        if (!form || !form->m_combatRoundTime) {
            auto slot = (spell->m_attributesEx3 & 0x1000000) ? 16 : 15;
            auto weapon = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(static_cast<CGPlayer_C*>(caster)->Player()->invSlots[slot], TYPE_ITEM, __FILE__, __LINE__));

            if (!weapon || !ItemIsUsable(weapon) || caster->IsHandHidden((spell->m_attributesEx3 >> 24) & 1)) {
                share = 2000;
            } else {
                auto stats = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(weapon->GetEntryID())), nullptr, nullptr, nullptr, false);

                if (stats) {
                    share = stats->delay;
                }
            }
        } else {
            share = form->m_combatRoundTime;
        }

        cost += static_cast<uint32_t>(share) / 100;
    }

    auto schoolMask = spell->m_schoolMask;
    float scaled = static_cast<float>(caster->GetPowerCostModifier(schoolMask) + cost);
    scaled = caster->GetPowerCostMultiplier(schoolMask) * scaled;
    cost = static_cast<int32_t>(std::lround(scaled));

    int32_t flat;
    int32_t pct;

    if (SpellGetModifier(spell, 14, &flat, &pct)) {
        cost = (flat + cost) * pct / 100;
    }

    // TODO the NPC scaling by gtNPCManaCostScaler and the totem bar's summed cost (see above).

    return cost < 1 ? 0 : cost;
}
