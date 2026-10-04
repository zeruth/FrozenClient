#include "object/client/CGUnit.hpp"
#include "object/client/CGObject.hpp"
#include "object/client/CMovement_C.hpp"

uint32_t CGUnit::GetBaseOffset() {
    return CGObject::TotalFields();
}

uint32_t CGUnit::GetBaseOffsetSaved() {
    return CGObject::TotalFieldsSaved();
}

uint32_t CGUnit::GetDataSize() {
    return CGUnit::TotalFields() * sizeof(uint32_t);
}

uint32_t CGUnit::GetDataSizeSaved() {
    return CGUnit::TotalFieldsSaved() * sizeof(uint32_t);
}

uint32_t CGUnit::TotalFields() {
    return CGUnit::GetBaseOffset() + 142;
}

uint32_t CGUnit::TotalFieldsSaved() {
    return CGUnit::GetBaseOffsetSaved() + 123;
}

int32_t CGUnit::GetDisplayID() const {
    return this->Unit()->displayID;
}

float CGUnit::GetFacing() const {
    return this->m_move->GetFacing();
}

int32_t CGUnit::GetNativeDisplayID() const {
    return this->Unit()->nativeDisplayID;
}

C3Vector CGUnit::GetPosition() const {
    return this->m_move->GetPosition();
}

float CGUnit::GetRawFacing() const {
    return this->m_move->GetRawFacing();
}

// The stat with a negative value read as zero (branchless in the reference: v & ((v < 0) - 1)).
// ref: FUN_005774b0
int32_t CGUnit::GetStatNonNegative(int32_t index) const {
    auto stat = this->m_unit->stats[index];

    return stat < 0 ? 0 : stat;
}

// ref: FUN_004f54d0
// Splits a resistance into what it would be without buffs and what it is now. The base is computed
// BEFORE the total is clamped, so a resistance debuffed below zero still reports the base it came
// from rather than a base derived from the clamped zero.
//
// FIVE outputs, not four. An earlier pass here wrote it with four, which was enough for
// UnitResistance and wrong as a port: the reference also fills an "effective" value, clamped
// together with the total and equal to it at every point in this function. UnitArmor is what
// needs it -- it returns effective and total as separate values even though nothing here makes
// them differ.
void CGUnit::GetResistanceBreakdown(int32_t index, int32_t* base, int32_t* total, int32_t* effective, int32_t* positive, int32_t* negative) const {
    *effective = this->m_unit->resistance[index];
    *total = *effective;
    *positive = this->m_unit->resistanceBuffModsPositive[index];
    *negative = this->m_unit->resistanceBuffModsNegative[index];
    *base = (*total - *positive) - *negative;

    if (*total < 0) {
        *effective = 0;
        *total = 0;
    }
}

CGUnitData* CGUnit::Unit() const {
    return this->m_unit;
}

// ref: FUN_004f5f00
int32_t CGUnit::HasCharmer() const {
    return this->m_unit->charmedBy != 0;
}

// ref: FUN_004f5f20
// The charmer when there is one, otherwise the creator.
const WOWGUID& CGUnit::GetCharmerOrCreator() const {
    return this->m_unit->charmedBy != 0 ? this->m_unit->charmedBy : this->m_unit->createdBy;
}

// ref: FUN_004f7250
// Unit flag 0x01000000 up, and the guid is the charmer (or, with no charmer, the creator).
int32_t CGUnit::IsControlledBy(const WOWGUID& guid) const {
    if (this->m_unit->flags & 0x01000000) {
        if (guid == this->GetCharmerOrCreator()) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_004f5f80
// Both npc flags 0x40000 and 0x80000 up.
int32_t CGUnit::IsPetitionerAndTabardDesigner() const {
    if (((this->m_unit->npcFlags >> 18) & 1) && ((this->m_unit->npcFlags >> 19) & 1)) {
        return 1;
    }

    return 0;
}

// ref: FUN_005ee050
int32_t CGUnit::GetPowerCostModifier(uint32_t schoolMask) const {
    int32_t result = 0;
    bool found = false;

    for (uint32_t school = 0; school < 7; school++) {
        if ((schoolMask & (1 << school)) && (!found || this->m_unit->powerCostModifier[school] < result)) {
            result = this->m_unit->powerCostModifier[school];
            found = true;
        }
    }

    return result;
}

// ref: FUN_005ee0b0
float CGUnit::GetPowerCostMultiplier(uint32_t schoolMask) const {
    float result = 0.0f;
    bool found = false;

    for (uint32_t school = 0; school < 7; school++) {
        if ((schoolMask & (1 << school)) && (!found || this->m_unit->powerCostMultiplier[school] < result)) {
            result = this->m_unit->powerCostMultiplier[school];
            found = true;
        }
    }

    return result + 1.0f;
}
