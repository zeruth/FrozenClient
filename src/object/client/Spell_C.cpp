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
#include "math/SoftFloat.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "ui/game/GameScript.hpp"
#include <cstring>
#include <storm/Hash.hpp>
#include <storm/Array.hpp>

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

// ref: FUN_007fdec0
// Whether an aura's effect reaches a spell: the same spell family, and the effect's class mask
// meeting the spell's.
bool SpellIsAffectedByEffect(const SpellRec* spell, const SpellRec* aura, int32_t effectIndex) {
    if (!spell || !aura || spell->m_spellClassSet != aura->m_spellClassSet) {
        return false;
    }

    auto mask = SpellGetEffectClassMask(aura, effectIndex);

    for (uint32_t i = 0; i < 3; i++) {
        if (spell->m_spellClassMask[i] & mask[i]) {
            return true;
        }
    }

    return false;
}

// ref: FUN_007fd440
// Sorts an effect (and, for the aura-applying effects, its aura) by how its points read. The first
// flag says the points are whole numbers -- the range is floored and ceiled -- and the answer says
// the damage modifiers apply; the second flag picks the periodic damage modifier over the direct
// one. The reference passes first in EAX, second in EDX and code in ECX.
int32_t Spell_C_ClassifyCodePair(uint32_t code, uint8_t* second, uint32_t subCode, uint8_t* first) {
    *first = 0;
    *second = 0;

    switch (code) {
        case 2:
        case 9:
        case 10:
        case 17:
        case 31:
        case 58:
        case 67:
        case 75:
        case 121:
            *first = 1;

            return 1;

        case 8:
        case 30:
        case 62:
        case 146:
            *first = 1;

            return 0;

        case 6:
        case 27:
        case 35:
        case 65:
        case 119:
        case 128:
        case 129:
        case 143:
            switch (subCode) {
                case 15:
                case 43:
                    *first = 1;

                    return 1;

                case 3:
                case 8:
                case 20:
                case 53:
                case 62:
                case 89:
                    *first = 1;
                    *second = 1;

                    return 1;

                case 21:
                case 24:
                case 63:
                case 64:
                case 162:
                    *first = 1;

                    return 0;
            }

            return 0;
    }

    return 0;
}

// ref: FUN_007fdbe0
// The modifier applied to a value held as a float bit pattern, in the same soft arithmetic the
// effect points are computed in: (value + flat) * pct * 0.01.
void SpellApplyModifierSoft(const SpellRec* spell, uint32_t* value, int32_t op) {
    int32_t flat;
    int32_t pct;

    if (!SpellGetModifier(spell, op, &flat, &pct)) {
        return;
    }

    uint32_t pctBits;
    SoftFloatFromInt(&pctBits, pct);

    uint32_t flatBits;
    SoftFloatFromInt(&flatBits, flat);

    uint32_t sum;
    SoftFloatAdd(&sum, value, &flatBits);

    uint32_t scaled;
    SoftFloatMultiply(&scaled, &sum, &pctBits);

    uint32_t result;
    SoftFloatMultiply(&result, &scaled, &g_softFloatHundredth);

    *value = result;
}

// Rounds a soft float to the nearest 1/128: scaled up by 2^7 (a zero stays zero), rounded half
// up, and scaled back down, flushing to zero when that would underflow. Inline in the reference,
// once for each end of the range.
static uint32_t SpellPointsQuantize(uint32_t bits) {
    bits += (bits & 0x7F800000) ? 0x3800000 : 0;

    uint32_t rounded;
    SoftFloatRound(&rounded, &bits);

    uint32_t underflow = static_cast<uint32_t>(static_cast<int32_t>((rounded - 0x4000000) ^ rounded) >> 31);

    return ~underflow & (rounded - 0x3800000);
}

