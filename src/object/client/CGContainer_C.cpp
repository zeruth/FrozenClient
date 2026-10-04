#include "object/client/CGContainer_C.hpp"

// ref: FUN_00706a20
// PARTIAL: the 74 link nodes the reference also initialises at +0x3e8 are not ported.
CGContainer_C::CGContainer_C(uint32_t time, CClientObjCreate& objCreate) : CGItem_C(time, objCreate) {
    this->m_bag.m_owner = this->GetGUID();
    this->m_bag.m_numSlots = this->m_cont->numSlots;
    this->m_bag.m_slots = this->m_cont->slots;
    this->m_bag.m_hasBankSlots = 0;
}

// The reference fills both slots 0x24 and 0x28 with FUN_00706ad0, tagged on GetBag below.
void* CGContainer_C::Virtual024() {
    return &this->m_bag;
}

// ref: FUN_00706ad0
CGBag_C* CGContainer_C::GetBag() {
    return &this->m_bag;
}

CGContainer_C::~CGContainer_C() {
    // TODO
}

void CGContainer_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGItem_C::SetStorage(storage, saved);

    this->m_cont = reinterpret_cast<CGContainerData*>(&storage[CGContainer::GetBaseOffset()]);
    this->m_contSaved = &saved[CGContainer::GetBaseOffsetSaved()];
}
