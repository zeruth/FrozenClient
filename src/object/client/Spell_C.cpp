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