// ref: FUN_007ff770
// The range an effect's points fall in at the caster's level: base + 1 to base + die sides, each
// grown by the per-level points over the levels above the spell's base level. Unless noModifiers,
// the spell modifiers then apply -- all effects, this effect index, damage or periodic damage for
// the effects that deal it, and the aura's own for three aura types. A level of 0 means the
// caster's. Computed in the reference's soft float arithmetic so it agrees bit for bit.
void SpellGetEffectPoints(const SpellRec* spell, int32_t effectIndex, float* minPoints, float* maxPoints, int32_t level, int32_t pet, int32_t inspect, int32_t noModifiers) {
    *minPoints = 0.0f;
    *maxPoints = 0.0f;

    if (!spell) {
        return;
    }

    uint8_t wholeNumbers;
    uint8_t periodic;
    auto damage = Spell_C_ClassifyCodePair(spell->m_effect[effectIndex], &periodic, spell->m_effectAura[effectIndex], &wholeNumbers);

    if (!level) {
        level = SpellGetCasterLevel(spell, pet, inspect);
    }

    if (spell->m_baseLevel > 0) {
        level -= spell->m_baseLevel;
    }

    if (level < 0) {
        level = 0;
    }

    uint32_t perLevel;
    memcpy(&perLevel, &spell->m_effectRealPointsPerLevel[effectIndex], sizeof(perLevel));

    uint32_t levelBits;
    uint32_t growth;

    uint32_t low;
    SoftFloatFromInt(&low, spell->m_effectBasePoints[effectIndex] + 1);
    SoftFloatFromInt(&levelBits, level);
    SoftFloatMultiply(&growth, &perLevel, &levelBits);

    uint32_t sum;
    SoftFloatAdd(&sum, &low, &growth);
    low = sum;

    uint32_t high;
    SoftFloatFromInt(&high, spell->m_effectDieSides[effectIndex] + spell->m_effectBasePoints[effectIndex]);
    SoftFloatFromInt(&levelBits, level);
    SoftFloatMultiply(&growth, &perLevel, &levelBits);

    SoftFloatAdd(&sum, &high, &growth);
    high = sum;

    if (!noModifiers) {
        SpellApplyModifierSoft(spell, &low, 8);
        SpellApplyModifierSoft(spell, &high, 8);

        switch (effectIndex) {
            case 0:
                SpellApplyModifierSoft(spell, &low, 3);
                SpellApplyModifierSoft(spell, &high, 3);
                break;

            case 1:
                SpellApplyModifierSoft(spell, &low, 12);
                SpellApplyModifierSoft(spell, &high, 12);
                break;

            case 2:
                SpellApplyModifierSoft(spell, &low, 23);
                SpellApplyModifierSoft(spell, &high, 23);
                break;
        }

        if (damage) {
            if (periodic) {
                SpellApplyModifierSoft(spell, &low, 22);
                SpellApplyModifierSoft(spell, &high, 22);
            } else {
                SpellApplyModifierSoft(spell, &low, 0);
                SpellApplyModifierSoft(spell, &high, 0);
            }
        }

        auto aura = spell->m_effectAura[effectIndex];

        if (aura == 10 || aura == 103 || aura == 183) {
            SpellApplyModifierSoft(spell, &low, 2);
            SpellApplyModifierSoft(spell, &high, 2);
        }
    }

    low = SpellPointsQuantize(low);
    high = SpellPointsQuantize(high);

    if (wholeNumbers) {
        uint32_t bits;

        SoftFloatFloor(&bits, &low);
        memcpy(minPoints, &bits, sizeof(bits));

        SoftFloatCeil(&bits, &high);
        memcpy(maxPoints, &bits, sizeof(bits));
    } else {
        memcpy(minPoints, &low, sizeof(low));
        memcpy(maxPoints, &high, sizeof(high));
    }
}

// ref: FUN_00800a70
// Milliseconds an aura lasts at the caster's level, capped by the duration row, through the
// duration modifiers unless noModifiers. With applyHaste, a periodic aura that is hasted -- by
// AttributesEx5 0x2000, or by a periodic haste aura (316) on the player reaching it -- is scaled
// by the player's cast speed, unless AttributesEx3 0x20000000 exempts it.
int32_t SpellGetDuration(const SpellRec* spell, int32_t pet, int32_t inspect, int32_t noModifiers, int32_t applyHaste) {
    if (!spell) {
        return 0;
    }

    auto row = g_spellDurationDB.GetRecord(spell->m_durationIndex);

    if (!row) {
        return 0;
    }

    int32_t grown = (SpellGetCasterLevel(spell, pet, inspect) - spell->m_baseLevel) * row->m_durationPerLevel + row->m_duration;
    int32_t duration = row->m_maxDuration;

    if (grown < row->m_maxDuration) {
        duration = grown;
    }

    if (!noModifiers) {
        SpellApplyModifier(spell, &duration, 1);
    }

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\wow-patch-3_3_5_A-BNet\\WoW\\Source\\Object/ObjectClient/Player_C.h", 0xa0));

    if (!player || !applyHaste) {
        return duration;
    }

    uint32_t i = 0;

    while (spell->m_effectAuraPeriod[i] == 0) {
        if (++i > 2) {
            return duration;
        }
    }

    if (!(spell->m_attributesEx5 & 0x2000) && !player->HasAuraAffectingSpell(316, spell)) {
        return duration;
    }

    if (!(spell->m_attributesEx3 & 0x20000000)) {
        auto speed = player->Unit()->modCastingSpeed;

        if (speed >= 0.001f) {
            duration = static_cast<int32_t>(std::nearbyint(static_cast<float>(duration) * speed));
        }
    }

    return duration;
}

