#ifndef OBJECT_CLIENT_SPELL_C_HPP
#define OBJECT_CLIENT_SPELL_C_HPP

#include "util/guid/Types.hpp"
#include <storm/List.hpp>
#include <cstdint>
#include "net/Types.hpp"

class CDataStore;
class CGObject_C;
class CGPlayer_C;
class CGUnit_C;
class ItemSubClassRec;
class SkillLineAbilityRec;
class SpellRec;
class SpellVisualKitRec;
class SpellVisualRec;

// The reference's Spell_C.cpp.

// A cast the client is tracking: a node of the lists at 00af524c and 00af5258. Only the fields
// the ported functions read are declared; the reference node is much larger.
struct SpellCastNode : public TSLinkedNode<SpellCastNode> {
    // m_link                       // +0x00
    WOWGUID m_sourceGUID;           // +0x08, the caster, or the item it is cast from
    WOWGUID m_casterGUID;           // +0x10
    // TODO
    int32_t m_spellID;              // +0x20
    uint8_t m_castID;               // +0x24, matched against the byte a unit carries at +0xa5c
    // TODO
};

// ref: FUN_007fde20
bool SpellHasEffect(const SpellRec* spell, int32_t effect);

// ref: FUN_007fde50
bool SpellHasAura(const SpellRec* spell, int32_t aura);

// ref: FUN_007fde80
const uint32_t* SpellGetEffectClassMask(const SpellRec* spell, int32_t effectIndex);

// ref: FUN_007fdfa0
bool SpellAuraGrantsImmunity(const SpellRec* aura, const SpellRec* spell, int32_t effectIndex);

// ref: FUN_007fe130
int32_t SpellGetAutoRepeatSpell();

// ref: FUN_007fe140
void SpellSetPendingAutoRepeatSpell(int32_t spellID);

// ref: FUN_007fe180
int32_t SpellGetPendingAutoRepeatSpell();

// ref: FUN_007fe190
void SpellResetPendingAutoRepeatError();

// ref: FUN_007fe1b0
int32_t SpellTargetDisposition(const SpellRec* spell);

// ref: FUN_007fe4b0
const char* SpellPowerTypeToken(uint32_t powerType);

// ref: FUN_007fe820
bool RecordListContains(int32_t value, const int32_t* record);

// ref: FUN_007fe850
bool SpellRequiresForm(const SpellRec* spell, int32_t formIndex);

// ref: FUN_007fe890
bool SpellExcludesForm(const SpellRec* spell, int32_t formIndex);

// ref: FUN_007fe8d0
bool SpellCastIsByPlayerPet(const SpellCastNode* node);

// ref: FUN_007fea30
const ItemSubClassRec* ItemSubClassFindByMask(int32_t itemClass, uint32_t subClassMask);

// ref: FUN_007fef10
void SpellDisplayCustomError(char* buffer, uint32_t bufferSize, int32_t error);

// ref: FUN_00800390
void SpellDisplayPetTameError(uint8_t result);

// ref: FUN_008008d0
void SpellSendCancelChannelling(int32_t spellID);

// ref: FUN_00800950
bool SpellChangesShapeshiftForm(const SpellRec* spell);

// ref: FUN_00800d00
const SpellVisualKitRec* SpellVisualGetImpactKit(const SpellVisualRec* visual, int32_t caster);

// ref: FUN_00805100
void SpellCastRetire(SpellCastNode* node);

// ref: FUN_00805180
SpellCastNode* SpellCastFind(uint8_t castID, int32_t spellID, const WOWGUID& caster);

// ref: FUN_00805f60
bool SpellCastIsPending(const WOWGUID& source, int32_t spellID, const SpellCastNode* exclude);

// ref: FUN_00805fc0
bool SpellCastIsPendingFromItem(int32_t itemEntry);

// ref: FUN_00810320
bool RaceClassMaskMatches(uint8_t race, uint8_t classID, uint32_t raceMask, uint32_t classMask, int32_t invertRace, int32_t invertClass);

// ref: FUN_00810410
const SkillLineAbilityRec* SkillLineAbilityFind(int32_t skillLine, int32_t spell);

// ref: FUN_007fcc70
// Whether a spell carries any of: attributes 0x404, attributesEx 0x200, attributesEx2 0x100000.
// False for no spell.
bool Spell_C_HasAttributeMask(const SpellRec* spell);

// ref: FUN_007fcca0
// The SPELL_FAILED_* token for a cast result, SPELL_FAILED_UNKNOWN past the last.
const char* Spell_C_GetFailedReasonToken(uint32_t reason);

// ref: FUN_007fd440
// Two flags for a code pair, both cleared first. Returns 1 for the codes that set the first flag
// alone or both, 0 otherwise (including the codes that set the first flag and still answer 0).
int32_t Spell_C_ClassifyCodePair(uint32_t code, uint8_t* second, uint32_t subCode, uint8_t* first);

// ref: FUN_007fd620
// Whether a spell is waiting for its target to be picked.
bool Spell_C_IsTargeting();

// ref: FUN_007fd760
// Whether the pending target flags include 0x10 (an item) or 0x4000.
bool Spell_C_TargetingWantsItem();

// ref: FUN_007fdc60
int32_t ReceiveSpellModifier(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

// ref: FUN_008007a0
void SpellSetClassSet(int32_t classID);

// ref: FUN_007fd970
bool SpellGetModifier(const SpellRec* spell, int32_t op, int32_t* flat, int32_t* pct);

// ref: FUN_007fdb50
void SpellApplyModifier(const SpellRec* spell, int32_t* value, int32_t op);

// ref: FUN_007fdba0
void SpellApplyModifier(const SpellRec* spell, float* value, int32_t op);

// ref: FUN_00800770
bool SpellHasModifier(const SpellRec* spell, int32_t op);

// ref: FUN_007fde00
int32_t SpellPowerDivisor(int32_t powerType);

// ref: FUN_007ff070
int32_t SpellGetCasterLevel(const SpellRec* spell, int32_t pet, int32_t inspect);

// ref: FUN_007ff100
int32_t SpellGetPowerCostPerTime(const SpellRec* spell, CGUnit_C* caster);

// ref: FUN_007ff180
int32_t SpellGetCastTime(const SpellRec* spell, int32_t pet, int32_t inspect, int32_t allowNegative);

// ref: FUN_007ff380
bool SpellRangeIsMelee(const SpellRec* spell);

// ref: FUN_007ff3c0
bool SpellRangeUsesCombatReach(const SpellRec* spell);

// ref: FUN_007ff400
void SpellGetDisplayMinRange(const SpellRec* spell, float* minRange);

// ref: FUN_007ff480
void SpellGetRange(CGUnit_C* caster, const SpellRec* spell, float* minRange, float* maxRange, int32_t friendly, CGObject_C* target);

// ref: FUN_007fef60
void SpellApplyCooldownModifiers(int32_t* cooldown, const SpellRec* spell, int32_t categoryOnly);

// ref: FUN_00800d60
bool SpellIgnoresReagents(CGPlayer_C* player, const SpellRec* spell);

// ref: FUN_008012f0
int32_t SpellGetPowerCost(const SpellRec* spell, CGUnit_C* caster);

#endif
