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

CGUnitData* CGUnit::Unit() const {
    return this->m_unit;
}