// ref: FUN_00802850
// The spell cast in this one's place at the current dungeon or raid difficulty, from its
// SpellDifficulty row: the row's normal spell when the difficulty has none (the 25-player heroic
// first trying the 25-player normal), and the spell itself outside an instance.
int32_t SpellGetDifficultySpellID(int32_t spellID) {
    auto spell = g_spellDB.GetRecord(spellID);

    if (!spell || !spell->m_difficulty) {
        return spellID;
    }

    auto row = g_spellDifficultyDB.GetRecord(spell->m_difficulty);

    if (!row) {
        return spellID;
    }

    auto map = g_mapDB.GetRecord(s_instanceMapID);

    if (!map) {
        return spellID;
    }

    uint32_t difficulty;

    if (map->m_instanceType == 2) {
        difficulty = CGPartyInfo::GetEffectiveMapRaidDifficulty();
    } else if (map->m_instanceType == 1) {
        difficulty = CGPartyInfo::GetEffectiveDungeonDifficulty();
    } else {
        return spellID;
    }

    auto id = row->m_difficultySpellID[difficulty];

    if (id == 0 && (difficulty != 3 || (id = row->m_difficultySpellID[1]) == 0)) {
        id = row->m_difficultySpellID[0];

        return id ? id : spellID;
    }

    return id;
}

// ------------------------------------------------------------------------------------------------
// The skill caches: SkillRaceClassInfo and SkillCostsData grouped by their key, the run of
// SkillLineAbility rows each linkable skill line starts, and the ability each spell is for a race
// and class. Spell_C.cpp 0x00810370 .. 0x00812410.
// ------------------------------------------------------------------------------------------------

// A run of rows sharing one key: the first and how many follow it in the table.
struct SKILLRUN : public TSHashObject<SKILLRUN, HASHKEY_NONE> {
    const void* first;          // +0x18
    int32_t count;              // +0x1c
};

// The ability a spell is for one race and class.
struct SKILLABILITYKEY {
    uint8_t race;
    uint8_t classID;
    int32_t spell;

    bool operator==(const SKILLABILITYKEY& key) const {
        return this->race == key.race && this->classID == key.classID && this->spell == key.spell;
    }
};

struct SKILLABILITYENTRY : public TSHashObject<SKILLABILITYENTRY, SKILLABILITYKEY> {
    const SkillLineAbilityRec* ability;     // +0x1c
};

// The first ability row of a linkable skill line and how many follow.
struct SkillLineLinkInfo {
    const SkillLineAbilityRec* first;
    int32_t count;
};

static TSHashTable<SKILLRUN, HASHKEY_NONE> s_skillRaceClassInfo;           // ref: DAT_00d3f610
static TSHashTable<SKILLRUN, HASHKEY_NONE> s_skillCosts;                   // ref: DAT_00d3f638
static TSHashTable<SKILLABILITYENTRY, SKILLABILITYKEY> s_skillAbilities;   // ref: DAT_00d3f660
static TSGrowableArray<SkillLineLinkInfo> s_skillLineLinks;                // ref: DAT_00d3f688
static int32_t s_skillAbilitiesDirty;                                      // ref: DAT_00d3f60c

// ref: FUN_00812200
// Groups SkillRaceClassInfo by skill line and SkillCostsData by cost id. Both tables are sorted on
// that key, so each group is a run.
void SkillCachesInitialize() {
    SKILLRUN* run = nullptr;
    int32_t key = -1;

    for (int32_t i = 0; i < g_skillRaceClassInfoDB.GetNumRecords(); i++) {
        auto rec = g_skillRaceClassInfoDB.GetRecordByIndex(i);

        if (rec->m_skillID == key) {
            run->count++;
            continue;
        }

        run = s_skillRaceClassInfo.New(static_cast<uint32_t>(rec->m_skillID), HASHKEY_NONE(), 0, 0);
        run->first = rec;
        run->count = 1;
        key = rec->m_skillID;
    }

    run = nullptr;
    key = -1;

    for (int32_t i = 0; i < g_skillCostsDataDB.GetNumRecords(); i++) {
        auto rec = g_skillCostsDataDB.GetRecordByIndex(i);

        if (rec->m_skillCostsID == key) {
            run->count++;
            continue;
        }

        run = s_skillCosts.New(static_cast<uint32_t>(rec->m_skillCostsID), HASHKEY_NONE(), 0, 0);
        run->first = rec;
        run->count = 1;
        key = rec->m_skillCostsID;
    }

    s_skillAbilitiesDirty = 1;
}

