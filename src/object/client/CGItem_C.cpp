#include "object/client/CGItem_C.hpp"
#include "db/Db.hpp"

CGItem_C::CGItem_C(uint32_t time, CClientObjCreate& objCreate) : CGObject_C(time, objCreate) {
    // TODO
}

CGItem_C::~CGItem_C() {
    // TODO
}

void CGItem_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    this->CGObject_C::PostInit(time, init, a4);

    // TODO
}

// ref: FUN_00707220
int32_t CGItem_C::GetClassID() const {
    auto rec = g_itemDB.GetRecord(this->GetEntryID());
    return rec ? rec->m_classID : 0;
}

// ref: FUN_00707250
int32_t CGItem_C::GetSubclassID() const {
    auto rec = g_itemDB.GetRecord(this->GetEntryID());
    return rec ? rec->m_subclassID : 0;
}

// ref: FUN_00707300
int32_t CGItem_C::GetDisplayInfoID() const {
    auto rec = g_itemDB.GetRecord(this->GetEntryID());
    return rec ? rec->m_displayInfoID : 0;
}

// ref: FUN_00707280
int32_t CGItem_C::GetInventoryType() const {
    auto rec = g_itemDB.GetRecord(this->GetEntryID());
    return rec ? rec->m_inventoryType : 0;
}

void CGItem_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGObject_C::SetStorage(storage, saved);

    this->m_item = reinterpret_cast<CGItemData*>(&storage[CGItem::GetBaseOffset()]);
    this->m_itemSaved = &saved[CGItem::GetBaseOffsetSaved()];
}