// ref: FUN_00810920
// Empties the caches; the ability cache is rebuilt on its next use.
void SkillCachesDestroy() {
    s_skillRaceClassInfo.Clear();
    s_skillAbilitiesDirty = 1;
    s_skillCosts.Clear();
    s_skillAbilities.Clear();
    s_skillLineLinks.SetCount(0);
}

// ref: FUN_00810ed0
// The SkillRaceClassInfo row that opens a skill line to a race and class (an empty mask opening it
// to all).
const SkillRaceClassInfoRec* SkillRaceClassInfoFind(uint8_t race, uint8_t classID, int32_t skillLine) {
    auto run = s_skillRaceClassInfo.Ptr(static_cast<uint32_t>(skillLine), HASHKEY_NONE());

    if (!run) {
        return nullptr;
    }

    auto rec = static_cast<const SkillRaceClassInfoRec*>(run->first);

    for (int32_t i = 0; i < run->count; i++, rec++) {
        if (rec->m_raceMask == 0 || (rec->m_raceMask & (1u << ((race - 1u) & 0x1F)))) {
            if (rec->m_classMask == 0 || (rec->m_classMask & (1u << ((classID - 1u) & 0x1F)))) {
                return rec;
            }
        }
    }

    return nullptr;
}

// ref: FUN_00810f50
// What training a skill line costs at a tier.
int32_t SkillCostsGetCost(const SkillRaceClassInfoRec* info, int32_t tier) {
    auto skillLine = g_skillLineDB.GetRecord(info->m_skillID);

    if (!skillLine || !skillLine->m_skillCostsID) {
        return 0;
    }

    auto run = s_skillCosts.Ptr(static_cast<uint32_t>(skillLine->m_skillCostsID), HASHKEY_NONE());

    if (!run || tier > run->count) {
        return 0;
    }

    // The reference indexes the run as a flat array of five-column rows
    auto columns = reinterpret_cast<const int32_t*>(run->first);

    return columns[tier * 5 + info->m_skillCostIndex - 3];
}

// ref: FUN_008104a0
// The run of abilities a linkable skill line starts.
bool SkillLineGetLinkInfo(int32_t skillLine, const SkillLineAbilityRec** first, int32_t* count) {
    for (uint32_t i = 0; i < s_skillLineLinks.Count(); i++) {
        if (s_skillLineLinks[i].first->m_skillLine == skillLine) {
            *first = s_skillLineLinks[i].first;
            *count = s_skillLineLinks[i].count;

            return true;
        }
    }

    return false;
}

// ref: FUN_00811f20
// Records what an ability is for a race and class, when that race or class is the player's and
// both the skill line and the ability are open to it. The reference passes ability in EBX.
static bool SkillAbilityCacheAdd(uint8_t race, uint8_t classID, const SkillLineAbilityRec* ability) {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\wow-patch-3_3_5_A-BNet\\WoW\\Source\\Object/ObjectClient/Player_C.h", 0xa0));

    if (player
        && race != (player->Unit()->bytes0 & 0xFF)
        && classID != ((player->Unit()->bytes0 >> 8) & 0xFF)) {
        return false;
    }

    if (!SkillRaceClassInfoFind(race, classID, ability->m_skillLine)) {
        return false;
    }

    if (!RaceClassMaskMatches(race, classID, ability->m_raceMask, ability->m_classMask, ability->m_excludeRace, ability->m_excludeClass)) {
        return false;
    }

    SKILLABILITYKEY key = { race, classID, ability->m_spell };
    auto entry = s_skillAbilities.New(static_cast<uint32_t>(ability->m_spell), key, 0, 0);
    entry->ability = ability;

    return true;
}

// ref: FUN_00812030
// Builds the ability cache over every race and class combination CharBaseInfo lists, and on the
// first combination the run of abilities each linkable skill line starts.
static void SkillAbilityCacheBuild() {
    SkillLineLinkInfo* link = nullptr;
    int32_t skillLine = -1;
    bool first = true;

    for (int32_t i = 0; i < g_charBaseInfoDB.GetNumRecords(); i++) {
        auto combo = g_charBaseInfoDB.GetRecordByIndex(i);
        auto race = static_cast<uint8_t>(combo->m_raceID);
        auto classID = static_cast<uint8_t>(combo->m_classID);

        if (!g_chrRacesDB.GetRecord(race)) {
            continue;
        }

        for (int32_t j = 0; j < g_skillLineAbilityDB.GetNumRecords(); j++) {
            auto ability = g_skillLineAbilityDB.GetRecordByIndex(j);

            SkillAbilityCacheAdd(race, classID, ability);

            if (!first) {
                continue;
            }

            if (ability->m_skillLine == skillLine) {
                if (link) {
                    link->count++;
                }

                continue;
            }

            auto line = g_skillLineDB.GetRecord(ability->m_skillLine);

            if (!line || !line->m_canLink) {
                link = nullptr;
                continue;
            }

            link = s_skillLineLinks.New();
            link->first = ability;
            link->count = 1;
            skillLine = ability->m_skillLine;
        }

        first = false;
    }

    s_skillAbilitiesDirty = 0;
}

// ref: FUN_00812410
// The SkillLineAbility row a spell is for a race and class: from the cache, or for a combination
// that shares neither with the player, by searching the table.
const SkillLineAbilityRec* SkillLineAbilityFindForRaceClass(uint8_t race, uint8_t classID, int32_t spell) {
    if (s_skillAbilitiesDirty) {
        SkillAbilityCacheBuild();
    }

    SKILLABILITYKEY key = { race, classID, spell };
    auto entry = s_skillAbilities.Ptr(static_cast<uint32_t>(spell), key);

    if (entry) {
        return entry->ability;
    }

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\wow-patch-3_3_5_A-BNet\\WoW\\Source\\Object/ObjectClient/Player_C.h", 0xa0));

    if (!player) {
        return nullptr;
    }

    auto data = player->Unit();

    if ((data->bytes0 & 0xFF) == race && ((data->bytes0 >> 8) & 0xFF) == classID) {
        return nullptr;
    }

    for (int32_t i = 0; i < g_skillLineAbilityDB.GetNumRecords(); i++) {
        auto ability = g_skillLineAbilityDB.GetRecordByIndex(i);

        if (ability->m_spell == spell
            && SkillRaceClassInfoFind(race, classID, ability->m_skillLine)
            && RaceClassMaskMatches(race, classID, ability->m_raceMask, ability->m_classMask, ability->m_excludeRace, ability->m_excludeClass)) {
            return ability;
        }
    }

    return nullptr;
}

// ref: FUN_008016c0
// Each end rounded the reference's way: (int)round(2v -/+ 0.5) >> 1, away from the half for a
// negative value, under the FPU's round-to-nearest.
void SpellGetEffectSkillValue(const SpellRec* spell, int32_t effectIndex, int32_t* minValue, int32_t* maxValue, int32_t level, int32_t pet, int32_t inspect, int32_t noModifiers) {
    float minPoints;
    float maxPoints;
    SpellGetEffectPoints(spell, effectIndex, &minPoints, &maxPoints, level, pet, inspect, noModifiers);

    auto twiceMin = minPoints < 0.0f ? minPoints + minPoints + 0.5f : minPoints + minPoints - 0.5f;
    *minValue = static_cast<int32_t>(std::nearbyint(twiceMin)) >> 1;

    auto twiceMax = maxPoints < 0.0f ? maxPoints + maxPoints + 0.5f : maxPoints + maxPoints - 0.5f;
    *maxValue = static_cast<int32_t>(std::nearbyint(twiceMax)) >> 1;
}

// The cast waiting for the player to pick a target; its spell is at +0x20. The spell cast port
// creates it; nothing does until then.
struct SPELL_TARGETING_CAST {
    uint8_t unk00[0x20];
    int32_t spellID;            // +0x20
};

static SPELL_TARGETING_CAST* s_targetingCast;       // ref: DAT_00d3f4e4

// ref: FUN_007fd620
bool Spell_C_IsTargeting() {
    return s_targetingCast != nullptr;
}

// ref: FUN_007fd630
int32_t Spell_C_GetTargetingSpellID() {
    return s_targetingCast ? s_targetingCast->spellID : 0;
}

// ref: FUN_00719980
// The reference reads the current map from DAT_00bd088c; frozen's is the object manager's.
bool SpellIsUsableInArena(const SpellRec* spell) {
    auto map = g_mapDB.GetRecord(static_cast<int32_t>(ClntObjMgrGetMapID()));

    if (map && map->m_instanceType == 4) {
        if (!(spell->m_attributesEx4 & 0x20000) && (spell->m_recoveryTime > 900000 || spell->m_categoryRecoveryTime > 900000 || (spell->m_attributesEx4 & 0x10000))) {
            return false;
        }
    }

    return true;
}
